import logging
from collections.abc import Iterable
from pathlib import Path

from devtools.ya.yalibrary import sjson

from devtools.ya.ide.gradle.common import tracer
from devtools.ya.ide.gradle.config import _JavaSemConfig
from devtools.ya.ide.gradle.ya_settings import _YaSettings


class _SymlinkCollector:
    """Iterate on settings and build root and call collect function for every place, where symlinks waited"""

    SETTINGS_FILES: tuple[str] = (
        "settings.gradle.kts",
        "gradlew",
        "gradlew.bat",
        _YaSettings.YA_SETTINGS_XML,
        _JavaSemConfig.GRADLE_PROPS,
    )  # Files for symlink to settings root
    SETTINGS_MKDIRS: tuple[str] = (".idea", ".kotlin", "build")  # Folders for creating at settings root
    SETTINGS_DIRS: tuple[str] = list(SETTINGS_MKDIRS) + ["gradle"]  # Folders for symlink to settings root

    BUILD_SKIP_ROOT_DIRS: tuple[str] = list(SETTINGS_DIRS) + [
        _JavaSemConfig.YMAKE_DIR
    ]  # Skipped for build directories in export root
    BUILD_FILE: str = "build.gradle.kts"  # Filename for create build symlinks

    def __init__(self, java_sem_config: _JavaSemConfig):
        self.config: _JavaSemConfig = java_sem_config

    def collect_symlinks(self) -> Iterable[tuple[Path]]:
        yield from self._collect_settings_symlinks()
        yield from self._collect_build_symlinks()

    def _collect_settings_symlinks(self) -> Iterable[tuple[Path]]:
        """Collect symlinks for each settings files/dirs"""
        for mkdir in _SymlinkCollector.SETTINGS_MKDIRS:
            _SymlinkCollector.mkdir(self.config.export_root / mkdir)
        for export_file in self.config.export_root.iterdir():
            basename = export_file.name
            if (basename in _SymlinkCollector.SETTINGS_FILES and export_file.is_file()) or (
                basename in _SymlinkCollector.SETTINGS_DIRS and export_file.is_dir()
            ):
                arcadia_file = self.config.settings_root / basename
                yield export_file, arcadia_file

    def _collect_build_symlinks(self) -> Iterable[tuple[Path]]:
        """Collect symlinks for each build files/dirs from arcadia to export"""
        for export_file in self.config.export_root.iterdir():
            basename = export_file.name
            if basename not in _SymlinkCollector.BUILD_SKIP_ROOT_DIRS and export_file.is_dir():
                export_dir = export_file
                for walk_root, _, files in export_dir.walk():
                    for file in files:
                        if file == _SymlinkCollector.BUILD_FILE:
                            export_file = walk_root / file
                            arcadia_file = self.config.arcadia_root / export_file.relative_to(self.config.export_root)
                            yield export_file, arcadia_file
            elif basename == _SymlinkCollector.BUILD_FILE and export_file.is_file():
                arcadia_file = self.config.arcadia_root / basename
                yield export_file, arcadia_file

    @staticmethod
    def mkdir(path: Path) -> None:
        path.mkdir(0o755, parents=True, exist_ok=True)

    @staticmethod
    def resolve(path: Path, path_root: Path = None, export_root: Path = None) -> tuple[bool, Path]:
        """If symlink, resolve path, else return path as is"""
        if not path.is_symlink():
            return True, path
        try:
            resolved_path = path.resolve(True)
            if (
                path_root is not None
                and export_root is not None
                and resolved_path.is_relative_to(export_root)
                and (_JavaSemConfig.GRADLE_BUILD_DIR not in str(resolved_path))
            ):
                tail = str(path.relative_to(path_root))  # Tail of path relative to root
                if not str(resolved_path).endswith(tail):  # Resolved must has same tail
                    # else it invalid symlink
                    path.unlink()  # remove invalid symlink
                    return False, path
            return True, resolved_path
        except Exception:
            path.unlink()  # remove invalid symlink
            return False, path


class _ExistsSymlinkCollector(_SymlinkCollector):
    """Collect exists symlinks for remove later"""

    _SYMLINKS_FILE = 'symlinks.json'

    def __init__(self, java_sem_config: _JavaSemConfig):
        super().__init__(java_sem_config)
        self.logger = logging.getLogger(type(self).__name__)
        self.symlinks: dict[Path, Path] = {}

    def collect(self) -> None:
        """Collect already exists symlinks"""
        if not self.config.export_root.exists():
            return

        try:
            if self._load():
                return
        except Exception as e:
            self.logger.error("Can't load symlinks from file %s: %s", self._symlinks_path, e)

        for export_file, arcadia_file in self.collect_symlinks():
            if self._check_symlink(arcadia_file, export_file):
                self.add_symlink(arcadia_file, export_file)

    def add_symlink(self, arcadia_file: Path, export_file: Path) -> None:
        self.symlinks[arcadia_file] = export_file

    def del_symlink(self, arcadia_file: Path) -> None:
        del self.symlinks[arcadia_file]

    def save(self) -> None:
        if not self.config.export_root.exists():
            return
        symlinks_path = self._symlinks_path
        symlinks: dict[str, str] = {
            str(arcadia_file): str(export_file) for arcadia_file, export_file in self.symlinks.items()
        }
        with symlinks_path.open('wb') as f:
            sjson.dump(symlinks, f)

    def _load(self) -> bool:
        symlinks_path = self._symlinks_path
        if not symlinks_path.exists():
            return False
        with symlinks_path.open('rb') as f:
            symlinks: dict[str, str] = sjson.load(f)
        self.symlinks = {}
        for arcadia_file, export_file in symlinks.items():
            arcadia_file = Path(arcadia_file)
            export_file = Path(export_file)
            if self._check_symlink(arcadia_file, export_file):
                self.add_symlink(arcadia_file, export_file)
        return True

    def _check_symlink(self, arcadia_file: Path, export_file: Path) -> bool:
        """Check symlink exists, remove invalid symlinks"""
        is_valid, to_path = _SymlinkCollector.resolve(arcadia_file)
        return is_valid and to_path == export_file.absolute()

    @property
    def _symlinks_path(self) -> Path:
        """Make filename for store symlinks"""
        return self.config.export_root / self._SYMLINKS_FILE


class _RemoveSymlinkCollector(_SymlinkCollector):
    """Collect for remove symlinks"""

    _MAX_SYMLINKS_DEEP: int = 3

    def __init__(self, exists_symlinks: _ExistsSymlinkCollector):
        super().__init__(exists_symlinks.config)
        self.logger = logging.getLogger(type(self).__name__)
        self.symlinks: dict[Path, Path] = exists_symlinks.symlinks.copy()
        self.exists_symlinks: _ExistsSymlinkCollector = exists_symlinks

    def remove(self) -> None:
        """Remove symlinks from arcadia files to export files"""
        for arcadia_file, export_file in self.symlinks.items():
            try:
                arcadia_file.unlink()
                self.exists_symlinks.del_symlink(arcadia_file)  # remove deleted from exists
            except Exception as e:
                self.logger.warning(
                    "Can't remove symlink '%s' -> '%s': %s", arcadia_file, export_file, e, exc_info=True
                )

    def add_symlink(self, arcadia_file: Path, export_file: Path) -> None:
        self.symlinks[arcadia_file] = export_file

    def del_symlink(self, arcadia_file: Path) -> None:
        del self.symlinks[arcadia_file]

    def remove_invalid_symlinks(self):
        _RemoveSymlinkCollector._remove_invalid_symlinks(
            self.config.settings_root, self.config.export_root, -1
        )  # Check only top level for settings
        for rel_target in self.config.params.rel_targets:
            _RemoveSymlinkCollector._remove_invalid_symlinks(
                self.config.arcadia_root / rel_target, self.config.export_root, self._MAX_SYMLINKS_DEEP
            )

    @staticmethod
    def _remove_invalid_symlinks(dirtree: Path, export_root: Path, symlinks_deep: int) -> None:
        """Remove all invalid symlinks"""
        for walk_root, dirs, files in dirtree.walk():
            has_symlinks = False
            for item in dirs + files:
                path = walk_root / item
                res, resolved_path = _SymlinkCollector.resolve(path, dirtree, export_root)
                if not res or path != resolved_path:
                    has_symlinks = True
            if symlinks_deep >= 0:
                for dir in dirs:  # Manual walk into dir
                    _RemoveSymlinkCollector._remove_invalid_symlinks(
                        walk_root / dir,
                        export_root,
                        _RemoveSymlinkCollector._MAX_SYMLINKS_DEEP if has_symlinks else symlinks_deep - 1,
                    )
            dirs.clear()  # Stop auto walking into walk_root by dirtree


class _NewSymlinkCollector(_SymlinkCollector):
    """Collect new symlinks for create later, exclude already exists"""

    def __init__(self, exists_symlinks: _ExistsSymlinkCollector, remove_symlinks: _RemoveSymlinkCollector):
        super().__init__(exists_symlinks.config)
        self.logger = logging.getLogger(type(self).__name__)
        self.exists_symlinks: _ExistsSymlinkCollector = exists_symlinks
        self.remove_symlinks: _RemoveSymlinkCollector = remove_symlinks
        self.symlinks: dict[Path, Path] = {}
        self.has_errors: bool = False

    def collect(self) -> None:
        """Collect new symlinks for creating, skip already exists symlinks"""
        for export_file, arcadia_file in self.collect_symlinks():
            self._collect_symlink(export_file, arcadia_file)

    def _collect_symlink(self, export_file: Path, arcadia_file: Path) -> None:
        if arcadia_file in self.remove_symlinks.symlinks:
            # Already exists, don't remove it
            self.remove_symlinks.del_symlink(arcadia_file)
        elif not arcadia_file.exists():
            self.symlinks[arcadia_file] = export_file
        elif arcadia_file.is_symlink():
            is_valid, to_path = _SymlinkCollector.resolve(arcadia_file)
            if is_valid:
                if (to_path != export_file.absolute()) and to_path.is_relative_to(_JavaSemConfig.EXPORT_ROOT_BASE):
                    self.logger.error(
                        "Can't create symlink %s -> %s, already symlink to another Gradle project -> %s",
                        arcadia_file,
                        export_file,
                        to_path,
                    )
                    self.has_errors = True
            else:
                self.symlinks[arcadia_file] = export_file  # require create new symlink

    def create(self) -> None:
        """Create symlinks from arcadia files to export files"""
        for arcadia_file, export_file in self.symlinks.items():
            try:
                arcadia_file.symlink_to(export_file, export_file.is_dir())
                self.exists_symlinks.add_symlink(arcadia_file, export_file)  # add created symlink as exists
            except Exception as e:
                self.logger.error("Can't create symlink '%s' -> '%s': %s", arcadia_file, export_file, e, exc_info=True)
                self.has_errors = True


def _collect_symlinks(
    config: _JavaSemConfig, parent_scope: str = 'collect symlinks'
) -> tuple[_ExistsSymlinkCollector, _RemoveSymlinkCollector]:
    """Collect exists and invalid symlinks, remove invalid symlinks"""
    with tracer.scope(parent_scope + '>exists'):
        exists_symlinks = _ExistsSymlinkCollector(config)
        exists_symlinks.collect()
    with tracer.scope(parent_scope + '>find and remove invalid'):
        remove_symlinks = _RemoveSymlinkCollector(exists_symlinks)
        remove_symlinks.remove_invalid_symlinks()
    return exists_symlinks, remove_symlinks
