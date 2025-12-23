from devtools.ya.core import stage_tracer
from pathlib import Path
import exts.filelock

tracer = stage_tracer.get_tracer("gradle")


class YaIdeGradleException(Exception):
    mute = True


class ExclusiveLock:
    def __init__(self, path: Path):
        self.lock = exts.filelock.FileLock(str(path.parent / (path.name + '.lock')))

    def acquire(self) -> bool:
        return self.lock.acquire()

    def release(self) -> None:
        self.lock.release()
        try:
            lock_file = Path(self.lock.path)
            if lock_file.exists():
                lock_file.unlink()
        except Exception:
            pass  # ignore any errors at removing lock file

    def __enter__(self):
        self.acquire()
        return self

    def __exit__(self, type, value, traceback):
        self.release()
