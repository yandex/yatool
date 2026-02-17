import logging
import os
import sys
import threading
import socket
import six.moves.queue as Queue
import time


import exts.fs
import exts.archive
import devtools.ya.test.system.process as process

from library.python.port_manager import PortManager
from yatest.common.process import InvalidExecutionStateError

logger = logging.getLogger(__name__)

ADB_PATH = 'platform-tools/adb'
EMULATOR_PATH = 'emulator/emulator'


class RetryableException(Exception):
    pass


class AndroidEmulator(object):
    def __init__(self, build_root, sdk_root, avd_root, arch):
        self.build_root = build_root
        self.sdk_root = sdk_root
        self.avd_root = avd_root
        self.arch = arch
        self.adb_path = os.path.join(self.sdk_root, 'android_sdk', ADB_PATH)
        self.running_devices = {}
        self.port_manager = PortManager()
        self.adb_port = self.port_manager.get_port()
        self.check_marker_script = 'check_end_marker.sh'

        self._prepare_env()

    def _prepare_env(self):
        env = os.environ.copy()
        env['HOME'] = self.build_root
        env['ANDROID_SDK_HOME'] = os.path.join(self.build_root, '.android')
        env['ANDROID_AVD_HOME'] = os.path.join(self.build_root, '.android', 'avd')
        env['ANDROID_ADB_SERVER_PORT'] = str(self.adb_port)
        # env['ANDROID_SDK_ROOT'] = self.sdk_root
        self.env = env

    def _prepare_device(self, device_name):
        avd_dir = os.path.join(self.build_root, '.android', 'avd')
        destination_avd = os.path.join(avd_dir, device_name + '.avd')
        destination_ini = os.path.join(avd_dir, device_name + '.ini')
        source_avd = os.path.join(self.avd_root, 'avd', 'template-' + self.arch + '.avd')
        source_ini = os.path.join(self.avd_root, 'avd', 'template.ini')
        exts.fs.ensure_dir(avd_dir)
        assert not os.path.exists(destination_avd)
        exts.fs.copytree3(source_avd, destination_avd)
        with open(destination_ini, 'w') as dest_ini:
            with open(source_ini) as src_ini:
                content = src_ini.read().format(build_root=self.build_root, device_name=device_name + '.avd')
            dest_ini.write(content)

    def run_cmd(self, device_id, cmd):
        return process.execute(self._get_adb_cmd(device_id) + cmd, check_exit_code=True, env=self.env)

    def chmod(self, device_id, path, permissions, recursive=False):
        self.run_cmd(
            device_id, ['shell', 'su root chmod {}{} {}'.format('-R ' if recursive else '', permissions, path)]
        )

    def get_temp_dir(self, app_name):
        return '/data/data/{}/'.format(app_name)

    def boot_device(self, device_name):
        if device_name in self.running_devices:
            return
        self._prepare_device(device_name)
        process.execute(self._get_adb_cmd() + ['start-server'], check_exit_code=True, env=self.env)
        re_port = self.port_manager.get_port()
        que = Queue.Queue()
        t = threading.Thread(target=lambda q, sname: q.put(get_port_from_socket(sname)), args=(que, re_port))
        t.start()
        p = process.execute(
            [
                os.path.join(self.sdk_root, 'android_sdk', EMULATOR_PATH),
                '@' + device_name,
                '-no-window',
                '-no-audio',
                '-no-skin',
                '-report-console',
                'tcp:{}'.format(str(re_port)),
            ],
            check_exit_code=True,
            env=self.env,
            wait=False,
        )
        self.running_devices[device_name] = {'proc': p}
        t.join()
        port = que.get() if not que.empty() else None
        if not port:
            raise Exception("Can't detect emulator port")
        port = port[0]
        device_id = 'emulator-' + port.decode()
        self.running_devices[device_name]['device_id'] = device_id

        def get_emulator_state():
            return process.execute(
                self._get_adb_cmd(device_id) + ['get-state'],
                check_exit_code=False,
                env=self.env,
            )

        def get_running_devices():
            return process.execute(
                self._get_adb_cmd() + ['devices'],
                check_exit_code=False,
                env=self.env,
            )

        emulator_state = get_emulator_state()
        while emulator_state.stdout != 'device\n':
            logger.info('Emulator state stdout: {}'.format(emulator_state.stdout))
            logger.info('Emulator state stderr: {}'.format(emulator_state.stderr))
            time.sleep(1)
            if emulator_state.stderr == "error: device '{}' not found\n".format(device_id):
                devices = get_running_devices()
                logger.info('Running devices stdout: {}'.format(devices.stdout))
                logger.info('Running devices stderr: {}'.format(devices.stderr))
            emulator_state = get_emulator_state()

        # wait while device fully booted
        def is_device_fully_booted():
            return (
                process.execute(
                    self._get_adb_cmd() + ['shell', 'getprop', 'sys.boot_completed'],
                    check_exit_code=False,
                    env=self.env,
                ).stdout
                == '1\n'
            )

        while not is_device_fully_booted():
            time.sleep(1)

    def install_app(self, device_name, app_path, app_name):
        device_id = self._get_device_id(device_name)
        process.execute(self._get_adb_cmd(device_id) + ['uninstall', app_name], check_exit_code=False, env=self.env)

        def install_apk():
            return process.execute(
                self._get_adb_cmd(device_id) + ['install', app_path],
                check_exit_code=False,
                env=self.env,
            )

        result = install_apk()
        while result.stdout != 'Performing Streamed Install\nSuccess\n':
            logger.info('Install apk stdout: {}'.format(result.stdout))
            logger.info('Install apk stderr: {}'.format(result.stderr))
            time.sleep(5)
            result = install_apk()

    def push_check_marker_script(self, device_id, app_name, end_marker):
        with open(self.check_marker_script, 'w') as check_script:
            check_script.write('while [[ ! -f {} ]]; do sleep 1; done;'.format(end_marker))

        push_dir = '/sdcard/'
        self.run_cmd(device_id, ['push', self.check_marker_script, push_dir + self.check_marker_script])

        self.run_cmd(
            device_id,
            [
                'shell',
                'su',
                'root',
                'cp',
                push_dir + self.check_marker_script,
                self.get_temp_dir(app_name) + self.check_marker_script,
            ],
        )
        self.chmod(device_id, self.get_temp_dir(app_name) + self.check_marker_script, '777')

    def run_test(self, device_name, entry_point, app_name, end_marker, args):
        device_id = self._get_device_id(device_name)
        try:
            process.execute(
                self._get_adb_cmd(device_id) + ['shell', 'logcat', '-c'], check_exit_code=True, env=self.env, timeout=10
            )
        except process.TimeoutError:
            pass
        run_args = self._get_adb_cmd(device_id) + ['shell', 'am', 'start']
        i = 0
        while i < len(args):
            if args[i].find('=') == -1:
                run_args += ['--es', args[i], args[i + 1]]
                i += 2
            else:
                run_args += ['--ei', args[i], '0']
                i += 1
        run_args += [entry_point]
        process.execute(run_args, check_exit_code=True, env=self.env)
        self.push_check_marker_script(device_id, app_name, end_marker)
        self.run_cmd(
            device_id,
            ['shell', 'run-as', app_name, 'sh', '{}{}'.format(self.get_temp_dir(app_name), self.check_marker_script)],
        )

    def run_list(self, device_name, entry_point, app_name, end_marker, args):
        device_id = self._get_device_id(device_name)
        try:
            process.execute(
                self._get_adb_cmd(device_id) + ['shell', 'logcat', '-c'], check_exit_code=True, env=self.env, timeout=10
            )
        except process.TimeoutError:
            pass
        run_args = self._get_adb_cmd(device_id) + ['shell', 'am', 'start']
        for arg in args:
            run_args += ['--ei', arg, '0']
        run_args += [entry_point]
        process.execute(
            run_args,
            check_exit_code=True,
            env=self.env,
        )
        self.push_check_marker_script(device_id, app_name, end_marker)
        self.run_cmd(
            device_id,
            ['shell', 'run-as', app_name, 'sh', '{}{}'.format(self.get_temp_dir(app_name), self.check_marker_script)],
        )
        res = self.run_cmd(device_id, ['shell', 'run-as', app_name, 'cat', end_marker])
        return res

    def extract_result(self, device_name, app_name, reports_to_extract):
        device_id = self._get_device_id(device_name)
        try:
            process.execute(
                self._get_adb_cmd(device_id) + ['shell', 'logcat', 'unit_tests:I', '*:S', '-d'],
                stdout=sys.stdout,
                stderr=sys.stderr,
                env=self.env,
                timeout=10,
            )
        except process.TimeoutError:
            pass

        self.chmod(device_id, self.get_temp_dir(app_name), '777', recursive=True)

        for src, dst in reports_to_extract:
            temp_name = '/sdcard/' + os.path.basename(src)
            try:
                process.execute(
                    self._get_adb_cmd(device_id) + ['shell', 'su', 'root', 'cp', src, temp_name],
                    check_exit_code=True,
                    env=self.env,
                    timeout=10,
                )
            except Exception as e:
                raise RetryableException("Can't copy file:\n" + str(e))
            for _ in range(3):
                try:
                    self.run_cmd(device_id, ['pull', temp_name, dst])
                    if os.path.exists(dst):
                        break
                except Exception as e:
                    raise RetryableException(e)
            else:
                raise RetryableException("Can't extract {} from avd. See logs for details".format(dst))
            if not os.path.getsize(dst):
                raise RetryableException("{} from avd is empty. See logs for details".format(dst))

    def _get_device_id(self, device_name):
        assert device_name in self.running_devices
        return self.running_devices[device_name]['device_id']

    def _get_adb_cmd(self, device_id=None):
        return [
            self.adb_path,
        ] + (['-s', device_id] if device_id else [])

    def cleanup(self):
        for p in self.running_devices.values():
            try:
                p['proc'].kill()
            except InvalidExecutionStateError:
                pass  # procces allready stopped, do nothing
        process.execute(self._get_adb_cmd() + ['kill-server'], check_exit_code=True, env=self.env)
        self.port_manager.release()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.cleanup()


def get_port_from_socket(re_port):
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.settimeout(20.0)
    server.bind(('127.0.0.1', re_port))
    try:
        server.listen(1)
        conn, addr = server.accept()
        data = conn.recv(1024)
        if data:
            return data.strip().split()
    except socket.timeout:
        return None
