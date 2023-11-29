import base64
import logging
import os
import six
import socket
import struct
import tempfile
import threading

import exts.uniq_id
import library.python.windows


logger = logging.getLogger(__name__)

CMD_STOP = 'plsstopit'
# https://a.yandex-team.ru/arc/trunk/arcadia/contrib/libs/musl/include/sys/socket.h?blame=true&rev=5623854#L182
SO_PEERCRED = 17
# https://a.yandex-team.ru/arc/trunk/arcadia/contrib/libs/musl-1.1.19/include/sys/socket.h?rev=3796328&blame=true#L177
SO_PASSCRED = 16


def decode_dist_secret(val):
    # XXX distbuild specific
    return six.ensure_str(base64.b64decode(val.strip()).strip())


def get_exe(pid):
    return os.readlink('/proc/{}/exe'.format(pid))


def get_cmdline(pid):
    with open('/proc/{}/cmdline'.format(pid)) as afile:
        return afile.read()


def start_server(mount_point, secrets):
    assert secrets
    assert not library.python.windows.on_win()
    assert os.path.exists(os.path.dirname(mount_point)), mount_point

    if os.path.exists(mount_point):
        os.unlink(mount_point)

    self_exe = get_exe(os.getpid())

    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.bind(mount_point)
    sock.listen(5)

    def server():
        while True:
            conn, _ = sock.accept()
            cred = conn.getsockopt(socket.SOL_SOCKET, SO_PEERCRED, struct.calcsize('3i'))
            pid, uid, gid = struct.unpack('3i', cred)
            logger.debug("New connection to secret server: pid=%d uid=%d gid=%d", pid, uid, gid)

            try:
                exe_path = get_exe(pid)
                if exe_path != self_exe:
                    logger.debug("Refused %s", exe_path)
                    continue

                logger.debug("Handshaked %s (%s)", exe_path, get_cmdline(pid))
                name = conn.recv(1024)
                name = six.ensure_str(name)

                if name == CMD_STOP:
                    return

                data = six.ensure_binary(secrets.get(name, ''))
                conn.send(data)
                logger.debug("Secret '%s' sent", name)
            finally:
                conn.close()

    logger.debug("Starting server with mount point: %s with exe_path=%s", mount_point, self_exe)

    thread = threading.Thread(target=server)
    thread.daemon = True
    thread.start()
    return thread


def stop_server(mount_point):
    get_secret(mount_point, CMD_STOP)


def get_secret(mount_point, name):
    assert not library.python.windows.on_win()

    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, SO_PASSCRED, 1)
    sock.connect(mount_point)

    try:
        data = six.ensure_binary(name)
        sock.send(data)
        secret = sock.recv(1024)
        logger.debug("Received %s bytes", len(secret))
        return six.ensure_str(secret)
    finally:
        sock.close()


def start_test_tool_secret_server(env):
    # We have already swallowed blue pill - case when ya runs run_test to run ya with run_test
    if 'YA_TEST_TOOL_SECRET_POINT' in os.environ:
        logger.debug("Secret point already mounted to %s", os.environ['YA_TEST_TOOL_SECRET_POINT'])
        return

    secrets = {}
    for name in [
        'YA_COMMON_YT_TOKEN',
    ]:
        if name in os.environ:
            secrets[name] = decode_dist_secret(os.environ[name])
            del os.environ[name]
            if name in env:
                del env[name]

    if not secrets:
        logger.debug("No secrets found")
        return

    if library.python.windows.on_win():
        logger.debug("No secret server for windows")
        return

    def get_mount_point():
        for func in [
            lambda: os.path.abspath('cred'),
            lambda: tempfile.NamedTemporaryFile(delete=False).name,
            lambda: '/tmp/{}'.format(exts.uniq_id.gen32()),
        ]:
            path = func()
            if len(path) <= 104 and os.path.exists(os.path.dirname(path)):
                return path

    mount_point = get_mount_point()
    if not mount_point:
        logger.debug("Failed to find suitable mount point for server")

    for dst in [os.environ, env]:
        dst['YA_TEST_TOOL_SECRET_POINT'] = mount_point

    start_server(mount_point, secrets)
