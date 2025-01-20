import enum

DEFAULT_BUILD_KEY = "build"


class TraversalType(enum.Enum):
    POSTORDER = "postorder"
    PREORDER = "preorder"
