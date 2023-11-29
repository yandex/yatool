from __future__ import absolute_import
from __future__ import print_function
import six.moves.queue
import errno
import logging
import os
import signal
import socket
import subprocess
import sys
import threading
import time

import core.common_opts
import core.config
import core.yarg
import yalibrary.tools
import exts.fs
import exts.process
import exts.windows
import ide.ide_common
import six

logger = logging.getLogger(__name__)

GDBSERVER_TIMEOUT = 5 * 60  # five minutes


class GDBWrapperOpts(core.yarg.Options):
    def __init__(self):
        super(GDBWrapperOpts, self).__init__()
        self.port = '0'
        self.find_port = False
        self.start_server = False
        self.stop_server = False
        self.patrol_server = False
        self.args = []

    @staticmethod
    def consumer():
        return core.common_opts.ArgConsumer(
            ['-P', '--port'],
            help='Port to run gdbserver on. If 0 will be automatically chosen',
            hook=core.yarg.SetValueHook('port'),
            group=core.yarg.ADVANCED_OPT_GROUP
        ) + core.common_opts.ArgConsumer(
            ['--find-port'],
            help='Find a free port, print it and terminate',
            hook=core.yarg.SetConstValueHook('find_port', True),
            group=core.yarg.DEVELOPERS_OPT_GROUP
        ) + core.yarg.ArgConsumer(
            ['--start-server'],
            help='Start gdbserver, saving it\'s PID before',
            hook=core.yarg.SetConstValueHook('start_server', True),
            group=core.yarg.DEVELOPERS_OPT_GROUP
        ) + core.yarg.ArgConsumer(
            ['--stop-server'],
            help='Stop gdbserver, killing it with pid saved before',
            hook=core.yarg.SetConstValueHook('stop_server', True),
            group=core.yarg.DEVELOPERS_OPT_GROUP
        ) + core.yarg.ArgConsumer(
            ['--patrol-server'],
            help='Kill server if pid file has too late modification time',
            hook=core.yarg.SetConstValueHook('patrol_server', True),
            group=core.yarg.DEVELOPERS_OPT_GROUP
        ) + core.yarg.FreeArgConsumer(
            help='GDB args',
            hook=core.yarg.ExtendHook('args')
        )


class GDBServerException(Exception):
    mute = True


class GDBServer(object):
    def __init__(self, host, remote_cache, port='0'):
        self.host = host
        self.remote_cache = remote_cache
        self.sshproxy = ide.ide_common.SSHProxy(self.host, ssh_args=['-n'])
        self.port = port if port != '0' else self.sshproxy.get_free_port(self.remote_cache)
        self.started = False
        self.pinger = None

    def start(self):
        if self.started:
            logger.debug('Trying to start already started gdbserver.')
            return
        self.sshproxy.start_gdbserver(self.remote_cache, self.port)
        self._ping_server()
        self.started = True
        logger.debug('Server started: %s:%s', self.host, self.port)

    def stop(self):
        if not self.started:
            logger.debug('Trying to stop not running server.')
            return
        self.pinger.cancel()
        self.sshproxy.stop_gdbserver(self.remote_cache, self.port)
        self.started = False
        logger.debug('Server stopped: %s:%s', self.host, self.port)

    def _ping_server(self, interval=60):
        self.sshproxy.touch(ide.ide_common.get_pidfile_path(self.remote_cache, self.port), opts=['-c'])
        self.pinger = threading.Timer(interval, self._ping_server, args=[interval])
        self.pinger.start()

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if exc_type:
            logger.debug('Error during debugging:\n\texc_type: %s\n\texc_val: %s\n\texc_tb: %s', exc_type, exc_val, exc_tb)
            sys.stderr.write('Error during debugging:\n\texc_type: {0}\n\texc_val: {1}\n\texc_tb: {2}\n', exc_type, exc_val, exc_tb)
        try:
            self.stop()
        except Exception as e:
            logger.debug("Couldn't stop gdbserver:\n\t%s", e.args)
        return True


def run_wrapper(params):
    if params.start_server:
        exts.process.popen([sys.executable, 'remote_gdb', '--patrol-server', '--port', params.port])
        gdbserver_exec = yalibrary.tools.tool('gdbserver')
        gdbserver_args = ['--multi', ':' + params.port]
        if gdbserver_exec:
            exts.process.execve(gdbserver_exec, gdbserver_args)
        exts.process.execve(sys.executable, ['tool', 'gdbserver'] + gdbserver_args)
    if params.find_port:
        s = socket.socket()
        s.bind(('', 0))
        port = s.getsockname()[1]
        s.close()
        print(port)
        return
    if params.stop_server:
        pid_file = ide.ide_common.get_pidfile_path(params.remote_cache_path, params.port)
        if not os.path.exists(pid_file):
            raise GDBServerException('PID file not found. Gdbserver could be still running.')
        pid = int(exts.fs.read_file(pid_file).strip())
        try:
            os.kill(pid, signal.SIGKILL)
        except Exception as e:
            logger.debug('Can\'t kill server, probably already dead: %s', e.args)
        exts.fs.ensure_removed(pid_file)
        return
    if params.patrol_server:
        pid_file = ide.ide_common.get_pidfile_path(params.remote_cache_path, params.port)
        try:
            pid = int(exts.fs.read_file(pid_file).strip())
        except Exception as e:
            logger.debug('Can\'t read pid from file %s, suspect process is already finished: %s', e.args)
            return
        while os.path.exists(pid_file) and time.time() - os.path.getmtime(pid_file) < GDBSERVER_TIMEOUT:
            time.sleep(30)
        try:
            os.kill(pid, signal.SIGKILL)
        except Exception as e:
            # server is already dead anyway
            logger.debug('Can\'t kill process %d: %s', pid, e.args)
        exts.fs.ensure_removed(pid_file)

    gdb_exec = yalibrary.tools.tool(u'gdb')
    if gdb_exec:
        gdb_run_cmd = [gdb_exec] + params.args
    else:
        gdb_run_cmd = [sys.executable, 'tool', 'gdb'] + params.args
    if not params.remote_host:
        run_proc_slave(gdb_run_cmd, always_by_line=False)
    else:
        with GDBServer(params.remote_host, params.remote_cache_path, params.port) as gdbserver:
            class _GDBStreamsModifier(object):
                def __init__(self):
                    self.is_waiting = False

                def mod_input(self, input_command):
                    if '-exec-continue' in input_command and self.is_waiting:
                        self.is_waiting = False
                        return 'run\n'
                    if 'target remote' not in input_command or input_command.count('!') != 3:
                        return input_command
                    pre_server, server, target, stuff = input_command.split('!')
                    new_command = '{0}target extended-remote {1}:{2}\nset remote exec-file {3}\n'.format(
                        input_command[:input_command.index('target remote')], server, gdbserver.port, target
                    )
                    logger.debug('Changed gdb input: %s into %s', input_command, new_command)
                    self.is_waiting = True
                    return new_command

            modifier = _GDBStreamsModifier()
            run_proc_slave(gdb_run_cmd, sysin_mod=modifier.mod_input)


def run_proc_slave(cmd, sysin_mod=None, procout_mod=None, procerr_mod=None, always_by_line=True):
    proc = exts.process.popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0, close_fds=exts.windows.on_win())
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    signal.signal(signal.SIGTERM, signal.SIG_IGN)

    msg_queue = six.moves.queue.Queue()

    def process_pipe(pipe, pipe_id, queue, read_by_line=False):
        characters = True
        while characters:
            characters = pipe.readline() if always_by_line or read_by_line else pipe.read(1)
            queue.put((pipe_id, characters))

    def f_id(x):
        return x
    ids = ('sysin', 'procout', 'procerr')
    channels_read = {'sysin': sys.stdin, 'procout': proc.stdout, 'procerr': proc.stderr}
    channels_write = {'sysin': proc.stdin, 'procout': sys.stdout, 'procerr': sys.stderr}
    modifiers = {'sysin': sysin_mod, 'procout': procout_mod, 'procerr': procerr_mod}
    modifiers = {k: (v or f_id) for k, v in six.iteritems(modifiers)}
    threads = [threading.Thread(target=process_pipe, args=(channels_read[_id], _id, msg_queue),
                                kwargs={'read_by_line': (_id == 'sysin')}) for _id in ids]
    for t in threads:
        t.daemon = True
        t.start()
    proc_pipes = 2

    while proc.poll() is None or proc_pipes:
        pipe_id, msg = msg_queue.get()
        try:
            if not msg:
                if pipe_id == 'sysin':
                    channels_write[pipe_id].close()
                else:
                    proc_pipes -= 1
                continue
            else:
                channels_write[pipe_id].write(modifiers[pipe_id](msg))
                channels_write[pipe_id].flush()
        except IOError as e:
            if e.errno == errno.EPIPE:
                logger.debug('Trying to write to finished process. Suppressing the error.')
            else:
                raise
