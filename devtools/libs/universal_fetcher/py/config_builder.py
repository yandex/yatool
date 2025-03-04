import collections.abc
import dataclasses
import json
import typing as tp
import abc
import collections

from .types import FetcherType, SandboxTransportType, NetworkType


def _to_ms(ms: int) -> str:
    return f"{ms}ms"


class BaseFetcherConfig(abc.ABC):
    @property
    @abc.abstractmethod
    def fetcher_type(self) -> FetcherType:
        pass

    @abc.abstractmethod
    def to_dict(self) -> dict:
        pass


@dataclasses.dataclass
class RetryPolicy:
    max_retry_count: int = 0
    initial_delay_ms: int = 10
    backoff_multiplier: float = 1.25
    max_delay_ms: int = 1_000
    jitter: bool = False
    use_fixed_delay: bool = True

    def to_dict(self) -> dict:
        return {
            "max_retry_count": self.max_retry_count,
            "initial_delay": _to_ms(self.initial_delay_ms),
            "backoff_multiplier": self.backoff_multiplier,
            "max_delay": _to_ms(self.max_delay_ms),
            "jitter": self.jitter,
            "use_fixed_delay": self.use_fixed_delay,
        }


@dataclasses.dataclass
class CustomFetcherConfig(BaseFetcherConfig):
    path_to_fetcher: str

    @property
    def fetcher_type(self) -> FetcherType:
        return FetcherType.CUSTOM

    def to_dict(self) -> dict:
        return dataclasses.asdict(self)


@dataclasses.dataclass
class DockerParams:
    auth_json_file: str
    skopeo_binary: str = "skopeo"
    timeout_ms: int = 600_000
    image_size_limit: int = 3e9  # 3gb
    _skopeo_args: list[str] | None = None

    def to_dict(self) -> dict:
        res = {
            "auth_json_file": self.auth_json_file,
            "skopeo_binary": self.skopeo_binary,
            "timeout": _to_ms(self.timeout_ms),
            "image_size_limit": self.image_size_limit,
        }

        if self._skopeo_args is not None:
            res["_skopeo_args"] = self._skopeo_args

        return res


@dataclasses.dataclass
class HttpParams:
    connect_timeout_ms: int = 30_000
    socket_timeout_ms: int = 5_000
    max_redirect_count: int = 5
    user_agent: str = "HttpUnifetcher/0.1"

    def to_dict(self) -> dict:
        return {
            "connect_timeout": _to_ms(self.connect_timeout_ms),
            "socket_timeout": _to_ms(self.socket_timeout_ms),
            "max_redirect_count": self.max_redirect_count,
            "user_agent": self.user_agent,
        }


class MdsParams(HttpParams):
    pass


@dataclasses.dataclass
class ExternalProgramFetcherParams:
    cmd: list[str]

    def to_dict(self):
        return dataclasses.asdict(self)


@dataclasses.dataclass
class SkynetParams:
    sky_run_timeout_ms: int | None = None
    network_type: NetworkType = NetworkType.BACKBONE

    def to_dict(self) -> dict:
        res = {"network_type": self.network_type}
        if self.sky_run_timeout_ms is not None:
            res["sky_run_timeout"] = _to_ms(self.sky_run_timeout_ms)
        return res


@dataclasses.dataclass
class SandboxParams:
    api_url: str = "https://sandbox.yandex-team.ru/api/v1.0/resource/"
    oauth_token: str = ""
    allow_no_auth: bool = False
    transports_order: collections.abc.Sequence[SandboxTransportType] = (
        SandboxTransportType.EXTERNAL_PROGRAM_FETCHER,
        SandboxTransportType.HTTP,
        SandboxTransportType.SKYNET,
        SandboxTransportType.MDS,
    )

    def to_dict(self):
        return dataclasses.asdict(self)


@dataclasses.dataclass
class DockerConfig(BaseFetcherConfig):
    params: DockerParams
    retry_policy: RetryPolicy | None = None

    @property
    def fetcher_type(self) -> FetcherType:
        return FetcherType.DOCKER

    def to_dict(self) -> dict:
        res = self.params.to_dict()
        if self.retry_policy:
            res["retries"] = self.retry_policy.to_dict()
        return res


@dataclasses.dataclass
class HttpConfig(BaseFetcherConfig):
    params: HttpParams
    retry_policy: RetryPolicy | None = None

    @property
    def fetcher_type(self) -> FetcherType:
        return FetcherType.HTTP

    def to_dict(self) -> dict:
        res = self.params.to_dict()
        if self.retry_policy:
            res["retries"] = self.retry_policy.to_dict()
        return res


@dataclasses.dataclass
class SandboxConfig(BaseFetcherConfig):
    sandbox_params: SandboxParams
    http_params: HttpParams | None = None
    skynet_params: SkynetParams | None = None
    mds_params: MdsParams | None = None
    external_program_fetcher_params: ExternalProgramFetcherParams | None = None
    retry_policy: RetryPolicy | None = None

    @property
    def fetcher_type(self) -> FetcherType:
        return FetcherType.SANDBOX

    def to_dict(self) -> dict:
        res = self.sandbox_params.to_dict()

        if self.http_params:
            res["http"] = self.http_params.to_dict()

        if self.skynet_params:
            res["skynet"] = self.skynet_params.to_dict()

        if self.mds_params:
            res["mds"] = self.mds_params.to_dict()

        if self.external_program_fetcher_params:
            res["external_program_fetcher"] = self.external_program_fetcher_params.to_dict()

        if self.retry_policy:
            res["retries"] = self.retry_policy.to_dict()

        return res


class FetchersConfig:
    def __init__(self, *configs: BaseFetcherConfig) -> None:
        self._validate_configs(configs)
        self._configs = configs

    @staticmethod
    def _validate_configs(configs: tp.Sequence) -> None:
        assert configs

        for cfg in configs:
            assert isinstance(cfg, BaseFetcherConfig), "You must inherit from BaseFetcherConfig"

    def to_dict(self) -> dict:
        res = {"fetchers": {}}
        for cfg in self._configs:
            res["fetchers"][cfg.fetcher_type] = cfg.to_dict()
        return res

    def build(self) -> str:
        return json.dumps(self.to_dict())
