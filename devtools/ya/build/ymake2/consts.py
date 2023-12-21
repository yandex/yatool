import enum


class YmakeEvents(enum.Enum):
    DEFAULT = 'd'
    ALL = 'a'
    PROGRESS = 'G'
    TOOLS = 'T'
    PREFETCH = 'H'
