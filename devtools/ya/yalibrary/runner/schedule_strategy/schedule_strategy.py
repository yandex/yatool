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
from collections.abc import Callable


logger = logging.getLogger(__name__)


type Action = object
type ScheduleHeuristic = Callable[[Action], float]
type ScheduleStrategy = tuple[ScheduleHeuristic, ...]


def _lpt(action: Action) -> float:
    """Longest processing time"""
    return -getattr(action, 'build_time', 0)


def _spt(action: Action) -> float:
    """Shortest processing time"""
    return -_lpt(action)


def _max_deps(action: Action) -> int:
    """Task with most deps"""
    return -len(getattr(action, 'deps', []))


def _min_deps(action: Action) -> int:
    """Task with fewest deps"""
    return -_max_deps(action)


def _deeper_first(action: Action) -> int:
    """Task located on the deeper levels of the graph"""
    return -getattr(action, 'max_dist', 0)


def _deeper_last(action: Action) -> int:
    """Task located on the higher levels of the graph"""
    return -_deeper_first(action)


def _lpt_with_deps(action: Action) -> float:
    """Longest processing time including dependent tasks"""
    return -getattr(action, 'build_time_with_deps', 0)


def _spt_with_deps(action: Action) -> float:
    """Shortest processing time including dependent tasks"""
    return -_lpt_with_deps(action)


class Strategies:
    """
    Schedule strategy name is combined from function names of different strategies like this
    <strategy_name_1>__<strategy_name_2> ...
    To see all schedule strategies check out _SCHEDULE_STRATEGIES below
    """

    _build_time_components = ('lpt', 'spt')
    default_w_cache = (_deeper_first, _lpt)
    default_wo_cache = (_deeper_first,)

    @classmethod
    def pick(cls, name: str, build_time_cache_available: bool) -> ScheduleStrategy:
        default = cls.default_w_cache if build_time_cache_available else cls.default_wo_cache

        if name and hasattr(cls, name):
            requires_cache = any(component in name for component in cls._build_time_components)
            if requires_cache and not build_time_cache_available:
                logger.warning(
                    "Build time based schedule strategies are not available because build time cache is not set up. Falling back to '%s'",
                    strategy_name(cls.default_wo_cache),
                )
                return cls.default_wo_cache
            else:
                return getattr(cls, name)
        elif name and not hasattr(cls, name):
            logger.warning(
                "Unknown runner's schedule strategy '%s'. Falling back to '%s'", name, strategy_name(default)
            )
        return default


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


def strategy_name(strategy: ScheduleStrategy) -> str:
    return '__'.join(fn.__name__.strip('_') for fn in strategy)


def strategy_names() -> tuple[str]:
    return tuple(strategy_name(strategy) for strategy in _STRATEGIES)


for strategy in _STRATEGIES:
    setattr(Strategies, strategy_name(strategy), strategy)
