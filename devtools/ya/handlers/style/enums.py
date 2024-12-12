from enum import StrEnum, auto


STDIN_FILENAME = 'source.cpp'
STDIN_FILENAME_STAMP = 'STDIN_FILENAME_STAMP'


class StylerKind(StrEnum):
    PY = auto()
    CPP = auto()
    CUDA = auto()
    YAMAKE = auto()
    GO = auto()
