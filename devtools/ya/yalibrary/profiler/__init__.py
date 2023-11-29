import logging
import contextlib


logger = logging.getLogger(__name__)


class Stats(object):
    def __init__(self):
        self._profiles = []

    def add(self, v):
        import pstats

        self._profiles.append(pstats.Stats(v))

    def save_merged_stat(self, profile_to):
        st = None

        for x in self._profiles:
            if st is None:
                st = x
            else:
                st.add(x)

        if st is not None:
            logger.debug('Save profile to %s', profile_to)
            st.dump_stats(profile_to)


@contextlib.contextmanager
def profiling(usage):
    import cProfile

    prof = cProfile.Profile()
    prof.enable()

    yield

    prof.disable()
    usage(prof)


def _enable_thread_profiling(stats):
    import threading

    logger.debug('Enable threads profiling')

    thread_run = threading.Thread.run

    def profile_run(self):
        with profiling(stats.add):
            thread_run(self)

    threading.Thread.run = profile_run


def with_profiler_support(ctx):
    profile_to = getattr(ctx.params, 'profile_to', None)
    if profile_to:
        stats = Stats()
        _enable_thread_profiling(stats)

        import cProfile

        prof = cProfile.Profile()
        prof.enable()

        try:
            yield
        finally:
            prof.disable()
            stats.add(prof)
            stats.save_merged_stat(profile_to)
    else:
        yield
