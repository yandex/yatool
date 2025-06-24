import os
import stat
import logging
import typing as tp
import functools


import devtools.libs.universal_fetcher.py as universal_fetcher
import devtools.ya.core.report
import exts.archive
import exts.deepget as deepget
import exts.windows as windows
import exts.http_client
import app_config

logger = logging.getLogger(__name__)


DEFAULT_TRANSPORT_ORDER = [
    universal_fetcher.SandboxTransportType.HTTP,
    universal_fetcher.SandboxTransportType.SKYNET,
    universal_fetcher.SandboxTransportType.MDS,
]

FETCHER_PARAM_TO_UFETCHER_SANDBOX_TRANSPORT_TYPE = {
    "custom": universal_fetcher.SandboxTransportType.EXTERNAL_PROGRAM_FETCHER,
    "proxy": universal_fetcher.SandboxTransportType.HTTP,
    "sandbox": universal_fetcher.SandboxTransportType.HTTP,
    "skynet": universal_fetcher.SandboxTransportType.SKYNET,
    "mds": universal_fetcher.SandboxTransportType.MDS,
}

DEFAULT_SKY_PATH = "/skynet/tools/sky"


class UnableToFetchError(Exception):
    mute = True


def _get_sandbox_token() -> str:
    try:
        import app_ctx

        _, _, sandbox_token = app_ctx.fetcher_params
        return sandbox_token or ""
    except (ImportError, AttributeError):
        return ""


def _get_docker_config() -> str:
    try:
        import app_ctx

        docker_config_path = app_ctx.docker_config_path
        return docker_config_path or ""
    except (ImportError, AttributeError):
        return ""


@functools.cache
def _get_external_program_fetcher_cmd() -> list[str]:
    try:
        import app_ctx

        cmd = app_ctx.fetcher_params[0]
        if not cmd:
            return []

        if not os.path.exists(cmd):
            logger.debug(f"External program fetcher does not exist: {cmd}")
            return []

        return [cmd]
    except (ImportError, AttributeError, IndexError):
        logger.debug("External program fetcher command: empty")
        return []


def _get_transports_order() -> list[universal_fetcher.SandboxTransportType]:
    try:
        import app_ctx

        fetcher_params = app_ctx.fetcher_params[1]
        if len(fetcher_params) == 0:
            return DEFAULT_TRANSPORT_ORDER
    except (ImportError, AttributeError, IndexError):
        return DEFAULT_TRANSPORT_ORDER

    order = []
    for param in fetcher_params:
        param_name = param["name"]
        if param_name in FETCHER_PARAM_TO_UFETCHER_SANDBOX_TRANSPORT_TYPE and param_name not in order:
            if any(
                [
                    param_name == "custom" and not _get_external_program_fetcher_cmd(),
                    param_name == "skynet" and not os.path.exists(DEFAULT_SKY_PATH),
                ]
            ):
                continue
            order.append(FETCHER_PARAM_TO_UFETCHER_SANDBOX_TRANSPORT_TYPE[param_name])
    return order


@functools.cache
def get_ufetcher(should_tar_output: bool = True) -> universal_fetcher.UniversalFetcher:
    # 2.3 + 5 + 12 + 27 + 64 + 148 + 340 + 360
    default_retry_policy = universal_fetcher.RetryPolicy(
        max_retry_count=9,
        initial_delay_ms=1_000,
        use_fixed_delay=False,
        max_delay_ms=360_000,
        backoff_multiplier=2.3,
    )

    http_params = universal_fetcher.HttpParams(
        user_agent=exts.http_client.make_user_agent(),
        socket_timeout_ms=30_000,
        connect_timeout_ms=30_000,
    )

    sandbox_config = None
    docker_config = None

    configs = []

    docker_config_path = _get_docker_config()
    if docker_config_path:
        docker_config = universal_fetcher.DockerConfig(
            universal_fetcher.DockerParams(
                auth_json_file=docker_config_path,
            ),
            default_retry_policy,
        )

    if app_config.in_house:
        transports_order = _get_transports_order()
        sandbox_params = universal_fetcher.SandboxParams(
            oauth_token=_get_sandbox_token(),
            transports_order=transports_order,
            allow_no_auth=True,
            should_tar_output=should_tar_output,
        )
        kwargs = {
            "sandbox_params": sandbox_params,
            "retry_policy": default_retry_policy,
        }

        if universal_fetcher.SandboxTransportType.HTTP in transports_order:
            kwargs["http_params"] = http_params

        if universal_fetcher.SandboxTransportType.SKYNET in transports_order:
            kwargs["skynet_params"] = universal_fetcher.SkynetParams()

        if universal_fetcher.SandboxTransportType.MDS in transports_order:
            kwargs["mds_params"] = universal_fetcher.MdsParams(socket_timeout_ms=30_000, connect_timeout_ms=30_000)

        if universal_fetcher.SandboxTransportType.EXTERNAL_PROGRAM_FETCHER in transports_order:
            if cmd := _get_external_program_fetcher_cmd():
                kwargs["external_program_fetcher_params"] = universal_fetcher.ExternalProgramFetcherParams(cmd=cmd)

        sandbox_config = universal_fetcher.SandboxConfig(**kwargs)

    http_config = universal_fetcher.HttpConfig(
        http_params,
        default_retry_policy,
    )
    configs.append(http_config)

    if sandbox_config:
        configs.append(sandbox_config)

    if docker_config:
        configs.append(docker_config)

    cfg = universal_fetcher.FetchersConfig(*configs)

    json_conf = cfg.build()

    return universal_fetcher.UniversalFetcher(json_conf, logger)


class UFetcherDownloader:
    def __init__(
        self,
        ufetcher: universal_fetcher.UniversalFetcher,
        parsed_uri: str,
        progress_callback: tp.Callable[[int, int], None] | None,
        keep_directory_packed: bool,
        resource_info: dict | None,
        resource_type: tp.Literal['sbr', 'http', 'https', 'docker'],
    ):
        self._ufetcher = ufetcher
        self._parsed_uri = parsed_uri
        self._progress_callback = progress_callback
        self._keep_dir_packed = keep_directory_packed
        self._default_resource_info = resource_info
        self._resource_type = resource_type

    def __call__(self, download_to: str) -> dict:
        res_info = self._download(download_to)
        self._send_result_to_telemetry_if_needed(res_info)
        self.handle_error(res_info)
        self._post_process(res_info, download_to)
        return res_info

    def _download(self, download_to: str) -> dict:
        dst_path = os.path.dirname(download_to)
        filename = os.path.basename(download_to)

        try:
            return self._ufetcher.download(self._parsed_uri, dst_path, filename, self._progress_callback, 200)
        finally:
            if hasattr(self._progress_callback, 'finalize'):
                self._progress_callback.finalize()

    def _post_process(self, res_info: dict, download_to: str) -> None:
        res_info_attrs = deepget.deepget(res_info, ("last_attempt", "result", "resource_info", "attrs"))

        orig_fname = res_info_attrs.get('original_filename', None)
        res_info['filename'] = res_info['file_name'] = orig_fname

        is_multifile = res_info_attrs.get('multifile', False)
        res_info['multifile'] = is_multifile

        resource_id = res_info_attrs.get('id', None)
        res_info['id'] = resource_id

        is_executable = deepget.deepget(res_info, ("last_attempt", "result", "resource_info")).get('executable', False)

        if self._default_resource_info:
            res_info['filename'] = res_info['file_name'] = self._default_resource_info.get('file_name', orig_fname)
            res_info['id'] = self._default_resource_info.get('id', resource_id)

        should_unpack = is_multifile and not self._keep_dir_packed
        if should_unpack:
            try:
                exts.archive.extract_from_tar(download_to, download_to)
            except Exception as err:
                logger.debug("Couldn't extract from tar: %s" % err)

        self.update_permissions(download_to, is_executable, self._resource_type)

    @staticmethod
    def handle_error(res_info: dict) -> None:
        try:
            status = res_info['last_attempt']['result']['status']
            if status != universal_fetcher.OK:
                error = res_info['last_attempt']['result']['error']
                logger.debug("universal fetcher returned the following result: %s", res_info)
                raise UnableToFetchError(f"universal fetcher got error: {error}")

        except KeyError as err:
            raise UnableToFetchError(f'No attempt was made: {err}')

    @classmethod
    def update_permissions(cls, dst: str, executable: bool, resource_type: str) -> None:
        """
        Update permissions on downloaded result
        - Remove write permissions for files on non-Windows
        - Set write permissions for files on Windows
        - Set write permissions for directories
        - If resource is executable, set executable premissions if result is file
        """
        if resource_type.startswith("sbr"):
            os.chmod(dst, os.stat(dst).st_mode | stat.S_IWUSR)

            if os.path.isdir(dst):
                if executable:
                    logger.debug("Got exec premissions for dir: %s", dst)

                for root, dirs, files in os.walk(dst):
                    for file_name in files:
                        extracted_path = os.path.join(root, file_name)
                        perms = cls._get_sandbox_file_permissions(extracted_path, not dirs and executable)
                        os.chmod(extracted_path, perms)

                    for dir_name in dirs:
                        extracted_dir = os.path.join(root, dir_name)
                        os.chmod(extracted_dir, os.stat(extracted_dir).st_mode | stat.S_IWUSR)

            else:
                perms = cls._get_sandbox_file_permissions(dst, executable)
                os.chmod(dst, perms)
        elif resource_type.startswith("http"):
            os.chmod(dst, stat.S_IRUSR | stat.S_IWUSR | stat.S_IRGRP | stat.S_IROTH)

    @staticmethod
    def _get_sandbox_file_permissions(dst: str, executable: bool) -> int:
        perms = os.stat(dst).st_mode

        if windows.on_win():
            perms |= stat.S_IWUSR
        else:
            perms &= ~(stat.S_IWUSR | stat.S_IWGRP | stat.S_IWOTH)

        if executable:
            logger.debug("Setting exec premissions for %s", dst)
            perms |= stat.S_IXUSR | stat.S_IXGRP

        return perms

    @staticmethod
    def _send_result_to_telemetry_if_needed(res_info: dict) -> None:
        history_len = len(res_info["history"]) if "history" in res_info else 0
        transport_history = deepget.deepget(res_info, ("last_attempt", "result", "transport_history"))
        transport_history_len = len(transport_history) if transport_history else 0

        more_then_one_attempt_was_made = history_len > 0 or transport_history_len > 1
        if more_then_one_attempt_was_made:
            devtools.ya.core.report.telemetry.report(
                devtools.ya.core.report.ReportTypes.UNIVERSAL_FETCHER, {"universal_fetcher_result": res_info}
            )
