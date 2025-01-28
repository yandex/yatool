import atexit
import contextlib
import logging
import os
import random
import signal
import socket
import subprocess
import sys
import tempfile
import time

import six

import exts.fs
import exts.func
import exts.uniq_id
import exts.windows

import grpc
import psutil
from devtools.executor.proto import runner_pb2, runner_pb2_grpc


cdef extern from "devtools/executor/lib/server.h":
    int RunServer(char* mount_point, bint cache_stderr, bint debug) nogil


class ShutdownException(Exception):
    pass


def terminate_process(pid):
    try:
        if exts.windows.on_win():
            proc = psutil.Process(pid)
            proc.terminate()
            proc.wait()
        else:
            os.kill(pid, signal.SIGTERM)
            os.waitpid(pid, 0)
    except (OSError, psutil.NoSuchProcess):
        pass

def _get_free_port():
    start = random.randint(2048, 16384)
    for i in xrange(start, start + 1024):
        sock = socket.socket()
        try:
            sock.bind(('localhost', i))
        except socket.error:
            continue
        finally:
            sock.close()
        return i
    raise AssertionError("Failed to find free port")


def _get_mount_point():
    for func in [
        lambda: tempfile.NamedTemporaryFile(delete=False, prefix=str(os.getpid())).name,
        lambda: '/tmp/{}{}'.format(os.getpid(), exts.uniq_id.gen32()),
    ]:
        path = func()
        if len(path) < 104 and os.path.exists(os.path.dirname(path)):
            try:
                os.unlink(path)
            except OSError:
                pass
            return path
    raise AssertionError("Failed to find valid mount point for unix socket")


def _get_address():
    if exts.windows.on_win():
        return "localhost:{}".format(_get_free_port())
    else:
        return "unix:{}".format(_get_mount_point())


def start_executor(terminate_at_exit=True, cache_stderr=True, debug=False, wait_init=True):
    address = _get_address()

    env = dict(os.environ)
    env.update({
        'DEBUG_LOCAL_EXECUTOR': str(int(debug)),
        'Y_PYTHON_ENTRY_POINT': 'devtools.executor.python.executor:_run_server_entry_point',
    })

    cmd = [sys.executable, address, str(int(cache_stderr))]
    logging.debug("Starting local executor with cmd: %s", cmd)

    if debug:
        out_stream = tempfile.NamedTemporaryFile()
    else:
        out_stream = sys.stderr

    proc = subprocess.Popen(cmd, env=env, stdout=out_stream, stderr=out_stream)
    pid = proc.pid

    def wait_till_initialized(connect_timeout=15):
        tries = 0
        last_error = ''
        deadline = time.time() + connect_timeout

        while time.time() < deadline and proc.poll() is None:
            tries += 1
            try:
                with grpc.insecure_channel(address) as channel:
                    stub = runner_pb2_grpc.RunnerStub(channel)
                    stub.Ping(runner_pb2.TEmpty())
            except Exception as e:
                last_error = e
                time.sleep(0.05)
            else:
                if terminate_at_exit:
                    def shutdown():
                        if proc.poll() is None:
                            proc.terminate()

                    atexit.register(shutdown)
                return

        logging.debug("Failed to connect to external executor within %d tries. Last error: %s", tries, last_error)

        if proc.poll() is None:
            proc.terminate()
            proc.wait()

            error_msg = "Failed to connect to the external executor within {} seconds".format(connect_timeout)
            if debug:
                with open(out_stream.name) as afile:
                    data = afile.read()
                error_msg += "\nServer log:\n{}".format(data)
        else:
            error_msg = "External executor failed to start with {} exit code".format(proc.returncode)

        raise Exception(error_msg)


    if wait_init:
        logging.debug("Waiting for initialization")
        wait_till_initialized()
        logging.debug("Executor initialized")
        return pid, address, None
    else:
        logging.debug("Not waiting for initialization, returning waiter function")
        return pid, address, wait_till_initialized


def _run_server_entry_point():
    if sys.platform.startswith("linux"):
        from library.python.prctl import prctl
        prctl.set_pdeathsig(signal.SIGTERM)

    address = sys.argv[1]
    cache_stderr = bool(int(sys.argv[2]))
    debug = bool(int(os.environ.get('DEBUG_LOCAL_EXECUTOR')))
    if debug:
        sys.stderr.write("Staring server at {}\n".format(address))

    cdef bytes rawstr = six.ensure_binary(address)
    cdef char* cstr = rawstr

    try:
        RunServer(cstr, cache_stderr, debug)
        exit(0)
    except Exception:
        exit(1)


@contextlib.contextmanager
def with_executor():
    pid, address, _ = start_executor(terminate_at_exit=False)
    yield address
    terminate_process(pid)


class Result(object):
    def __init__(self, address, res):
        self.address = address
        self.raw_result_iter = res

        response = next(res)
        if response.Error:
            if response.Error == "Shutdown in progress":
                raise ShutdownException(response.Error)
            self.pid = 1
            self.returncode = 1
            self.buff = response.Error
        else:
            self.pid = response.Pid
            self.returncode = None
            self.buff = ''

    def _iter_lines(self, chunk):
        if '\n' not in chunk:
            self.buff += chunk
            return

        lines = (self.buff + chunk).split('\n')
        for line in lines[:-1]:
            yield line + '\n'
        if lines[-1] is not '':
            yield lines[-1]

        self.buff = ''

    def iter_stderr(self):
        for response in self.raw_result_iter:
            if response.ExitCode:
                self.returncode = response.ExitCode
                return

            for chunk in response.StderrLines:
                for line in self._iter_lines(six.ensure_str(chunk, errors='replace')):
                    yield line

        if self.buff:
            yield six.ensure_str(self.buff, errors='replace')
        self.buff = ''
        if self.returncode is None:
            self.returncode = 0


@exts.func.memoize()
def _get_channel(address):
    channel = grpc.insecure_channel(address)
    def close_channel():
        channel.close()
    atexit.register(close_channel)
    return channel


@exts.func.memoize()
def _get_client(address):
    return runner_pb2_grpc.RunnerStub(_get_channel(address))


def fs_encode(s):
    return six.ensure_binary(s, sys.getfilesystemencoding(), 'ignore')


@contextlib.contextmanager
def run_external_process(address, args, stdout=None, cwd=None, env=None, nice=10, requirements=None):
    env = env or dict(os.environ)
    env = [runner_pb2.TEnvEntry(Name=fs_encode(k), Value=fs_encode(v)) for k, v in six.iteritems(env)]

    if cwd is not None:
        cwd = fs_encode(cwd)

    if stdout is not None:
        stdout = fs_encode(stdout)

    proto_requirements = None
    if requirements:
        network = requirements.get("network")
        if network == "restricted":
            network_type = runner_pb2.TRequirements.NetworkDescriptor.Value('RESTRICTED')
        elif network == "full":
            network_type = runner_pb2.TRequirements.NetworkDescriptor.Value('FULL')
        else:
            network_type = None

        proto_requirements = runner_pb2.TRequirements(
            Cpu=requirements.get("cpu"),
            DiskUsage=requirements.get("disk_usage"),
            Ram=requirements.get("ram"),
            RamDisk=requirements.get("ram_disk"),
            Container=requirements.get("container"),
            Network=network_type,
        )

    command = runner_pb2.TCommand(
        Args=[fs_encode(a) for a in args],
        StdoutFilename=stdout,
        Cwd=cwd,
        Env=env,
        Nice=nice,
        Requirements=proto_requirements,
    )
    yield Result(address, _get_client(address).Execute(command))
