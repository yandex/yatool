import os
import logging
import re
import shutil
from pathlib import Path
from configparser import ConfigParser
from pyjavaproperties import Properties

from devtools.ya.core import config as core_config
from devtools.ya.build.sem_graph import SemLang, SemConfig, SemException
from devtools.ya.core.config import get_user
from yalibrary import platform_matcher
from exts import hashing

try:
    from yalibrary import oauth
except ImportError:
    # oauth module not exist in Open Source mode
    oauth = None

from devtools.ya.ide.gradle.common import YaIdeGradleException


class _JavaSemConfig(SemConfig):
    """Check and use command line options for configure roots and flags"""

    GRADLE_PROPS = 'gradle.properties'  # Gradle properties filename auto detected by Gradle
    GRADLE_PROPS_FILE: Path = Path.home() / '.gradle' / GRADLE_PROPS  # User Gradle properties file
    BUCKET_USERNAME = 'bucketUsername'
    BUCKET_PASSWORD = 'bucketPassword'
    GRADLE_WRAPPER_USER = 'systemProp.gradle.wrapperUser'
    GRADLE_WRAPPER_PASSWORD = 'systemProp.gradle.wrapperPassword'
    GRADLE_REQUIRED_PROPS: tuple[str] = (
        BUCKET_USERNAME,
        BUCKET_PASSWORD,
        GRADLE_WRAPPER_USER,
        GRADLE_WRAPPER_PASSWORD,
    )
    GRADLE_BUILD_DIR = "gradle.build"
    CONFIG_SIGN = "config.sign"

    EXPORT_ROOT_BASE: Path = Path(core_config.misc_root()) / 'gradle'  # Base folder of all export roots

    _YA_GRADLE_CONFIG = 'ya.gradle.config'
    _YA_IDE_GRADLE = 'ya.ide.gradle'

    def __init__(self, params):
        if platform_matcher.is_windows():
            raise YaIdeGradleException("Windows is not supported in ya ide gradle")
        self.start_cwd: Path = Path().cwd()
        super().__init__(SemLang.JAVA(), params)
        self.logger = logging.getLogger(type(self).__name__)
        if not self.params.remove:
            self._setup_gradle_props()
        self.ya_gradle_config_files: list[str] = []
        self.ya_gradle_config: ConfigParser = None
        self.yexport_toml_checked = False
        self._update_params()
        self.rel_exclude_targets: list[str] = []
        self._check_exclude_targets()
        if not self.yexport_toml_checked:
            self._check_yexport_toml()
        self.sign()

    def _prepare_targets(self) -> None:
        super()._prepare_targets()
        # For use settings_root as hash base for export root must fill it after prepare targets
        self.settings_root: Path = self._get_settings_root()

    def _setup_gradle_props(self) -> None:
        """Setup all required gradle properties"""
        if oauth is None:
            # Do not setup gradle properties in Open Source mode
            return

        errors = []
        if _JavaSemConfig.GRADLE_PROPS_FILE.exists() and _JavaSemConfig.GRADLE_PROPS_FILE.is_dir():
            raise YaIdeGradleException(
                f'Now Gradle properties file {_JavaSemConfig.GRADLE_PROPS_FILE} is invalid: is directory'
            )

        if not _JavaSemConfig.GRADLE_PROPS_FILE.exists():
            logging.info(f'Gradle properties file {_JavaSemConfig.GRADLE_PROPS_FILE} not found')
            logging.info('Try setup automatically')
            with _JavaSemConfig.GRADLE_PROPS_FILE.open('w') as f:
                f.write('')  # create empty file

        gradle_props = Properties()
        with _JavaSemConfig.GRADLE_PROPS_FILE.open() as f:
            gradle_props.load(f)

        user = get_user()
        updated_fields = []
        if gradle_props.get(_JavaSemConfig.BUCKET_USERNAME) is None:
            updated_fields.append(f'{_JavaSemConfig.BUCKET_USERNAME}={user}')

        if gradle_props.get(_JavaSemConfig.BUCKET_PASSWORD) is None:
            try:
                oauth_token = oauth.resolve_token({}, required=True, query_passwd=True, ignore_existing_token=True)
                updated_fields.append(f'{_JavaSemConfig.BUCKET_PASSWORD}={oauth_token}')
            except Exception as e:
                errors.append(f'Could not get OAuth bucket token: {e}')

        if gradle_props.get(_JavaSemConfig.GRADLE_WRAPPER_USER) is None:
            updated_fields.append(f'{_JavaSemConfig.GRADLE_WRAPPER_USER}={user}')

        if gradle_props.get(_JavaSemConfig.GRADLE_WRAPPER_PASSWORD) is None:
            updated_fields.append(f'{_JavaSemConfig.GRADLE_WRAPPER_PASSWORD}={user}')

        if updated_fields and not errors:
            logging.info(f'Update gradle properties file {_JavaSemConfig.GRADLE_PROPS_FILE}')
            with _JavaSemConfig.GRADLE_PROPS_FILE.open() as f:
                original = f.read()
            with _JavaSemConfig.GRADLE_PROPS_FILE.open(mode='w') as f:
                updated_properties = '\n'.join([original, *updated_fields]).lstrip('\n')
                f.write(updated_properties)

        if errors:
            raise YaIdeGradleException(
                '\n'.join(
                    [
                        'For use [ya ide gradle] REQUIRED access from Gradle to Bucket [http://bucket.yandex-team.ru/]',
                        'Please, read more about work with Bucket https://docs.yandex-team.ru/bucket',
                        'and authentication for Gradle https://docs.yandex-team.ru/bucket/gradle#autentifikaciya',
                        'Token can be taken from here https://oauth.yandex-team.ru/authorize?response_type=token&client_id=bf8b6a8a109242daaf62bce9d6609b3b',
                        '',
                        'Automatic setup failed',
                        *errors,
                    ]
                )
            )
        if not self.params.collect_contribs:
            self.logger.warning(
                "You have selected the mode without collecting contribs to jar files, to build successfully in Gradle, check bucket repository settings and access rights"
            )
        if self.params.strip_symlinks:
            raise YaIdeGradleException(
                "You must remove strip_symlinks = true from ya.conf, else 'ya ide gradle' can't work properly"
            )

    def _check_exclude_targets(self) -> None:
        if not self.params.exclude:
            return
        cwd = Path().cwd().resolve()
        for exclude_target in self.params.exclude:
            exclude_target = Path(exclude_target)
            if not exclude_target.is_absolute():
                exclude_target = cwd / exclude_target
            if not exclude_target.exists():
                raise SemException(f"Not found exclude target {exclude_target}")
            if not exclude_target.is_relative_to(self.arcadia_root):
                raise SemException(f"Not exclude target {exclude_target} not in {self.arcadia_root}")
            rel_exclude_target = exclude_target.relative_to(self.arcadia_root)
            if not self.in_rel_targets(rel_exclude_target):
                self.logger.warning(f"Skip exclude {rel_exclude_target}: not in any export targets")
            if str(rel_exclude_target) in self.params.rel_targets:
                raise SemException(f"Exclude target {rel_exclude_target} can't be same as any export targets")
            self.rel_exclude_targets.append(str(rel_exclude_target))
        self.logger.info("Exclude targets: %s", self.rel_exclude_targets)

    def _check_yexport_toml(self) -> None:
        if not self.params.yexport_toml:
            return
        norm_yexport_toml: dict[str, str] = {}
        for kv in self.params.yexport_toml:
            if str(kv).count('=') != 1:
                raise SemException(f"Invalid --yexport-toml value: {kv}, waited 'key=value'")
            k, v = kv.split('=', 2)
            norm_yexport_toml[k.strip()] = v.strip()
        self.params.yexport_toml = [k + " = " + v for k, v in norm_yexport_toml.items()]
        self.yexport_toml_checked = True

    def is_exclude_target(self, rel_target: Path | str) -> bool:
        if not self.rel_exclude_targets:
            return False
        rel_target = str(rel_target)
        if rel_target in self.rel_exclude_targets:
            return True
        rel_target_len = len(rel_target)
        for rel_exclude_target in self.rel_exclude_targets:
            if rel_target_len > len(rel_exclude_target) and rel_target.startswith(rel_exclude_target + "/"):
                return True
        return False

    def _get_export_root(self) -> Path:
        """Create export_root path by hash of targets"""
        if self.params.settings_root_as_hash_base:
            export_hash = hashing.fast_hash(str(self.settings_root))  # based on settings_root only
            all_abs_targets = ':'.join(sorted(self.params.abs_targets))
            old_export_hash0 = hashing.fast_hash(all_abs_targets)  # based on targets
            old_export_hash1 = hashing.fast_hash(
                str(self.settings_root) + ':' + all_abs_targets
            )  # based on settings_root + targets
            old_export_root0 = _JavaSemConfig.EXPORT_ROOT_BASE / old_export_hash0
            old_export_root1 = _JavaSemConfig.EXPORT_ROOT_BASE / old_export_hash1
            if self.params.remove:  # in remove mode use old hashes if export root exists
                if old_export_root1.exists():
                    export_hash = old_export_hash1
                elif old_export_root0.exists():
                    export_hash = old_export_hash0
            else:  # export (or reexport) mode
                # remove another old export roots if exists
                if old_export_hash0 != export_hash and old_export_root0.exists():
                    self.logger.warning("Remove old export root %s", old_export_root0)
                    shutil.rmtree(old_export_root0)
                if old_export_hash1 != export_hash and old_export_root1.exists():
                    self.logger.warning("Remove old export root %s", old_export_root1)
                    shutil.rmtree(old_export_root1)
        else:
            all_abs_targets = ':'.join(sorted(self.params.abs_targets))
            export_hash = hashing.fast_hash(all_abs_targets)
            if not (_JavaSemConfig.EXPORT_ROOT_BASE / export_hash).exists():
                # First time export, add settings root to hash - new improved hashing logic
                export_hash = hashing.fast_hash(str(self.settings_root) + ':' + all_abs_targets)
        export_root = _JavaSemConfig.EXPORT_ROOT_BASE / export_hash
        self.logger.info("Export root: %s", export_root)
        return export_root

    def _get_settings_root(self) -> Path:
        """Create settings_root path by options and targets"""
        settings_root = Path(self.params.abs_targets[0])
        cwd = self.start_cwd
        if self.params.settings_root:
            cwd_settings_root = cwd / self.params.settings_root
            if cwd.is_relative_to(self.arcadia_root) and cwd_settings_root.exists():
                settings_root = cwd_settings_root
            else:
                settings_root = self.arcadia_root / Path(self.params.settings_root)
        elif len(self.params.abs_targets) > 1:
            if cwd.is_relative_to(self.arcadia_root) and cwd != self.arcadia_root:
                settings_root = cwd
        self.logger.info("Settings root: %s", settings_root)
        if not settings_root.exists() or not settings_root.is_dir():
            raise YaIdeGradleException('Not found settings root directory')
        return settings_root

    def in_rel_targets(self, rel_target: Path) -> bool:
        for conf_rel_target in self.params.rel_targets:
            if rel_target.is_relative_to(Path(conf_rel_target)):
                return True
        return False

    def in_rel_exclude_targets(self, rel_target: Path) -> bool:
        for exclude_rel_target in self.rel_exclude_targets:
            if rel_target.is_relative_to(Path(exclude_rel_target)):
                return True
        return False

    def is_rel_parent_of_targets(self, rel_target: Path) -> bool:
        for conf_rel_target in self.params.rel_targets:
            if Path(conf_rel_target).is_relative_to(rel_target):
                return True
        return False

    def get_project_gradle_props(self) -> dict[str, str]:
        return self._get_section(self.GRADLE_PROPS)

    def get_configs_dir(self) -> Path:
        """Get directory with ya ide gradle configs"""
        return self.arcadia_root / "build" / "yandex_specific" / "gradle"

    def _update_params(self) -> None:
        props = self._get_section(self._YA_IDE_GRADLE).items()
        if not props:
            return
        os.chdir(self.settings_root)  # in options has relative paths, it relative to settings root
        printedSetFrom = False
        merge_params = ['yexport_toml']
        default_true_params = ['collect_contribs', 'build_foreign']
        for prop, value in props:
            param = self._prop2param(prop)
            default_value = True
            if param.startswith('no_'):
                param = param[3:]
                default_value = False
            if not hasattr(self.params, param):
                raise YaIdeGradleException(f"Unknown ya ide gradle param {param}")
            if param in merge_params:  # Merge lists, and config values prepend command line values
                value = re.split(r'[\s\t\n,]+', value)
                setattr(self.params, param, value + getattr(self.params, param))
                if param == 'yexport_toml':
                    self._check_yexport_toml()
            else:
                if (getattr(self.params, param) and (param not in default_true_params)) or (
                    not getattr(self.params, param) and (param in default_true_params)
                ):  # already overwrote by command line opts
                    continue  # skip config value
                if isinstance(getattr(self.params, param), list):  # this param must be list
                    value = re.split(r'[\s\t\n,]+', value)
                elif value is None:  # prop without value, some flag, for example, disable-lombok-plugin
                    value = default_value
                setattr(self.params, param, value)
            if not printedSetFrom:
                self.logger.info(
                    "Set params from %s:",
                    ', '.join([str(Path(f).relative_to(self.arcadia_root)) for f in self.ya_gradle_config_files]),
                )
                printedSetFrom = True
            self.logger.info("    %s = %s", param, getattr(self.params, param))

    @staticmethod
    def _prop2param(prop: str) -> str:
        return prop.replace('-', '_')

    def _get_section(self, section: str) -> dict[str, str]:
        self._load_ya_gradle_config()
        if section not in self.ya_gradle_config.sections():
            return {}
        return self.ya_gradle_config[section]

    def _load_ya_gradle_config(self) -> None:
        if self.ya_gradle_config is not None:
            return
        self.ya_gradle_config = ConfigParser(allow_no_value=True)
        ya_gradle_config_files = [
            str(self.get_configs_dir() / self._YA_GRADLE_CONFIG),
            str(self.settings_root / self._YA_GRADLE_CONFIG),
        ]
        try:
            self.ya_gradle_config_files = self.ya_gradle_config.read(ya_gradle_config_files, 'UTF-8')
        except Exception as e:
            raise YaIdeGradleException(f"Can't read config files {ya_gradle_config_files}: {e}") from e

    def _get_sign(self) -> str:
        return hashing.fast_hash(':'.join(sorted(self.params.abs_targets)) + '|' + ':'.join(self.rel_exclude_targets))

    def _sign_filename(self) -> Path:
        return self.export_root / self.CONFIG_SIGN

    def _save_sign(self, sign: str) -> None:
        sign_filename = self._sign_filename()
        sign_filename.parent.mkdir(0o755, parents=True, exist_ok=True)
        with sign_filename.open('w') as f:
            f.write(sign)

    def _load_sign(self) -> str:
        sign_filename = self._sign_filename()
        if not sign_filename.exists():
            return ""
        with sign_filename.open('r') as f:
            return f.read()

    def sign(self) -> None:
        sign = self._get_sign()
        self.new_sign: bool = self._load_sign() != sign
        self._save_sign(sign)
