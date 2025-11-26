from devtools.ya.core import stage_tracer
from pathlib import Path
from exts.plocker import Lock, LOCK_EX, LOCK_NB

tracer = stage_tracer.get_tracer("gradle")


class YaIdeGradleException(Exception):
    mute = True


class ExclusiveLock:
    def __init__(self, path: Path, timeout: int = 1800):
        self.lock = Lock(path.parent / (path.name + '.lock'), timeout=timeout, mode='w', flags=LOCK_EX | LOCK_NB)
        self.success_locked: bool = False

    def acquire(self) -> bool:
        if self.success_locked:
            return False
        try:
            if self.lock.acquire():
                self.success_locked = True
                return True
        except Exception:
            pass  # acquire failed
        self._remove_lock_file()
        return False

    def release(self) -> None:
        if not self.success_locked:
            return
        self.lock.release()
        self.success_locked = False
        self._remove_lock_file()

    def _remove_lock_file(self) -> None:
        try:
            lock_file = Path(self.lock.filename)
            if lock_file.exists():
                lock_file.unlink()
        except Exception:
            pass  # ignore any errors at removing lock file
