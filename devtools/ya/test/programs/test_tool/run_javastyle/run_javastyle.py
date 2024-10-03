import argparse
import os
import sys
import re
import collections
import logging
import time
import datetime
import getpass
import six

from six.moves import urllib as urllib2
import exts.fs
import devtools.ya.test.common
from devtools.ya.test import facility
import devtools.ya.test.system.process
import devtools.ya.test.test_types.common
import devtools.ya.test.filter as test_filter

from devtools.ya.test.programs.test_tool.lib.migrations_config import load_yaml_config, MigrationsConfig

TEST_TYPE = 'jstyle'

logger = logging.getLogger(__name__)


def setup_logging(verbose):
    level = logging.DEBUG if verbose else logging.ERROR
    logging.basicConfig(level=level, stream=sys.stdout, format="%(asctime)s: %(levelname)s: %(message)s")


def read_jbuild_test_cases(source_root, idx_file, srcdir):
    test_cases = {}
    with open(idx_file) as idx_file:
        for cls_line in idx_file:
            for cls in [
                os.path.normpath(os.path.join(srcdir, i)) for i in cls_line.strip().split(' ') if i.endswith('.java')
            ]:
                test_cases[os.path.relpath(cls, source_root)] = os.path.normpath(cls)
    return test_cases


def read_test_cases(source_root, index_file):
    splited = index_file.split('::')
    if len(splited) > 1:
        return read_jbuild_test_cases(source_root, splited[0], splited[1])

    test_cases = {}
    with open(index_file) as idx_file:
        for cls_line in idx_file:
            for cls in [i for i in cls_line.split(' ') if i.endswith('.java') and i.startswith(source_root)]:
                test_cases[os.path.relpath(cls, source_root)] = os.path.normpath(cls)
    return test_cases


def wait_for_server_startup(proc, log, timeout):
    start = time.time()
    deadline = start + timeout
    # https://a.yandex-team.ru/arcadia/devtools/jstyle-runner/java/src/ru/yandex/devtools/JStyleRunnerServer.java?rev=r9523589#L149
    target = 'Server is started:'
    # https://a.yandex-team.ru/arcadia/devtools/jstyle-runner/java/src/ru/yandex/devtools/JStyleRunnerServer.java?rev=r9747798#L131
    file_locked_exit_code = 2
    data = ''
    stream = open(log)

    try:
        while time.time() < deadline:
            block = stream.read(1024)
            if block:
                data += block
                if target in data:
                    logger.debug("Startup duration: {:.3f}s".format(time.time() - start))
                    return
            else:
                if proc.running:
                    time.sleep(0.02)
                elif proc.returncode == file_locked_exit_code:
                    logger.debug("Lock is acquired by another process")
                    return
                else:
                    raise Exception(
                        "jstyle server failed with {} exit code, see logs for more info".format(proc.returncode)
                    )

        raise Exception("Failed to start jstyle server in {} seconds".format(timeout))

    except Exception:
        stream.close()
        proc.terminate()
        raise


def execute(java_cmd, config_file, input_file, lock_file, logs_dir):
    logger.debug("Using lock file: %s", lock_file)
    repeat = 0

    def get_log(ext):
        filename = os.path.join(logs_dir, 'jstyle_server.' + ext)
        open(filename, 'a').close()
        return filename

    # Create logs before calling wait_for_server_startup() to avoid race
    stdout_log = get_log('out')
    stderr_log = get_log('err')

    while True:
        try:
            with open(lock_file) as f:
                port = int(f.read())

            url = 'http://localhost:' + str(port) + '/'
            logger.debug("Connecting to port: %s, %s", port, url)

            start = datetime.datetime.now()

            request = urllib2.request.Request(
                url, headers={'Method': 'GET', 'Config-file': config_file, 'Input-file': input_file}
            )
            response = urllib2.request.urlopen(request)
            headers = response.info()
            exit_code = int(headers['Ret-code'])
            content = six.ensure_str(response.read())
            if 'Internal-error' in headers:
                std_err = content
                std_out = ''
            else:
                std_err = ''
                std_out = content

            elapsed = (datetime.datetime.now() - start).total_seconds()

            class Response:  # Just like a normal response
                def __init__(self):
                    self.exit_code = exit_code
                    self.std_err = std_err
                    self.std_out = std_out
                    self.elapsed = elapsed

            return Response()
        except urllib2.error.HTTPError as e:
            logger.error("%s: %s", e.code, e.read())
            raise
        except Exception as e:
            repeat += 1
            if repeat >= 10:
                raise
            logger.debug("Can't connect to jstyle-server: %s", e)
            proc = devtools.ya.test.system.process.execute(
                java_cmd,
                check_exit_code=False,
                wait=False,
                stdout=stdout_log,
                stderr=stderr_log,
            )
            wait_for_server_startup(proc, stdout_log, 20)


def get_lock_dir():
    dir = os.getenv('YA_CORE_TMP_PATH')
    if dir:
        dir = os.path.join(dir, 'jstyle-server')
        try:
            os.makedirs(dir)
        except OSError:
            None
    else:
        dir = '/var/tmp'
    if not os.path.exists(dir):
        logger.warning("Dir %s not found, use legacy mode", dir)
        return None
    return dir


def get_lock_file(runner_lib_path):
    username = getpass.getuser()
    lnk_file = os.path.join(runner_lib_path, 'lnk')

    prefix = 'jstyle-server-'
    suffix = '.port'
    if os.path.exists(lnk_file):
        with open(lnk_file) as f:
            return prefix + f.read().replace(':', '-') + '-' + username + suffix
    else:
        return prefix + username + suffix


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('index_files', nargs='*')
    parser.add_argument('--java')
    parser.add_argument('--runner-lib-path')
    parser.add_argument('--config-xml')
    parser.add_argument('--tests-filters', required=False, action="append")
    parser.add_argument('--source-root')
    parser.add_argument('--trace-path', help="Path to the output trace log")
    parser.add_argument('--out-path', help="Path to the output test_cases")
    parser.add_argument('--list', action="store_true", help="List of tests", default=False)
    parser.add_argument('--verbose', action='store_true', default=False)
    parser.add_argument("--modulo", default=1, type=int)
    parser.add_argument("--modulo-index", default=0, type=int)
    parser.add_argument("--jstyle-migrations", help="Path to jstyle migrations.yaml", default=None)
    parser.add_argument("--use-jstyle-server", action="store_true", default=False)
    args = parser.parse_args()
    args.source_root = os.path.normpath(args.source_root)

    jstyle_migrations_path = os.environ.get("_YA_TEST_JSTYLE_CONFIG", args.jstyle_migrations)

    if jstyle_migrations_path is None:
        config_migrations = MigrationsConfig(section="jstyle")
    else:
        logger.debug("Loading jstyle migrations: %s", jstyle_migrations_path)
        migrations = load_yaml_config(jstyle_migrations_path)
        logger.debug("Building migration config")
        config_migrations = MigrationsConfig(migrations, section="jstyle")

    test_cases = {}
    for idx_file in args.index_files:
        test_cases.update(read_test_cases(args.source_root, idx_file))
    if test_cases and args.tests_filters:
        filter_func = test_filter.make_testname_filter(args.tests_filters)
        test_cases = {tc: test_cases[tc] for tc in test_cases if filter_func('{}::{}'.format(tc, TEST_TYPE))}
    if args.modulo > 1:
        test_cases = {tc: test_cases[tc] for tc in sorted(test_cases.keys())[args.modulo_index :: args.modulo]}
    if args.list:
        sys.stdout.write(os.linesep.join(sorted(test_cases.keys())))
        return 0
    setup_logging(args.verbose)
    logger.debug('Total cases:\n' + '\n'.join(test_cases.values()))

    suite = devtools.ya.test.test_types.common.PerformedTestSuite(None, None, None)
    suite.set_work_dir(os.getcwd())
    suite.register_chunk()

    tests = []
    skipped_files = set()
    while test_cases:
        logs_dir = args.out_path
        checkstyle_input = os.path.join(logs_dir, 'checkstyle.files.list')
        with open(checkstyle_input, 'w') as f:
            f.write('\n'.join(test_cases.values()))
        if os.path.isfile(args.runner_lib_path):
            cp = args.runner_lib_path
        else:
            cp_file_path = os.path.join(args.runner_lib_path, 'cp.txt')
            if os.path.exists(cp_file_path):
                with open(cp_file_path) as f:
                    cp = os.pathsep.join(
                        [(os.path.join(args.runner_lib_path, line.strip())) for line in f if line.strip()]
                    )
            else:
                cp = os.path.join(args.runner_lib_path, '*')

        lock_dir = get_lock_dir()
        if args.use_jstyle_server and lock_dir:
            lock_file = os.path.join(lock_dir, get_lock_file(args.runner_lib_path))
            java_cmd = [args.java, '-cp', cp, 'ru.yandex.devtools.JStyleRunnerServer', '-f', lock_file]
            res = execute(java_cmd, args.config_xml, checkstyle_input, lock_file, logs_dir)
        else:
            java_cmd = [args.java, '-cp', cp, 'ru.yandex.devtools.JStyleRunner', '-c', args.config_xml]
            if args.verbose:
                java_cmd.append('-d')
            java_cmd.append(checkstyle_input)
            res = devtools.ya.test.system.process.execute(java_cmd, check_exit_code=False)

        logger.debug('executed in %s sec', res.elapsed)
        logger.debug('jstyle-runner stdout:\n' + res.std_out or '')
        logger.debug('jstyle-runner stderr:\n' + res.std_err or '')
        if res.exit_code < 0:
            logger.error('%s was terminated by signal: %s', java_cmd[0], res.exit_code)
            return 1
        if res.std_out.find('Starting audit...') == -1:
            logger.error(
                'Something wrong with checkstyle lib( did not find "Starting audit..." in stdout ):\n' + res.std_out
            )
            return 1

        parser = re.compile('^\\[ERROR\\]\\s+' + re.escape(args.source_root + os.path.sep) + '([^:]+):(\\d+:.+)$')
        checkstyle_err_parser = re.compile(
            '^([^:]+)' + re.escape(': Exception was thrown while processing') + '\\s+(.+)$'
        )
        err_dict = collections.defaultdict(list)
        try_again = None
        for line in res.std_out.split('\n'):
            m = parser.match(line)
            if m:
                arcadia_relative_path = m.group(1)
                assert arcadia_relative_path in test_cases

                if config_migrations.is_skipped(arcadia_relative_path):
                    suite.add_chunk_info('Skip file ' + arcadia_relative_path)
                    skipped_files.add(arcadia_relative_path)
                    continue

                exceptions = config_migrations.get_exceptions(os.path.dirname(arcadia_relative_path))
                error = m.group(2).strip()

                skip_check = False

                if exceptions:
                    suite.add_chunk_info(
                        'Ignore {} for file {}'.format(
                            str(list(exceptions)),
                            arcadia_relative_path,
                        )
                    )

                    for exception in exceptions:
                        if error.endswith("[{}]".format(exception)):
                            skip_check = True

                if not skip_check:
                    err_dict[arcadia_relative_path].append('line:' + error)

        for line in res.std_err.split('\n'):
            if try_again:
                try_again['data'].append(line)
                continue
            m = checkstyle_err_parser.match(line)
            if m:
                file_name = m.group(2)
                checkstyle_exception = m.group(1)
                assert file_name in test_cases.values()
                file_name = [i for i in test_cases.keys() if test_cases[i] == file_name][0]
                try_again = {
                    'file': devtools.ya.test.common.get_unique_file_path(
                        logs_dir, "{}.{}.out".format(file_name, TEST_TYPE)
                    ),
                    'data': [line],
                }
                test_case = facility.TestCase(
                    '{}::{}'.format(file_name, TEST_TYPE),
                    devtools.ya.test.const.Status.FAIL,
                    checkstyle_exception + ' was thrown',
                    res.elapsed,
                    logs={'logsdir': logs_dir, 'stdout': try_again['file']},
                )
                tests.append(test_case)
                del test_cases[file_name]

        if try_again:
            tests = []
            exts.fs.write_file(try_again['file'], '\n'.join(try_again['data']), binary=False)
            continue
        for tc in sorted(test_cases):
            logs = {'logsdir': logs_dir}
            snippet = ''
            status = devtools.ya.test.const.Status.GOOD

            if tc in err_dict:
                out_path = devtools.ya.test.common.get_unique_file_path(logs_dir, "{}.{}.out".format(tc, TEST_TYPE))
                exts.fs.write_file(out_path, '\n'.join(err_dict[tc]), binary=False)
                logs['stdout'] = out_path
                snippet = err_dict[tc][:3]
                if len(err_dict[tc]) > 3:
                    snippet.append('...and {} more, see stdout for details'.format(len(err_dict[tc]) - 3))
                snippet = '\n'.join(snippet)
                status = devtools.ya.test.const.Status.FAIL
            if tc in skipped_files:
                snippet = "Skipped by migrations config"
                status = devtools.ya.test.const.Status.SKIPPED

            test_case = facility.TestCase(
                '{}::{}'.format(tc, TEST_TYPE),
                status,
                snippet,
                res.elapsed / len(test_cases),
                logs=logs,
            )
            tests.append(test_case)
        break

    suite.chunk.tests = tests
    suite.generate_trace_file(args.trace_path)
    return 0
