"""
Order in which runner executes scheduled tasks.
Order is preserved with the help of priority queue, see WorkerThreads.
"""

__all__ = (
    'Strategies',
    'strategy_name',
    'strategy_names',
)


import logging


logger = logging.getLogger(__name__)


def _lpt(action):
    """Longest processing time"""
    return -getattr(action, 'build_time', 0)


def _spt(action):
    """Shortest processing time"""
    return -_lpt(action)


def _max_deps(action):
    """Task with most deps"""
    return -len(getattr(action, 'deps', []))


def _min_deps(action):
    """Task with fewest deps"""
    return -_max_deps(action)


def _deeper_first(action):
    """Task located on the deeper levels of the graph"""
    return -getattr(action, 'max_dist', 0)


def _deeper_last(action):
    """Task located on the higher levels of the graph"""
    return -_deeper_first(action)


def _lpt_with_deps(action):
    """Longest processing time including dependent tasks"""
    return -getattr(action, 'build_time_with_deps', 0)


def _spt_with_deps(action):
    """Shortest processing time including dependent tasks"""
    return -_lpt_with_deps(action)


class Strategies(object):
    """
    Schedule strategy name is combined from function names of different strategies like this
    <strategy_name_1>__<strategy_name_2> ...
    Default strategy is "deeper_first". To see all schedule strategies check out _SCHEDULE_STRATEGIES below
    """

    _build_time_ss_components = ('lpt', 'spt')
    default = (_deeper_first,)

    @classmethod
    def pick(cls, name, build_time_cache_available):
        if name is None:
            return cls.default
        if not hasattr(cls, name):
            logger.warning(
                "Unknown runner's schedule strategy {!r}. Falling back to default: {!r}".format(
                    name, strategy_name(cls.default)
                )
            )
            return cls.default
        if not build_time_cache_available and any(bth in name for bth in cls._build_time_ss_components):
            logger.warning(
                'Build time based schedule strategies are not available because build time cache is not set up. Falling back to default: {!r}'.format(
                    strategy_name(cls.default)
                )
            )
            return cls.default
        return getattr(cls, name)


_STRATEGIES = (
    (_lpt,),
    (_spt,),
    (_lpt_with_deps,),
    (_spt_with_deps,),
    (_max_deps,),
    (_min_deps,),
    (_deeper_first,),
    (_deeper_last,),
    (_max_deps, _deeper_first),
    (_max_deps, _deeper_last),
    (_min_deps, _deeper_first),
    (_min_deps, _deeper_last),
    (_max_deps, _lpt),
    (_max_deps, _spt),
    (_min_deps, _lpt),
    (_min_deps, _spt),
    (_deeper_first, _lpt),
    (_deeper_first, _spt),
    (_deeper_last, _lpt),
    (_deeper_last, _spt),
    (_deeper_first, _lpt_with_deps),
    (_deeper_first, _spt_with_deps),
)


def strategy_name(strategy):
    return '__'.join(fn.__name__.strip('_') for fn in strategy)


def strategy_names():
    return tuple(strategy_name(strategy) for strategy in _STRATEGIES)


for strategy in _STRATEGIES:
    setattr(Strategies, strategy_name(strategy), strategy)
