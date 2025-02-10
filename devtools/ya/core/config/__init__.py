import getpass
import exts.yjson as json
import os
import os.path
import sys
import logging

import six

import yalibrary.find_root

import devtools.ya.core.resource
from exts import fs
from exts import func
from exts import strtobool

logger = logging.getLogger(__name__)


def home_dir():
    # When executed in yp pods, $HOME will point to the current snapshot work dir.
    # Temporarily delete os.environ["HOME"] to force reading current home directory from /etc/passwd
    casa_antica = os.environ.pop("HOME", None)
    try:
        casa_moderna = os.path.expanduser("~")
        if os.path.isabs(casa_moderna):
            # This home dir is valid, prefer it over $HOME
            return casa_moderna
        else:
            # When ya-bin is built with musl, only users from /etc/passwd will be properly resolved,
            # as musl does not have nss module for LDAP integration.
            return casa_antica

    finally:
        if casa_antica is not None:
            os.environ["HOME"] = casa_antica


# no caching to make testing possible
def _guess_misc_root():
    return os.getenv('YA_CACHE_DIR') or os.path.join(home_dir(), '.ya')


@func.lazy
def misc_root():
    dir_name = _guess_misc_root()
    return os.path.realpath(fs.create_dirs(dir_name))


@func.lazy
def logs_root():
    logs_root = os.getenv('YA_LOGS_ROOT', None)
    if logs_root is not None:
        return logs_root
    else:
        return os.path.join(misc_root(), 'logs')


@func.lazy
def evlogs_root():
    return os.path.join(misc_root(), 'evlogs')


# no caching to make testing possible
def _guess_tool_root(version):
    dir_name = os.getenv('YA_CACHE_DIR_TOOLS') or os.path.join(_guess_misc_root(), 'tools')
    dir_name = os.path.join(dir_name, 'v' + str(version))
    return dir_name


@func.memoize()
def tool_root(version=None):
    DEFAULT_TOOL_CACHE_VERSION = 3
    dir_name = _guess_tool_root(version or DEFAULT_TOOL_CACHE_VERSION)
    return os.path.realpath(fs.create_dirs(dir_name))


@func.lazy
def build_root():
    return os.path.realpath(fs.create_dirs(os.path.join(misc_root(), 'build')))


def main_file():
    return os.path.abspath(sys.modules['__main__'].__file__)


@func.lazy
def is_developer_ya_version():
    if 'YA_DEV' in os.environ:
        return os.environ['YA_DEV'] == 'yes'
    return False


def ya_path(arc_root, *path):
    return os.path.join(arc_root, *path)


def tmp_path():
    return os.path.join(misc_root(), 'tmp')


@func.lazy
def pycache_path():
    return os.path.join(misc_root(), 'py', 'pycache')


def user_junk_dir(username=None):
    username = username or get_user()
    try:
        import app_config

        return app_config.junk_root.format(username=username)
    except Exception:
        return 'junk/' + username


def junk_path(arc_root, *path):
    return ya_path(arc_root, user_junk_dir(username=get_user()), *path)


def entry_point_path():
    return sys.executable if devtools.ya.core.resource.am_i_binary() else main_file()


class CannotDetermineRootException(Exception):
    pass


class MissingConfigError(Exception):
    mute = True


def find_root(fail_on_error=True):
    # type: (bool) -> str
    if 'YA_SOURCE_ROOT' in os.environ:
        return os.path.abspath(os.environ['YA_SOURCE_ROOT'])
    starts_from_path = os.getcwd() if devtools.ya.core.resource.am_i_binary() else __file__
    root = yalibrary.find_root.detect_root(starts_from_path)
    if fail_on_error and root is None:
        raise CannotDetermineRootException("can't determine root by path {}".format(starts_from_path))
    return root


def find_root_from(targets):
    if targets:
        res = yalibrary.find_root.detect_root(targets[0])
        if res is None:
            raise CannotDetermineRootException("can't determine root by target {}".format(targets[0]))
        return res
    return None


def _get_config_dirs():
    if os.environ.get('YA_TOOLS_CONFIG_PATH', None):
        return [os.environ['YA_TOOLS_CONFIG_PATH']]

    extra_roots = []
    extra_conf_root = extra_conf_dir()
    if extra_conf_root:
        extra_roots.append(os.path.normpath(extra_conf_root))

    return extra_roots + ['build']


def extra_conf_dir():
    import app_config

    return getattr(app_config, 'extra_conf_root', None)


def _get_config(name="ya.conf.json"):
    try:
        root = find_root()
        for path in _get_config_dirs():
            path = os.path.join(root, path, name)
            config = _try_read_from(path)
            if config is not None:
                return config
    except CannotDetermineRootException:
        pass
    if devtools.ya.core.resource.am_i_binary() and name == "ya.conf.json":
        conf_res = devtools.ya.core.resource.try_get_resource('ya.conf.json')
        if conf_res is not None:
            logger.debug('Use conf from resource')
            return json.loads(conf_res)
    else:
        config = _try_read_from(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', name))
        if config is not None:
            return config

    raise MissingConfigError('Cannot find config ' + name)


def _get_config_from_arc_rel_path(path):
    root = find_root(fail_on_error=False)
    if root is not None:
        abs_path = os.path.join(root, path)
        config = _try_read_from(abs_path)
        if config is not None:
            return config
    if devtools.ya.core.resource.am_i_binary():
        conf_res = devtools.ya.core.resource.try_get_resource(path)
        if conf_res is not None:
            logger.debug('Read config "{}" from resource'.format(path))
            return json.loads(conf_res)

    raise MissingConfigError('Cannot find config "{}"'.format(path))


@func.memoize()
def _try_read_from(f):
    if os.path.exists(f):
        with open(f, 'r') as config_file:
            logger.debug('Reading contents of %s', f)
            return json.load(config_file)

    return None


def _add_simple_tool(cfg, name, info):
    cfg['tools'][name] = info
    cfg['toolchain'][name] = {
        "tools": {
            name: {
                "bottle": name,
                "executable": name,
            },
        },
        "platforms": [
            {
                "host": {"os": x},
                "default": True,
            }
            for x in info.get('platforms', ['LINUX', 'DARWIN', 'WIN'])
        ],
    }
    cfg['bottles'][name] = {
        "formula": "build/external_resources/" + info.get('resource', name) + "/resources.json",
        "executable": {
            name: info.get('executable', ['bin', name]),
        },
    }


def _preprocess(cfg):
    if 'simple_tools' in cfg:
        for k, v in cfg['simple_tools'].items():
            _add_simple_tool(cfg, k, v)

    return cfg


@func.lazy
def config():
    return _preprocess(_get_config())


@func.memoize()
def config_from_arc_rel_path(path):
    '''
    This function doesn't try many locations as config() does
    and reads config from specified arcadia root relative path
    '''
    return _get_config_from_arc_rel_path(path)


@func.lazy
def has_mapping():
    try:
        import app_config

        return app_config.has_mapping
    except ImportError:
        return False


@func.lazy
def custom_sandbox_api_url():  # can be removed after code sync with Nebius ends (NDT-277)
    try:
        import app_config

        return os.getenv('YA_SANDBOX_API_URL') or app_config.custom_sandbox_api_url
    except (ImportError, AttributeError):
        return os.getenv('YA_SANDBOX_API_URL')


@func.lazy
def merge_mappings(mappings):
    assert isinstance(mappings, list)
    assert len(mappings) > 0

    res = mappings[0]

    for additional_mapping in mappings[1:]:
        for field in res.keys():
            res[field].update(additional_mapping.get(field, {}))

    return res


@func.lazy
def mapping():
    if has_mapping():
        mappings = [_get_config(name="mapping.conf.json")]
        try:
            ext_mapping_file = os.environ.get('YA_TOOLS_EXT_CONFIG_FILE')
            if ext_mapping_file is not None:
                mappings.append(config_from_arc_rel_path(ext_mapping_file))
            else:
                mappings.append(_get_config(name="ext_mapping.conf.json"))
        except MissingConfigError:
            pass

        return merge_mappings(mappings)


@func.lazy
def is_dev_mode():
    return os.getenv("YMAKE_DEV")


@func.lazy
def get_user():
    if six.PY3:
        import devtools.ya.core.user as core_user

        return core_user.get_user()

    return os.environ.get('YA_USER', getpass.getuser())


@func.lazy
def get_ya_token_path():
    return os.environ.get('YA_TOKEN_PATH', os.path.join(home_dir(), '.ya_token'))


@func.lazy
def get_docker_config_paths():
    return [
        os.path.join(home_dir(), '.docker', 'config.json'),
        os.path.join(home_dir(), '.config', 'containers', 'auth.json'),
    ]


@func.lazy
def get_arc_token_path():
    return os.environ.get('ARC_TOKEN_PATH', os.path.join(home_dir(), '.arc', 'token'))


@func.lazy
def is_test_mode():
    return strtobool.strtobool(os.environ.get('YA_TEST_MODE', '0'))


@func.lazy
def get_arc_token():
    token = os.environ.get('ARC_TOKEN', '')
    token_file = get_arc_token_path()
    if token:
        return token
    elif token_file:
        try:
            with open(token_file, 'r') as f:
                return f.read().strip()
        except OSError:
            logger.debug("Cannot set arc authorization from %s file", token_file)
    else:
        logger.debug("Cannot set arc authorization")
        return None


# runner3 used and build graph is not exported from this ya invocation
def is_self_contained_runner3(opts):
    return not (getattr(opts, 'use_distbuild', True) or getattr(opts, 'save_graph_to', True))
