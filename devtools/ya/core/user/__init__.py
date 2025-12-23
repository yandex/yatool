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
    if not username:
        return UserClass.ROBOT
    if username.startswith('teamcity'):
        return UserClass.ROBOT
    if username == 'loadbase':
        return UserClass.ROBOT
    if username.startswith('robot-'):
        return UserClass.ROBOT
    if username in ('sandbox', 'isandbox'):
        return UserClass.SANDBOX
    if username in ('root'):
        return UserClass.ROOT_USER
    if username.startswith('db-runner'):
        return UserClass.DISTBUILD
    if username.startswith('zomb-'):
        return UserClass.ZOMB
    if username.isdigit():
        return UserClass.ROBOT

    return UserClass.USER
