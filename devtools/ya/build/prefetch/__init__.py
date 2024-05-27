import os
import logging
import queue
import threading
import six
import subprocess

import yalibrary.vcs
import yalibrary.tools
import exts.process
import exts.asyncthread
import core.event_handling as event_handling

import typing as tp  # noqa

logger = logging.getLogger(__name__)


def prefetch_condition(arc_root, prefetch_enabled, vcs_type):
    # type: (tp.Optional[str], bool, str) -> bool
    if arc_root is None:
        logger.debug("arc_root is None, won't start prefetch")
        return False
    if not prefetch_enabled:
        logger.debug("prefetch disabled")
        return False
    if vcs_type != 'arc':
        logger.debug("Prefetch is only available on arc repostiory, %s detected", vcs_type)
        return False
    return True


def _join_thread(thr, name):
    # type: (tp.Callable, str) -> None
    _, exc = thr()
    logger.debug("Thread %s finished", name)
    if exc:
        logger.exception("Exception occured during execution of %s thread", name, exc_info=exc)


class ArcPrefetchSubscriber(event_handling.SubscriberSpecifiedTopics):
    topics = {"NEvent.TNeedDirHint"}

    _instance = None  # type: tp.Optional[ArcPrefetchSubscriber]

    @classmethod
    def get_subscriber(cls, arc_root):
        # type: (str) -> ArcPrefetchSubscriber
        if cls._instance is None:
            cls._instance = ArcPrefetchSubscriber(arc_root)

        if cls._instance._arc_root != arc_root:
            logger.warning(
                "Arc root mismatch at subscriber creation. Requested: %s, Actual: %s", arc_root, cls._instance._arc_root
            )

        return cls._instance

    def __init__(self, arc_root):
        self._arc_root = arc_root
        self._prefetcher = ArcStreamingPrefetcher.get_singleton(arc_root)
        self._subscribers = 0
        self._lock = threading.Lock()

    def _action(self, event):
        self._prefetcher.add_target(event['Dir'])

    def subscribe_to(self, q):
        # type: (event_handling.EventQueue) -> None
        with self._lock:
            self._subscribers += 1
            if self._subscribers == 1:
                logger.debug("Subscribing to event_queue")
                q.subscribe(self)

    def unsubscribe_from(self, q):
        # type: (event_handling.EventQueue) -> None
        with self._lock:
            self._subscribers -= 1
            if self._subscribers == 0:
                logger.debug("Unsubscribing from event_queue")
                q.unsubscribe(self)

    def on_subscribe(self):
        self._prefetcher.start()

    def on_unsubscribe(self):
        self._prefetcher.stop()


class ArcStreamingPrefetcher:
    _instance = None

    @classmethod
    def get_singleton(cls, arc_root):
        # type: (str, tp.Callable) -> ArcStreamingPrefetcher
        if cls._instance is None:
            cls._instance = cls(arc_root)

        assert cls._instance._arc_root == arc_root, "arc_root {} does not match with {} used earlier".format(
            arc_root, cls._instance._arc_root
        )

        return cls._instance

    def _prepare(self):
        self._arc_tool_future = exts.asyncthread.future(lambda: yalibrary.tools.tool('arc'))

    def __init__(self, arc_root):
        self._arc_root = arc_root
        self._arc_tool_future = None  # type: tp.Optional[tp.Callable]

        self._arc_tool = None

        self._arc_process = None

        self._writer_thread = None
        self._reader_thread = None

        self._stop_requested = False

        self._targets_queue = queue.Queue()

        self._known_targets = set()

        self._lock = threading.Lock()

        self._prepare()

    def add_target(self, target):
        self._targets_queue.put(target)

    def start(self):
        self._arc_tool = self._arc_tool_future()
        if self._arc_tool is None:
            # This will warn on each ymake launch.
            logger.warning("arc tool couldn't be found")
            return

        logger.debug('Starting arc prefetch-files')
        # Ignoring not found items for handlers like `ya py`, that create temp dirs in junk
        cmd = [self._arc_tool, 'prefetch-files', '--read-paths-from', '-', '--ignore-not-found']
        self._arc_process = exts.process.popen(
            cmd, stdout=None, stderr=subprocess.PIPE, stdin=subprocess.PIPE, **({'text': True} if six.PY3 else {})
        )
        logger.debug('arc prefetch-files started with pid %d, cmd: %s', self._arc_process.pid, cmd)

        logger.debug('Starting writer and reader threads')
        self._writer_thread = exts.asyncthread.asyncthread(self._run_write_loop)
        self._reader_thread = exts.asyncthread.asyncthread(self._run_read_loop)

    def stop(self):
        if not self._stop_requested and self._arc_process is not None:
            logger.debug("Stopping writer thread")
            self._stop_requested = True
            self._targets_queue.put("")

            _join_thread(self._writer_thread, "stdin_writer")

            logger.debug('Finishing arc prefetch-files [pid: %d]', self._arc_process.pid)
            try:
                self._arc_process.stdin.close()
            except Exception:
                logger.debug(
                    "Exception during closing STDIN of arc prefetch-files [pid: %d].",
                    self._arc_process.pid,
                    exc_info=True,
                )

    def _run_write_loop(self):
        while True:
            targets = []
            while not self._stop_requested:
                try:
                    targets.append(self._targets_queue.get(block=not targets))
                except queue.Empty:
                    break

            if self._stop_requested:
                break

            try:
                for target in self._dedup_targets(targets):
                    self._arc_process.stdin.write(os.path.join(self._arc_root, target) + '\n')
                self._arc_process.stdin.flush()
            except Exception:
                logger.debug(
                    "Exception during writing to STDIN of arc prefetch-files [pid: %d].",
                    self._arc_process.pid,
                    exc_info=True,
                )
                self._stop_requested = True

    def _run_read_loop(self):
        stderr = []
        err = self._arc_process.stderr
        while not self._stop_requested:
            for line in err:
                stderr.append(line)
                if err.closed:
                    break

        if self._stop_requested:
            self._arc_process.wait()

        logger.debug(
            'arc prefetch-files [pid: %d] finished with rc %s. stderr:',
            self._arc_process.pid,
            self._arc_process.returncode,
        )
        for line in stderr:
            logger.debug("%s", line.strip())

    def _is_known(self, target):
        while target:
            if target in self._known_targets:
                return True
            target = os.path.dirname(target)
        return False

    def _dedup_targets(self, targets):
        for target in targets:
            if not self._is_known(target):
                self._known_targets.add(target)
                yield target
