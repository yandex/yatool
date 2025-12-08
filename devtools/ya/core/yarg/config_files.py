import typing as tp

import logging
import os
import six
import platform

import app_config
import devtools.ya.core.config
import toml

from devtools.ya.core.yarg.consumers import ConfigConsumer, get_consumer

logger = logging.getLogger(__name__)

if tp.TYPE_CHECKING:
    from devtools.ya.core.yarg.options import Options  # noqa: F401
    from devtools.ya.core.yarg.consumers import Consumer  # noqa: F401


def encode_config(config_data):
    for param, value in six.iteritems(config_data):
        if isinstance(value, six.string_types):
            value = six.ensure_str(config_data[param])

        yield param, value


def load_config_by_file(config_file):
    # type: (str) -> tp.Optional[dict]

    if not os.path.isfile(config_file):
        return None

    try:
        logger.debug("Load data from config %s", config_file)

        with open(config_file) as file:
            data = toml.load(file)

        logger.debug("Found %d keys", len(data))

        data = dict(encode_config(data))

        return data
    except Exception as e:
        logger.warning("Failed to load %s: %s", config_file, str(e))

    return None


def load_config(files):
    # type: (tp.Sequence[str]) -> dict

    config = {}

    for file in files:
        data = load_config_by_file(file)

        if data is not None:
            config.update(data)

    return config


def apply_config(opt, consumer, *configs):
    # type: ("Options", "Consumer", dict) -> None
    consumer = consumer or get_consumer(opt)

    config_consumers = {entry.name: entry for entry in consumer.parts if isinstance(entry, ConfigConsumer)}

    for plane in configs:
        for config_name, config_value in six.iteritems(plane):
            if config_name not in config_consumers:
                # TODO: Check prefixes
                # TODO: Show line with error?
                if not isinstance(config_value, (list, dict, tuple)):
                    logger.debug("Unknown config parameter `%s`", config_name)
                continue

            entry = config_consumers[config_name]
            entry.consume_config_value(opt, config_value)


def get_config_files(cmd_name=None, user_config=True, global_config=True):
    config_files = []
    if 'YA_CONFIG_PATH' in os.environ:
        path = os.environ['YA_CONFIG_PATH']
        if os.path.isabs(path):
            return [path]
        return [os.path.join(devtools.ya.core.config.find_root(), path)]

    if 'YA_TEST_CONFIG_PATH' in os.environ:
        config_files.append(os.environ['YA_TEST_CONFIG_PATH'])

    if devtools.ya.core.config.find_root(fail_on_error=False):
        repository_root = devtools.ya.core.config.find_root()
        user = devtools.ya.core.config.get_user()
        system = platform.system().lower()
        # Respect XDG directory specification, as defined in
        # https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html
        #
        # Human-readable explanation can be found in ArchWiki:
        # https://wiki.archlinux.org/title/XDG_Base_Directory
        #
        # NB:
        #   this code intentionally does not takes ${XDG_CONFIG_HOME} into account,
        #   as nobody needs this override at the time
        xdg_config_home = os.path.join(devtools.ya.core.config.home_dir(), ".config")

        if global_config:
            filename = "ya.conf"
            if app_config.in_house:
                config_files.append(os.path.join(repository_root, filename))
            else:
                extra_conf_path = devtools.ya.core.config.extra_conf_dir()
                assert extra_conf_path, 'extra_conf_root parameter must be specified for non "in_house" ya'
                public_conf_path = os.path.join(repository_root, extra_conf_path, filename)
                if os.path.exists(public_conf_path):
                    config_files.append(public_conf_path)
                elif devtools.ya.core.config.is_test_mode():
                    raise AssertionError(
                        "opensource ya didn't find opensource ya.conf: {} is missing".format(public_conf_path)
                    )
                else:
                    config_files.append(os.path.join(repository_root, filename))
                    # Project specific config
                    config_files.append(os.path.join(repository_root, "build", "internal", filename))

        if user_config and 'sandbox' not in user and 'teamcity' not in user:
            files = [
                "ya.conf",
                f"ya.{system}.conf",
            ]
            if cmd_name:
                files.append(f"ya.{cmd_name}.conf")
                files.append(f"ya.{cmd_name}.{system}.conf")

            dirs = []
            # Don't try load user's config from junk using opensource ya
            if app_config.in_house:
                dirs.append(devtools.ya.core.config.junk_path(repository_root))

            dirs += [
                repository_root,
                os.path.dirname(repository_root),
                devtools.ya.core.config.misc_root(),
                xdg_config_home,
            ]

            for filename in files:
                config_files += [os.path.join(x, filename) for x in dirs]
    else:
        logger.debug("Failed to find config files")
    return config_files
