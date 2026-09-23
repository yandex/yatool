import os
import getpass
import logging

from library.python import user_class

logger = logging.getLogger(__name__)


USER_CLASS_BY_NAME = {
    '': user_class.UserClass.ROBOT,
    'loadbase': user_class.UserClass.ROBOT,
    'sandbox': user_class.UserClass.SANDBOX,
    'isandbox': user_class.UserClass.SANDBOX,
    'root': user_class.UserClass.ROOT_USER,
}

USER_CLASS_BY_PREFIX = {
    'teamcity': user_class.UserClass.ROBOT,
    'robot-': user_class.UserClass.ROBOT,
    'db-runner': user_class.UserClass.DISTBUILD,
    'zomb-': user_class.UserClass.ZOMB,
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


def classify_user(username: str) -> user_class.UserClass:
    if username in USER_CLASS_BY_NAME:
        return USER_CLASS_BY_NAME[username]
    for prefix, classified_user in USER_CLASS_BY_PREFIX.items():
        if username.startswith(prefix):
            return classified_user
    if username.isdigit():
        return user_class.UserClass.ROBOT

    return user_class.UserClass.USER


def classify_invocation_user(username: str, caller_info: dict | None = None) -> user_class.UserClass:
    if caller_info and caller_info.get('agent') not in (None, '', 'unknown'):
        return user_class.UserClass.AGENT

    return classify_user(username)
