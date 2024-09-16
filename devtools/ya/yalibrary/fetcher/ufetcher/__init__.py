import os
import logging
import typing as tp


import devtools.experimental.universal_fetcher.py as universal_fetcher
import exts.func
import exts.archive
import app_config

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)


DEFAULT_TRANSPORT_ORDER = [
    universal_fetcher.SandboxTransportType.HTTP,
    universal_fetcher.SandboxTransportType.SKYNET,
    universal_fetcher.SandboxTransportType.MDS,
]

FETCHER_PARAM_TO_UFETCHER_SANDBOX_TRANSPORT_TYPE = {
    "proxy": universal_fetcher.SandboxTransportType.HTTP,
    "sandbox": universal_fetcher.SandboxTransportType.HTTP,
    "skynet": universal_fetcher.SandboxTransportType.SKYNET,
    "mds": universal_fetcher.SandboxTransportType.MDS,
}


class UnableToFetchError(Exception):
    mute = True


def _get_sandbox_token() -> str:
    try:
        import app_ctx

        _, _, sandbox_token = app_ctx.fetcher_params
        return sandbox_token or ""
    except (ImportError, AttributeError):
        return ""


def _get_transports_order() -> list[universal_fetcher.SandboxTransportType]:
    try:
        import app_ctx

        fetcher_params = app_ctx.fetcher_params[1]
    except (ImportError, AttributeError, IndexError):
        return DEFAULT_TRANSPORT_ORDER

    order = []
    for param in fetcher_params:
        param_name = param["name"]
        if param_name in FETCHER_PARAM_TO_UFETCHER_SANDBOX_TRANSPORT_TYPE:
            order.append(FETCHER_PARAM_TO_UFETCHER_SANDBOX_TRANSPORT_TYPE[param_name])
    return order


@exts.func.memoize(thread_safe=False)
def get_ufetcher() -> universal_fetcher.UniversalFetcher:
    default_retry_policy = universal_fetcher.RetryPolicy()

    http_config = universal_fetcher.HttpConfig(universal_fetcher.HttpParams(), default_retry_policy)

    sandbox_config = None

    if app_config.in_house:
        transports_order = _get_transports_order()
        sandbox_config_payload = []

        if universal_fetcher.SandboxTransportType.HTTP in transports_order:
            sandbox_config_payload.append(universal_fetcher.HttpParams())

        if universal_fetcher.SandboxTransportType.SKYNET in transports_order:
            sandbox_config_payload.append(universal_fetcher.SkynetParams())

        if universal_fetcher.SandboxTransportType.MDS in transports_order:
            sandbox_config_payload.append(universal_fetcher.MdsParams())

        sandbox_config_payload.append(default_retry_policy)
        sandbox_config = universal_fetcher.SandboxConfig(
            universal_fetcher.SandboxParams(
                oauth_token=_get_sandbox_token(), transports_order=transports_order, allow_no_auth=True
            ),
            *sandbox_config_payload,
        )

    if sandbox_config:
        cfg = universal_fetcher.FetchersConfig(sandbox_config, http_config)
    else:
        cfg = universal_fetcher.FetchersConfig(http_config)

    json_conf = cfg.build()

    return universal_fetcher.UniversalFetcher(json_conf, logger)


class UFetcherDownloader:
    def __init__(
        self,
        ufetcher: universal_fetcher.UniversalFetcher,
        parsed_uri: str,
        progress_callback: tp.Callable[..., None] | None,
        state: tp.Any,
        keep_directory_packed: bool,
    ):
        self._ufetcher = ufetcher
        self._parsed_uri = parsed_uri
        self._keep_dir_packed = keep_directory_packed

    def __call__(self, download_to: str) -> dict:
        dst_path = os.path.dirname(download_to)
        filename = os.path.basename(download_to)

        res_info = self._ufetcher.download(self._parsed_uri, dst_path, filename)
        self._handle_error(res_info)
        self._post_process_res_info(res_info, download_to)
        return res_info

    def _post_process_res_info(self, res_info: dict, download_to: str) -> None:
        # TODO: kuzmich321@ (ufetcher) YA-2028

        try:
            orig_fname = res_info['last_attempt']['result']['resource_info']['attrs']['original_filename']
            res_info['filename'] = res_info['file_name'] = orig_fname
            res_info['multifile'] = res_info['last_attempt']['result']['resource_info']['attrs']['multifile']
            should_unpack = res_info['multifile'] and not self._keep_dir_packed

            if should_unpack:
                # TODO: kuzmich321@ (ufetcher) YA-2028
                exts.archive.extract_from_tar(download_to, download_to)
        except KeyError as err:
            logger.exception("{} not present in resource info.".format(err))

    @staticmethod
    def _handle_error(res_info: dict) -> None:
        try:
            error = res_info['last_attempt']['result']['error']
            if error is not None:
                raise UnableToFetchError(error)
        except KeyError as err:
            raise UnableToFetchError(f'No attempt was made: {err}')
