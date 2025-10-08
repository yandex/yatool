import base64
import logging
import os
import subprocess
import sys
import threading

import six

import exts.fs
import exts.func
import exts.yjson as json
import exts.windows

import app_config
import yalibrary.guards as guards
import yalibrary.platform_matcher as pm

import yalibrary.tools as tools
import devtools.ya.core.config
import devtools.ya.core.report
from exts import hashing

logger = logging.getLogger(__name__)


class BadCompilerException(Exception):
    mute = True


class FailedGenerationScript(Exception):
    mute = True
    retriable = False


@exts.func.memoize()
def resolve_system_cxx(cxx_compiler, host, target, c_compiler=None):
    params = {'type': 'system_cxx'}
    if cxx_compiler is not None:
        params['cxx_compiler'] = cxx_compiler
    if c_compiler is not None:
        params['c_compiler'] = c_compiler

    return {
        'platform': {
            'host': pm.parse_platform(host),
            'target': pm.parse_platform(target),
        },
        'params': params,
        'name': 'system_cxx',
    }


def is_local(tc):
    try:
        return tc['params']['local']
    except KeyError:
        return False


def is_system(tc):
    try:
        return tc['params']['type'] == 'system_cxx'
    except KeyError:
        return False


def _resolve_cxx(host, target, c_compiler, cxx_compiler, ignore_mismatched_xcode_version=False):
    if c_compiler or cxx_compiler:
        res = resolve_system_cxx(cxx_compiler, host, target, c_compiler)
    elif devtools.ya.core.config.has_mapping() and exts.windows.on_win():
        # XXX for small ya, TODO move to proper place
        res = resolve_system_cxx("cl.exe", host, target)
    else:
        res = tools.resolve_tool('c++', host, target)
    if is_local(res):

        def get_output(*args):
            return subprocess.check_output(tuple(args), text=True).strip()

        version = get_output('xcodebuild', '-version').split()[1]
        expect_version = res['params']['version']
        if version != expect_version:
            if ignore_mismatched_xcode_version:
                logger.warning(
                    'You are using -DIGNORE_MISMATCHED_XCODE_VERSION. Successful compilation and launch is not guaranteed.'
                )
            else:
                logger.warning('Unsupported version of Xcode installed. To install supported version:')
                logger.warning('1. Download supported version one of the ways below:')
                logger.warning('   - Download supported version from https://xcodereleases.com/.')
                logger.warning(
                    '   - Download from sandbox https://sandbox.yandex-team.ru/resources?type=XCODE_ARCHIVE&limit=20&offset=0&attrs={{"version":"{version}"}}.'.format(
                        version=expect_version
                    )
                )
                logger.warning(
                    '2. Select supported version with `sudo xcode-select -s <path-to-xcode-dir>/Contents/Developer`'
                )
                logger.warning(
                    '\nOr you can add -DIGNORE_MISMATCHED_XCODE_VERSION for avoid this exception. Do this at your own risk.'
                )
                raise Exception('Unsupported Xcode version, installed = {}'.format(version))

        parsed_oses = [
            pm.parse_platform(host)['os'],
            pm.parse_platform(target)['os'],
        ]

        res['params']['c_compiler'] = get_output('xcrun', '--find', 'clang')
        res['params']['cxx_compiler'] = get_output('xcrun', '--find', 'clang++')
        res['params']['actool'] = get_output('xcrun', '--find', 'actool')
        res['params']['ibtool'] = get_output('xcrun', '--find', 'ibtool')
        res['params']['simctl'] = get_output('xcrun', '--find', 'simctl')
        res['params']['profiles'] = os.path.join(
            get_output('xcrun', '-sdk', 'iphoneos', '--show-sdk-platform-path'),
            'Library/Developer/CoreSimulator/Profiles',
        )
        res['params']['ar'] = get_output('xcrun', '--find', 'libtool')
        res['params']['strip'] = get_output('xcrun', '--find', 'strip')
        dwarf_tool = get_output('xcrun', '--find', 'dsymutil') + ' -flat'
        res['params']['dwarf_tool'] = {parsed_os: dwarf_tool for parsed_os in parsed_oses}
    # res is referencing to the cached dict
    return res.copy()


def host_platform_name():
    return pm.stringize_platform(pm.current_toolchain())


def mine_platform_name(s):
    aliases = {k.upper(): v for k, v in devtools.ya.core.config.config().get('toolchain_aliases', {}).items()}
    if s.upper() in aliases:
        platform_name = aliases[s.upper()].upper()
        logger.debug('Platform %s is alias for %s', s, platform_name)
    elif pm.prevalidate_platform(s):
        platform_name = s.upper()
    else:
        platform_name = pm.guess_platform(s)
        logger.debug(
            'Loose platform notation not following TOOLCHAIN-OS-ARCH scheme: %s, guessed platform: %s', s, platform_name
        )

    return platform_name


def host_for_target_platform_name(host_name, target_name):
    parsed_host = pm.parse_platform(host_name)
    parsed_target = pm.parse_platform(target_name)
    return pm.stringize_platform(
        {'toolchain': parsed_target['toolchain'], 'os': parsed_host['os'], 'arch': parsed_host['arch']}
    )


def gen_tc(platform_name, c_compiler=None, cxx_compiler=None, ignore_mismatched_xcode_version=False):
    return _resolve_cxx(platform_name, platform_name, c_compiler, cxx_compiler, ignore_mismatched_xcode_version)


def gen_cross_tc(host_name, target_name, c_compiler=None, cxx_compiler=None, ignore_mismatched_xcode_version=False):
    return _resolve_cxx(
        host_for_target_platform_name(host_name, target_name),
        target_name,
        c_compiler,
        cxx_compiler,
        ignore_mismatched_xcode_version,
    )


def gen_host_tc(c_compiler=None, cxx_compiler=None):
    return gen_tc(host_platform_name(), c_compiler, cxx_compiler)


def gen_specific_tc(tc_key):
    return tools.get_tool('c++', tc_key)


def gen_specific_tc_for_ide(ide):
    return tools.get_tool_for_ide('c++', ide)


def parse_local_ymake(path):
    local_ymake_content = exts.fs.read_text(path)

    def get_kv():
        res = []

        for line in local_ymake_content.split('\n'):
            if line and not line.startswith('#'):  # line is not commented
                res.append(line)

        return res

    lines = get_kv()
    kv = {}

    for line in lines:
        s = line.split('=', 1)

        if len(s) == 2:
            kv[s[0].strip()] = s[1].strip()
        else:
            logger.warning('From %s: skip line "%s"', path, line)

    return {'content': local_ymake_content, 'lines': lines, 'kv': kv, 'path': path}


def check_local_ymake(local_ymake):
    if os.path.exists(local_ymake):
        parsed = parse_local_ymake(local_ymake)

        kv = parsed['kv']
        lines = parsed['lines']
        local_ymake_content = parsed['content']

        if 'SANITIZER_TYPE' in kv:
            logger.warning(
                'Unsupported flag SANITIZER_TYPE in %s. Will skip it. Use --sanitize %s instead',
                local_ymake,
                kv['SANITIZER_TYPE'],
            )

            del kv['SANITIZER_TYPE']
        else:
            logger.info('Using local.ymake: %s with %s', local_ymake, ', '.join(lines))

        devtools.ya.core.report.telemetry.report(
            devtools.ya.core.report.ReportTypes.LOCAL_YMAKE,
            {
                'path': local_ymake,
                'content': local_ymake_content,
            },
        )
        if app_config.in_house:
            import yalibrary.diagnostics as diag

            if diag.is_active():
                diag.save('local.ymake', path=local_ymake, content=local_ymake_content)

        return parsed

    return {}


def detect_conf_root(arc_root, bld_root):
    return (
        os.path.join(bld_root, 'confs')
        if bld_root
        else os.path.normpath(os.path.join(arc_root, '..', 'ybuild', 'confs'))
    )  # XXX: fixme


CONF_DEBUG_OUTPUT_LOCK = threading.Lock()


@guards.guarded(guards.GuardTypes.FETCH)
def gen_conf(
    arc_dir,
    conf_dir,
    build_type,
    use_local_conf,
    local_conf_path,
    extra_flags,  # type: dict
    tool_chain,
    local_distbuild=False,
    conf_debug=None,
    debug_id=None,
    extra_conf=None,
):
    # do not even dare to download stuff from this function
    build_path = os.path.join(arc_dir, ymake_build_dir())
    build_internal_path = os.path.join(arc_dir, ymake_build_internal_dir())

    script = os.path.join(build_path, 'ymake_conf.py')
    custom_script = os.path.join(build_internal_path, 'custom_conf.py')
    core_conf = os.path.join(build_path, 'ymake.core.conf')

    def iter_dir_files_recursively(dirpath, check=None, skip_tests=False):
        for root, subdirs, files in os.walk(dirpath, topdown=True):
            for name in sorted(files):
                if skip_tests and 'tests' in subdirs:
                    subdirs.remove('tests')
                if check is None or check(name):
                    yield os.path.join(root, name)

    def iter_local_ymakes():
        if use_local_conf:
            if local_conf_path:
                yield local_conf_path
            else:
                for i in [os.path.join(arc_dir, 'local.ymake')]:
                    if os.path.exists(i):
                        yield i

    def iter_plugins():
        for p_dir in (os.path.join(prefix, 'plugins') for prefix in (build_path, build_internal_path)):
            if not os.path.isdir(p_dir):
                continue
            yield from iter_dir_files_recursively(
                p_dir, skip_tests=True, check=lambda x: x[0] not in '~#.' and x.endswith('.py')
            )

    def iter_conf_parts():
        for conf_dir in (os.path.join(build_path, d) for d in ('conf', 'internal/conf', 'ymake.parts')):
            if not os.path.isdir(conf_dir):
                continue
            yield from iter_dir_files_recursively(
                conf_dir, skip_tests=True, check=lambda x: x[0] not in '~#.' and x.endswith('.conf')
            )

    def iter_conf_files():
        yield script
        if os.path.exists(custom_script):
            yield custom_script
        yield core_conf

        yield from iter_conf_parts()

        yield from iter_plugins()

        yield from iter_local_ymakes()

        if extra_conf is not None:
            yield extra_conf

    def uniq_conf(data, name_parts):
        digest = '-'.join(name_parts) + '-' + hashing.md5_value(str(data))

        return os.path.join(conf_dir, digest, 'ymake.conf'), digest

    def subset(dct, white_list):
        return {k: v for k, v in dct.items() if k in white_list}

    data = [
        "14",
        json.dumps(tool_chain, sort_keys=True),
        json.dumps(extra_flags, sort_keys=True),
        build_type,
        arc_dir,
    ] + [exts.fs.read_text(x) for x in sorted(iter_conf_files())]
    first_conf, _ = uniq_conf(data, [build_type])

    conf_debug = conf_debug or {}
    debug_id = debug_id or 'unknown'
    print_commands = 'print-commands' in conf_debug
    force_run = 'force-run' in conf_debug
    list_files = 'list-files' in conf_debug

    has_first_conf = os.path.isfile(first_conf)
    if not has_first_conf or print_commands or force_run:
        exts.fs.create_dirs(os.path.dirname(first_conf))

        env = subset(
            os.environ,
            [
                'PATH',
                'PATHEXT',
                'SYSTEMROOT',
                'USERPROFILE',
                'VCINSTALLDIR',
                'VCTOOLSINSTALLDIR',
                'WINDOWSSDKDIR',
                'WINDOWSSDKVERSION',
            ],
        )  # XXX: use clear env!
        env['Y_PYTHON_ENTRY_POINT'] = ':main'

        def iter_all_flags():
            for local_ymake in iter_local_ymakes():
                yield from sorted(check_local_ymake(local_ymake).get('kv', {}).items())

            if extra_flags:
                yield from sorted(extra_flags.items())

        def iter_flags():
            for k, v in iter_all_flags():
                yield '-D'
                yield k + '=' + v

        tc_params = six.ensure_str(base64.b64encode(six.ensure_binary(json.dumps(tool_chain, sort_keys=True))))
        conf_params = ['-l'] if local_distbuild else []
        cmd = (
            [
                sys.executable,
                '-B',
                script,
                arc_dir,
                build_type,
                'no' if 'verbose-run' not in conf_debug else 'verbose',
                '--toolchain-params',
                tc_params,
            ]
            + conf_params
            + [x for x in iter_flags()]
        )

        if not has_first_conf or force_run:
            logger.debug('Generating conf into %s with cmd %s and env %s', first_conf, cmd, str(env))

            import subprocess as sp

            p = sp.Popen(cmd, env=env, stdout=sp.PIPE, stderr=sp.PIPE)
            out, err = p.communicate()
            rc = p.wait()

            err = six.ensure_str(err)

            if rc != 0:
                raise FailedGenerationScript('Config was not generated due to errors in {}\n{}'.format(script, err))

            if err:
                logger.warning('Non-empty stderr from ymake_conf.py:\n%s', err)

            if extra_conf is not None:
                out += exts.fs.read_file(extra_conf)
            exts.fs.write_file(first_conf, out)

        if print_commands:
            with CONF_DEBUG_OUTPUT_LOCK:
                sys.stdout.write(
                    '{id} {env} {cmd} > {path}\n'.format(
                        id=debug_id,
                        env=' '.join('{}={}'.format(k, env[k]) for k in sorted(env.keys())),
                        cmd=' '.join(cmd),
                        path=first_conf,
                    )
                )
                sys.stdout.flush()

    first_conf_data = exts.fs.read_file(first_conf)
    second_conf, second_conf_digest = uniq_conf([first_conf_data] + data, [build_type, 'x'])

    if not os.path.isfile(second_conf):
        exts.fs.create_dirs(os.path.dirname(second_conf))
        logger.debug('Copy conf %s to %s', first_conf, second_conf)
        exts.fs.write_file(second_conf, first_conf_data)

    if app_config.in_house:
        import yalibrary.diagnostics as diag

        if diag.is_active():
            diag.save('conf', value=exts.fs.read_file(second_conf))

    if list_files:
        with CONF_DEBUG_OUTPUT_LOCK:
            sys.stdout.write('{}\t{}\n'.format(debug_id, first_conf))
            sys.stdout.flush()

    return second_conf, second_conf_digest


@exts.func.lazy
def ymake_build_dir():
    return os.path.join('build')


@exts.func.lazy
def ymake_build_internal_dir():
    return os.path.join(ymake_build_dir(), 'internal')
