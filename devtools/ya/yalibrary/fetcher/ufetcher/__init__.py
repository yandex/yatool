import os
import logging
import typing as tp


import devtools.experimental.universal_fetcher.py as universal_fetcher
import exts.func
import exts.archive
import app_config

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)


class UnableToFetchError(Exception):
    mute = True


def _get_sandbox_token() -> str:
    try:
        import app_ctx

        _, _, sandbox_token = app_ctx.fetcher_params
        return sandbox_token or ""
    except (ImportError, AttributeError):
        return ""


def _get_transports_order() -> list:
    try:
        import app_ctx

        fetcher_params = app_ctx.fetcher_params[1]
    except (ImportError, AttributeError, IndexError):
        return ["http", "skynet", "mds"]

    fetcher_param_to_ufetcher_sandbox_transport_type = {
        "proxy": "http",
        "sandbox": "http",
        "skynet": "skynet",
        "mds": "mds",
    }

    order = []
    for param in fetcher_params:
        param_name = param["name"]
        if param_name in fetcher_param_to_ufetcher_sandbox_transport_type:
            order.append(fetcher_param_to_ufetcher_sandbox_transport_type[param_name])
    return order


@exts.func.memoize(thread_safe=False)
def get_ufetcher() -> universal_fetcher.UniversalFetcher:
    default_retry_policy = universal_fetcher.RetryPolicy()

    sandbox_config = None

    if app_config.in_house:
        transports_order = _get_transports_order()
        sandbox_config_payload = []

        if "http" in transports_order:
            sandbox_config_payload.append(universal_fetcher.HttpParams())

        if "skynet" in transports_order:
            sandbox_config_payload.append(universal_fetcher.SkynetParams())

        if "mds" in transports_order:
            sandbox_config_payload.append(universal_fetcher.MdsParams())

        sandbox_config_payload.append(default_retry_policy)
        sandbox_config = universal_fetcher.SandboxConfig(
            universal_fetcher.SandboxParams(
                oauth_token=_get_sandbox_token(), transports_order=transports_order, allow_no_auth=True
            ),
            *sandbox_config_payload,
        )

    if sandbox_config:
        cfg = universal_fetcher.FetchersConfig(
            sandbox_config,
            universal_fetcher.HttpConfig(
                universal_fetcher.HttpParams(),
                default_retry_policy,
            ),
        )
    else:
        cfg = universal_fetcher.FetchersConfig(
            universal_fetcher.HttpConfig(
                universal_fetcher.HttpParams(),
                default_retry_policy,
            ),
        )

    json_conf = cfg.build()

    return universal_fetcher.UniversalFetcher(json_conf, logger)


class UFetcherDownloader:
    def __init__(
        self,
        ufetcher: universal_fetcher.UniversalFetcher,
        parsed_uri: str,
        progress_callback: tp.Any,
        state: tp.Any,
        keep_directory_packed: bool,
    ):
        self._ufetcher = ufetcher
        self._parsed_uri = parsed_uri
        self._keep_dir_packed = keep_directory_packed

    def __call__(self, download_to: str) -> dict:
        # TODO: kuzmich321@ (ufetcher) fix this when fully migrated to ufetcher
        dst_path = os.path.dirname(download_to)
        filename = os.path.basename(download_to)

        res_info = self._ufetcher.download(self._parsed_uri, dst_path, filename)
        self._handle_error(res_info)
        self._post_process_res_info(res_info, download_to)
        return res_info

    def _post_process_res_info(self, res_info: dict, download_to: str) -> None:
        # TODO: kuzmich321@ (ufetcher) make universal fetcher return full resource info

        try:
            orig_fname = res_info['last_attempt']['result']['resource_info']['attrs']['original_filename']
            res_info['filename'] = res_info['file_name'] = orig_fname
            res_info['multifile'] = res_info['last_attempt']['result']['res_info']['attrs']['multifile']
            should_unpack = res_info['multifile'] and not self._keep_dir_packed

            if should_unpack:
                # TODO: kuzmich321@ (ufetcher) do we need to make it more consisent? extract to tmp + os.move
                exts.archive.extract_from_tar(download_to, download_to)
        except KeyError as err:
            logger.debug("{} not present in resource info.".format(err))

    @staticmethod
    def _handle_error(res_info: dict) -> None:
        try:
            error = res_info['last_attempt']['result']['error']
            if error is not None:
                raise UnableToFetchError(error)
        except KeyError:
            raise UnableToFetchError("No attempt was made.")
