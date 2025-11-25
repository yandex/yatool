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
        try:
            if self.lock.acquire():
                self.success_locked = True
                return True
        except Exception:
            pass  # acquire failed
        return False

    def release(self) -> None:
        if not self.success_locked:
            return
        self.lock.release()
        self.success_locked = False
