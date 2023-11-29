from enum import Enum


class HelpLevel(Enum):
    _DO_NOT_SHOW_HELP = 0
    BASIC = 1
    ADVANCED = 2
    EXPERT = 3
    INTERNAL = 4
    NONE = 5
