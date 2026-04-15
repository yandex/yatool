import os

import devtools.ya.core.config
from library.python import func
from library.python import unique_id


@func.lazy
def _get_env_session():
    return os.environ.get('GSID', '').split()


@func.lazy
def uid():
    return unique_id.gen16()


@func.lazy
def _get_current_session():
    ya_name = 'YA-DEV' if devtools.ya.core.config.is_developer_ya_version() else 'YA'
    return [ya_name + ':' + uid()]


@func.lazy
def _get_user_session():
    return ['USER' + ':' + devtools.ya.core.config.get_user()]


@func.lazy
def session_id():
    return _get_env_session() + _get_user_session() + _get_current_session()


@func.lazy
def flat_session_id():
    return ' '.join(session_id())
