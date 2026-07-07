import os
import enum
import getpass
import logging

logger = logging.getLogger(__name__)


class UserClass(enum.StrEnum):
    DISTBUILD = enum.auto()
    ROBOT = enum.auto()
    ROOT_USER = enum.auto()
    SANDBOX = enum.auto()
    USER = enum.auto()
    ZOMB = enum.auto()
    AGENT = enum.auto()


USER_CLASS_BY_NAME = {
    '': UserClass.ROBOT,
    'loadbase': UserClass.ROBOT,
    'sandbox': UserClass.SANDBOX,
    'isandbox': UserClass.SANDBOX,
    'root': UserClass.ROOT_USER,
}

USER_CLASS_BY_PREFIX = {
    'teamcity': UserClass.ROBOT,
    'robot-': UserClass.ROBOT,
    'db-runner': UserClass.DISTBUILD,
    'zomb-': UserClass.ZOMB,
}


def get_user() -> str:
    try:
        user = (
            os.environ.get('YA_USER', None)
            or os.environ.get('USER', None)
            or os.environ.get('USERNAME', None)
            or (hasattr(os, 'getuid') and ('root' if os.getuid() == 0 else None))  # there is no os.getuid for win
            or getpass.getuser()
        )
    except OSError:
        logger.debug("Failed to obtain username", exc_info=True)
        user = ""

    return user


def classify_user(username: str) -> UserClass:
    if username in USER_CLASS_BY_NAME:
        return USER_CLASS_BY_NAME[username]
    for prefix, user_class in USER_CLASS_BY_PREFIX.items():
        if username.startswith(prefix):
            return user_class
    if username.isdigit():
        return UserClass.ROBOT

    return UserClass.USER


def classify_invocation_user(username: str, caller_info: dict | None = None) -> UserClass:
    if caller_info and caller_info.get('agent') not in (None, '', 'unknown'):
        return UserClass.AGENT

    return classify_user(username)
