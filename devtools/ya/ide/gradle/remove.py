import logging
import shutil

from devtools.ya.ide.gradle.config import _JavaSemConfig
from devtools.ya.ide.gradle.symlinks import _RemoveSymlinkCollector


class _Remover:
    """Remove all symlinks and export root"""

    def __init__(self, java_sem_config: _JavaSemConfig, remove_symlinks: _RemoveSymlinkCollector):
        self.logger = logging.getLogger(type(self).__name__)
        self.config: _JavaSemConfig = java_sem_config
        self.remove_symlinks: _RemoveSymlinkCollector = remove_symlinks

    def remove(self) -> None:
        """Remove all exists symlinks and then remove export root"""
        if self.remove_symlinks.symlinks:
            self.logger.info("Remove %d symlinks from arcadia to export root", len(self.remove_symlinks.symlinks))
            self.remove_symlinks.remove()
        if self.config.export_root.exists():
            try:
                self.logger.info("Remove export root %s", self.config.export_root)
                shutil.rmtree(self.config.export_root)
            except Exception as e:
                self.logger.warning("While removing %s: %s", self.config.export_root, e, exc_info=True)
        else:
            self.logger.info("Export root %s already not found", self.config.export_root)
