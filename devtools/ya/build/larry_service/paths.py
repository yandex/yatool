"""Paths of a managed service installation, supplied by its owner."""

import os
import sys
import typing

from devtools.ya.core import config as core_config
import devtools.ya_make.libs.larry_client.py as protocol


class ServicePaths(typing.NamedTuple):
    root: str

    @property
    def installations(self) -> str:
        # Resolve ancestor aliases (e.g. /var on macOS). Operations first check
        # that the service root itself is private and is not a symlink.
        return os.path.join(os.path.realpath(self.root), 'installations')

    @property
    def current(self) -> str:
        return os.path.join(self.root, 'current')

    @property
    def executable(self) -> str:
        return os.path.join(self.current, 'larry')

    @property
    def state_file(self) -> str:
        return os.path.join(os.path.realpath(self.root), 'service-state.json')

    @property
    def token_file(self) -> str:
        return os.path.join(os.path.realpath(self.root), 'yt-token')


def default_service_paths(*, data_home: str | None = None) -> ServicePaths:
    """Keep Linux/macOS service ownership shared across transport generations."""
    if data_home is None:
        data_home = os.environ.get('XDG_DATA_HOME') or os.path.join(core_config.home_dir(), '.local', 'share')
    if not os.path.isabs(data_home):
        raise ValueError('Service data root must be absolute')
    namespace = (
        'service' if (sys.platform.startswith('linux') or sys.platform == 'darwin') else str(protocol.TRANSPORT_VERSION)
    )
    return ServicePaths(os.path.join(os.path.realpath(data_home), 'ya', 'larry', namespace))
