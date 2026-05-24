import atexit
import sys
import psutil


def kill_children() -> None:
    """Terminate all descendant processes of the current process.

    Sends SIGTERM first, then SIGKILL to any survivors after 1 second.
    Safe to call multiple times — silently ignores already-dead processes.
    """
    children = psutil.Process().children(recursive=True)
    for child in children:
        child.terminate()
    gone, alive = psutil.wait_procs(children, timeout=1)
    for p in alive:
        p.kill()


def setup_cronus() -> None:
    if sys.platform == 'linux':
        # prctl(PR_SET_CHILD_SUBREAPER) is Linux-only — makes RM the subreaper
        # for orphaned grandchildren of recipes.  On macOS there is no such
        # syscall; orphans are reparented to launchd instead.
        from library.python.prctl import prctl
        prctl.set_child_subreaper(1)
    atexit.register(kill_children)
