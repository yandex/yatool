import contextlib
import logging

logger = logging.getLogger(__name__)


class Cancelled(Exception):
    tame = True
    mute = True


def cancel():
    raise Cancelled('Cancelled')


class ActiveState(object):
    def __init__(self, name):
        self._name = name
        self._stop_execution = None
        self._actions = set()
        self._subs = []

    def check_cancel_state(self):
        if self._stop_execution:
            self._stop_execution()

        return True

    def sub(self, name):
        sub_state = ActiveState(name)
        self._subs.append(sub_state)

        return sub_state

    def is_stopped(self):
        return self._stop_execution

    def stopping(self, stop_acton=None):
        self._stop_execution = stop_acton or cancel
        for sub in self._subs:
            sub.stopping(stop_acton)

    def stop(self):
        self.stopping()

        for sub in self._subs:
            sub.stop()

        if not self._actions:
            return

        logger.debug('Start cleanup cycle for %s', self._name)

        while self._actions:
            try:
                self._actions.pop()()
            except Exception as e:
                logger.debug('Error while cleanup: %s', str(e))

        logger.debug('End cleanup cycle for %s', self._name)

    @contextlib.contextmanager
    def with_finalizer(self, func):
        self.check_cancel_state()
        self._actions.add(func)
        yield
        try:
            self._actions.remove(func)
        except KeyError:
            pass
