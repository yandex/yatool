import os
import logging
import queue
import threading
import time
import subprocess

import yalibrary.vcs
import yalibrary.tools
import exts.process
import exts.asyncthread
import core.event_handling as event_handling

logger = logging.getLogger(__name__)


class ArcPrefetchSubscriber(event_handling.SubscriberSpecifiedTopics):
    topics = {"NEvent.TNeedDirHint"}

    @staticmethod
    def get_subscriber(arc_root, prefetch_enabled, vcs_type, ymake_run_uid):
        if arc_root is None:
            logger.debug("arc_root is None, won't start prefetch")
            return None
        if not prefetch_enabled:
            logger.debug("prefetch disabled")
            return None
        if vcs_type != 'arc':
            logger.warning("Prefetch is only available on arc repostiory, %s detected", vcs_type)
            return None

        return ArcPrefetchSubscriber(arc_root, ymake_run_uid)

    def _filter_event(self, event):
        return event["_typename"] in self.topics and event["ymake_run_uid"] == self._ymake_run_uid

    def __init__(self, arc_root, ymake_run_uid):
        self._prefetcher = None
        self._ymake_run_uid = ymake_run_uid
        arc_tool = yalibrary.tools.tool('arc')
        if not arc_tool:
            logger.warning('arc tool couldn\'t be found')
            return
        self._prefetcher = ArcPrefetcher.get_singletone(arc_root, arc_tool)

    def _action(self, event):
        self._prefetcher.add_target(event['Dir'])

    def on_subscribe(self):
        self._prefetcher.start()

    def on_unsubscribe(self):
        self._prefetcher.stop()


class ArcPrefetcher:
    MAX_PREFETCH_TARGETS = 20
    POLL_TIMEOUT = 0.5

    _instance = None

    @classmethod
    def get_singletone(cls, arc_root, arc_tool):
        if cls._instance is None:
            cls._instance = cls(arc_root, arc_tool)

        assert cls._instance._arc_root == arc_root, "arc_root {} does not match with {} used earlier".format(
            arc_root, cls._instance._arc_root
        )
        assert cls._instance._arc_tool == arc_tool, "arc_tool {} does not match with {} used earlier".format(
            arc_tool, cls._instance._arc_tool
        )

        return cls._instance

    def __init__(self, arc_root, arc_tool):
        self._arc_root = arc_root
        self._arc_tool = arc_tool
        self._targets_queue = queue.Queue()

        self._arc_thread = None
        self._stop_requested = False

        self._known_targets = set()

        self._lock = threading.Lock()
        self._subscribers = 0

    def add_target(self, target):
        with self._lock:
            self._targets_queue.put(target)

    def start(self):
        with self._lock:
            self._subscribers += 1
            if self._arc_thread is None:
                logger.debug('Starting arc prefetch-files thread...')
                self._arc_thread = threading.Thread(target=self._run_prefetch_loop)
                self._arc_thread.daemon = True
                self._arc_thread.start()

    def stop(self):
        with self._lock:
            self._subscribers -= 1
            if self._subscribers == 0 and not self._stop_requested and self._arc_thread is not None:
                logger.debug('Stopping arc prefetch-files thread...')
                self._stop_requested = True
                self._targets_queue.put("")
                self._arc_thread.join()
                self._arc_thread = None

            self._stop_requested = False

    def _run_prefetch_loop(self):
        logger.debug('Started arc prefetch-files dispatch loop')

        while not self._stop_requested:
            try:
                targets = [self._targets_queue.get(block=True, timeout=self.POLL_TIMEOUT)]
            except queue.Empty:
                continue

            while not self._targets_queue.empty() and not self._stop_requested:
                targets.append(self._targets_queue.get(block=False))

            if self._stop_requested:
                break

            self._process_targets(targets)

        logger.debug('Stopped arc prefetch-files dispatch loop')

    def _call_arc_prefetch(self, targets):
        cmd = [self._arc_tool, 'prefetch-files'] + [os.path.join(self._arc_root, x) for x in targets]

        proc_start_time = time.time()
        proc = exts.process.popen(cmd, stdout=None, stderr=subprocess.PIPE)
        logger.debug('Calling arc prefetch-files [pid: %d]: %s', proc.pid, cmd)

        subprocess_done = threading.Event()
        err = ['']

        def run():
            try:
                err[0] = proc.communicate()[1]
            finally:
                subprocess_done.set()

        thread = threading.Thread(target=run)
        thread.daemon = True
        thread.start()

        terminating = False
        while not subprocess_done.wait(timeout=self.POLL_TIMEOUT):
            if self._stop_requested and not terminating:
                logger.debug('Terminating arc prefetch-files [pid: %d]', proc.pid)
                terminating = True
                try:
                    proc.terminate()
                except Exception:
                    pass

        thread.join()

        if not terminating:
            elapsed_time = time.time() - proc_start_time
            logger.debug('arc prefetch-files processed %d target(s) in %f seconds', len(targets), elapsed_time)

        logger.debug('Finished arc prefetch-files [pid: %d]. stderr: %s', proc.pid, err[0])

    def _process_targets(self, targets):
        targets = self._dedup_targets(targets)
        paged_targets = (
            targets[i : i + self.MAX_PREFETCH_TARGETS] for i in range(0, len(targets), self.MAX_PREFETCH_TARGETS)
        )

        for t in paged_targets:
            if self._stop_requested:
                break
            self._call_arc_prefetch(t)

    def _dedup_targets(self, targets):
        res = []
        for target in sorted(targets):
            if not self._is_known(target):
                self._known_targets.add(target)
                res.append(target)
        return res

    def _is_known(self, target):
        while target:
            if target in self._known_targets:
                return True
            target = os.path.dirname(target)
        return False
