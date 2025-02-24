import typing as tp
from collections.abc import Callable, Sequence, Mapping
from enum import Enum, auto


# Enum to allow comparison by id: "is". With a literal string we'd have to use "==".
class _NoValue(Enum):
    NV = auto()


NO_VALUE = _NoValue.NV
type MaybeValue[T] = T | tp.Literal[_NoValue.NV]

_BASE = '$base'


def array[T](settings: Mapping, uvalue: MaybeValue[Sequence[T]], bvalue: MaybeValue[Sequence[T]]) -> str:
    """
    'include' can be `*`, missing (same as `*`), "$base" (=`bvalue`), or a list of values.
    'exclude' can be `*`, missing (same as an empty list), or a list of values.

    1. 'include': `list`, 'exclude': `*` -> `uvalue` must match `include`.
    2. 'include': `list`, 'exclude': `list` -> `uvalue` must include `include` and must not include `exclude`.
    3. 'include': `*`, 'exclude': `*` -> invalid rule.
    4. 'include': `*`, 'exclude': `list` -> `uvalue` must not include `exclude`.
    """

    if uvalue is NO_VALUE:
        if settings.get('mandatory', False):
            return "Mandatory field is not present"
        return ''

    include = settings.get('include')
    if include is None or include == '*':
        include = '*'
    elif include == _BASE:
        if bvalue is NO_VALUE:
            return "Can't use value from base config, it's not set"
        include = set(bvalue)
    else:
        assert isinstance(include, Sequence)
        include = set(include)

    exclude = settings.get('exclude')
    if exclude is None:
        exclude = set()
    elif exclude == '*':
        pass
    else:
        assert isinstance(exclude, Sequence)
        exclude = set(exclude)

    uvalue_ = set(uvalue)
    if exclude == '*' and include != '*':
        if uvalue_ != include:
            return f"{sorted(uvalue_)} does not match {sorted(include)}"  # type: ignore
        return ''
    elif exclude != '*' and include != '*':
        if exclude & uvalue_:
            return f"Some of {sorted(uvalue_)} is in {sorted(exclude)}"  # type: ignore
        if include - uvalue_:
            return f"Some of {sorted(include)} is not in {sorted(uvalue_)}"  # type: ignore
    elif exclude != '*' and include == '*':
        if exclude & uvalue_:
            return f"Some of {sorted(uvalue_)} is in {sorted(exclude)}"  # type: ignore
    else:
        return 'Invalid rule'

    return ''


def number(settings: Mapping, uvalue: MaybeValue[float], bvalue: MaybeValue[float]) -> str:
    """
    'min' and 'max' can be "$base" (=`bvalue`), or a number.

    `uvalue` must be between `min` and `max`.
    """
    if uvalue is NO_VALUE:
        if settings.get('mandatory', False):
            return "Mandatory field is not present"
        return ''

    minmax = [settings['min'], settings['max']]
    for i, m in enumerate(minmax):
        if m == _BASE:
            if bvalue is NO_VALUE:
                return "Can't use value from base config, it's not set"
            minmax[i] = bvalue

    if not minmax[0] <= uvalue <= minmax[1]:
        return f"Value {uvalue} is not between {minmax[0]} and {minmax[1]}"

    return ''


def enum(settings: Mapping, uvalue: MaybeValue[str], bvalue: MaybeValue[str]) -> str:
    """
    'any_of' can be "$base" (=`bvalue`), or a list of values.

    `uvalue` must be in `any_of`.
    """
    if uvalue is NO_VALUE:
        if settings.get('mandatory', False):
            return "Mandatory field is not present"
        return ''

    any_of = settings['any_of']
    if any_of == _BASE:
        if bvalue is NO_VALUE:
            return "Can't use value from base config, it's not set"
        any_of = [bvalue]

    if uvalue not in any_of:
        return f"Value {uvalue} is not in {any_of}"

    return ''


def select_rule(type: str) -> Callable[[dict, MaybeValue, MaybeValue], str]:
    return globals()[type]
