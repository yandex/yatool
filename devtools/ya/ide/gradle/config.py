import logging
from pathlib import Path

from devtools.ya.core import config as core_config
from devtools.ya.build.sem_graph import SemLang, SemConfig, SemException
from yalibrary import platform_matcher
from exts import hashing

from devtools.ya.ide.gradle.common import YaIdeGradleException


class _JavaSemConfig(SemConfig):
    """Check and use command line options for configure roots and flags"""

    GRADLE_PROPS = 'gradle.properties'  # Gradle properties filename auto detected by Gradle
    GRADLE_PROPS_FILE: Path = Path.home() / '.gradle' / GRADLE_PROPS  # User Gradle properties file
    GRADLE_REQUIRED_PROPS: tuple[str] = (
        'bucketUsername',
        'bucketPassword',
        'systemProp.gradle.wrapperUser',
        'systemProp.gradle.wrapperPassword',
    )
    GRADLE_BUILD_DIR = "gradle.build"

    EXPORT_ROOT_BASE: Path = Path(core_config.misc_root()) / 'gradle'  # Base folder of all export roots

    def __init__(self, params):
        if platform_matcher.is_windows():
            raise YaIdeGradleException("Windows is not supported in ya ide gradle")
        super().__init__(SemLang.JAVA(), params)
        self.logger = logging.getLogger(type(self).__name__)
        self.settings_root: Path = self._get_settings_root()
        if not self.params.remove:
            self._check_gradle_props()
        self.rel_exclude_targets: list[str] = []
        self._check_exclude_targets()

    def _check_gradle_props(self) -> None:
        """Check exists all required gradle properties"""
        errors = []
        if not _JavaSemConfig.GRADLE_PROPS_FILE.is_file():
            errors.append(f'File {_JavaSemConfig.GRADLE_PROPS_FILE} does not exist')
        else:
            with _JavaSemConfig.GRADLE_PROPS_FILE.open() as f:
                props = f.read()
            for prop in _JavaSemConfig.GRADLE_REQUIRED_PROPS:
                if prop not in props:
                    errors.append(f'Required property {prop} is not defined in {_JavaSemConfig.GRADLE_PROPS_FILE} file')
        if errors:
            raise YaIdeGradleException(
                '\n'.join(
                    [
                        'For use [ya ide gradle] REQUIRED access from Gradle to Bucket [http://bucket.yandex-team.ru/]',
                        'Please, read more about work with Bucket https://docs.yandex-team.ru/bucket',
                        'and authentication for Gradle https://docs.yandex-team.ru/bucket/gradle#autentifikaciya',
                        'Token can be taken from here https://oauth.yandex-team.ru/authorize?response_type=token&client_id=bf8b6a8a109242daaf62bce9d6609b3b',
                        '',
                        f'Now Gradle properties file {_JavaSemConfig.GRADLE_PROPS_FILE} is invalid:',
                        *errors,
                    ]
                )
            )
        if not self.params.collect_contribs:
            self.logger.warning(
                "You have selected the mode without collecting contribs to jar files, to build successfully in Gradle, check bucket repository settings and access rights"
            )

    def _check_exclude_targets(self) -> None:
        if not self.params.exclude_targets:
            return
        cwd = Path().cwd().resolve()
        for exclude_target in self.params.exclude_targets:
            exclude_target = Path(exclude_target)
            if not exclude_target.is_absolute():
                exclude_target = cwd / exclude_target
            if not exclude_target.exists():
                raise SemException(f"Not found exclude target {exclude_target}")
            if not exclude_target.is_relative_to(self.arcadia_root):
                raise SemException(f"Not exclude target {exclude_target} not in {self.arcadia_root}")
            rel_exclude_target = exclude_target.relative_to(self.arcadia_root)
            if not self.in_rel_targets(rel_exclude_target):
                raise SemException(f"Exclude target {rel_exclude_target} not in any export targets")
            if str(rel_exclude_target) in self.params.rel_targets:
                raise SemException(f"Exclude target {rel_exclude_target} can't be same as any export targets")
            self.rel_exclude_targets.append(str(rel_exclude_target))
        self.logger.info("Exclude targets: %s", self.rel_exclude_targets)

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
        targets_hash = hashing.fast_hash(':'.join(sorted(self.params.abs_targets)))
        export_root = _JavaSemConfig.EXPORT_ROOT_BASE / targets_hash
        self.logger.info("Export root: %s", export_root)
        return export_root

    def _get_settings_root(self) -> Path:
        """Create settings_root path by options and targets"""
        settings_root = Path(self.params.abs_targets[0])
        cwd = Path.cwd()
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
