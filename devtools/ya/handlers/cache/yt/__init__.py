import logging
import os
from humanfriendly import parse_size, format_size

import devtools.ya.app
import devtools.ya.core.yarg as yarg
from devtools.ya.core.common_opts import LogFileOptions
from devtools.ya.yalibrary.store.yt_store.opts_helper import parse_yt_max_cache_size
from exts.asyncthread import future
from yalibrary.store.yt_store.yt_store import YtStore2


logger = logging.getLogger(__name__)


class YtClusterOptions(yarg.Options):
    def __init__(self):
        self.yt_proxy = None
        self.yt_dir = None
        self.yt_token = None
        self.yt_token_path = "~/.yt/token"

    @staticmethod
    def consumer():
        return [
            yarg.ArgConsumer(
                ["--yt-proxy"],
                help="YT cache proxy",
                hook=yarg.SetValueHook("yt_proxy"),
            ),
            yarg.ArgConsumer(["--yt-dir"], help="YT cache cypress directory path", hook=yarg.SetValueHook("yt_dir")),
            yarg.ArgConsumer(
                ["--yt-token-path"],
                help="YT token path",
                hook=yarg.SetValueHook("yt_token_path"),
            ),
            yarg.EnvConsumer(
                "YA_YT_PROXY",
                help="YT storage proxy",
                hook=yarg.SetValueHook("yt_proxy"),
            ),
            yarg.EnvConsumer(
                "YA_YT_DIR",
                help="YT storage cypress directory pass",
                hook=yarg.SetValueHook("yt_dir"),
            ),
            yarg.EnvConsumer(
                "YA_YT_TOKEN",
                help="YT token",
                hook=yarg.SetValueHook("yt_token"),
            ),
            yarg.EnvConsumer(
                "YA_YT_TOKEN_PATH",
                help="YT token path",
                hook=yarg.SetValueHook("yt_token_path"),
            ),
        ]

    def postprocess(self):
        super().postprocess()
        if self.yt_token_path:
            self.yt_token_path = os.path.expanduser(self.yt_token_path)
            self._read_token_file()

    def _read_token_file(self):
        try:
            with open(self.yt_token_path, "r") as f:
                self.yt_token = f.read().strip()
                logger.debug("Load yt token from %s", self.yt_token_path)
        except Exception as e:
            logger.warning("Cannot load token from {}: {}".format(self.yt_token_path, str(e)))
            self.yt_token_path = None


class DryRunOption(yarg.Options):
    def __init__(self):
        self.dry_run = False

    @staticmethod
    def consumer():
        return [
            yarg.ArgConsumer(
                ["--dry-run"],
                help="Dry run",
                hook=yarg.SetConstValueHook("dry_run", True),
            ),
        ]


class StripOptions(yarg.Options):
    def __init__(self):
        self.yt_max_cache_size = None
        self.yt_store_ttl = None
        self.yt_name_re_ttls = {}

    @staticmethod
    def consumer():
        return [
            yarg.ArgConsumer(
                ["--max-cache-size"],
                help="YT storage max size",
                hook=yarg.SetValueHook("yt_max_cache_size"),
            ),
            yarg.ArgConsumer(
                ["--ttl"],
                help="YT store ttl in hours",
                hook=yarg.SetValueHook("yt_store_ttl", transform=int),
            ),
            yarg.ArgConsumer(
                ["--name-re-ttl"],
                help="Individual ttl (in hours) for an artefact name regexp. Format: 'regexp:ttl'",
                hook=StripOptions.NameReTtlHook("yt_name_re_ttls"),
            ),
        ]

    class NameReTtlHook(yarg.BaseHook):
        def __init__(self, name):
            super().__init__()
            self.name = name

        def __call__(self, to, value):
            if not hasattr(to, self.name):
                raise Exception("{0} doesn't have {1} attr".format(to, self.name))
            dct = getattr(to, self.name)
            items = value.rsplit(":", 1)
            if len(items) != 2:
                raise yarg.ArgsValidatingException(f"Value '{value}' has wrong format")
            regexp, ttl = items
            try:
                YtStore2.validate_regexp(regexp)
            except ValueError as e:
                # The exception already contains wrong regexp value so we don't need to add it to the error message
                raise yarg.ArgsValidatingException(f"Cannot compile regexp: {e}")
            dct[regexp] = int(ttl)
            setattr(to, self.name, dct)

        @staticmethod
        def need_value():
            return True

    def postprocess(self):
        super().postprocess()
        try:
            self.yt_max_cache_size = parse_yt_max_cache_size(self.yt_max_cache_size)
        except ValueError as e:
            raise yarg.ArgsValidatingException(f"Wrong yt_max_cache_size value {self.yt_max_cache_size}: {e!s}")


class PoolOption(yarg.Options):
    def __init__(self):
        self.yt_pool = None

    @staticmethod
    def consumer():
        return [
            yarg.ArgConsumer(
                ["--pool"],
                help="YT pool for operations",
                hook=yarg.SetValueHook("yt_pool"),
            ),
        ]


class DataGcOptions(yarg.Options):
    def __init__(self):
        self.data_size_per_job = 90 * (1 << 30)
        self.data_size_per_key_range = 1536 * (1 << 30)

    @staticmethod
    def parse_size_arg(v: str) -> int:
        return parse_size(v, binary=True)

    @staticmethod
    def format_size_arg(v: int) -> str:
        return format_size(v, binary=True)

    @staticmethod
    def consumer():
        return [
            yarg.ArgConsumer(
                ["--data-size-per-job"],
                help="Data size per reduce job. Change if the average duration of the jobs is out of the range of 2..5 minutes",
                hook=yarg.SetValueHook(
                    "data_size_per_job",
                    transform=DataGcOptions.parse_size_arg,
                    default_value=DataGcOptions.format_size_arg,
                ),
            ),
            yarg.ArgConsumer(
                ["--data-size-per-key-range"],
                help="Data size per one reduce operation (one key range processing). Change if the average duration of the operation is out of the range 5-20 minutes",
                hook=yarg.SetValueHook(
                    "data_size_per_key_range",
                    transform=DataGcOptions.parse_size_arg,
                    default_value=DataGcOptions.format_size_arg,
                ),
            ),
        ]


class CacheYtHandler(yarg.CompositeHandler):
    description = "Yt cache maintenance"

    def __init__(self):
        yarg.CompositeHandler.__init__(self, description=self.description)
        self["strip"] = yarg.OptsHandler(
            action=devtools.ya.app.execute(strip, respawn=devtools.ya.app.RespawnType.NONE),
            description="Apply LRU rules",
            opts=get_common_opts()
            + [
                StripOptions(),
                PoolOption(),
                DryRunOption(),
            ],
        )
        self["data-gc"] = yarg.OptsHandler(
            action=devtools.ya.app.execute(data_gc, respawn=devtools.ya.app.RespawnType.NONE),
            description="Remove orphan (not referred from the metadata table) rows from the data table",
            opts=get_common_opts()
            + [
                DataGcOptions(),
                PoolOption(),
                DryRunOption(),
            ],
        )


def get_common_opts():
    return [
        yarg.ShowHelpOptions(),
        LogFileOptions(),
        YtClusterOptions(),
    ]


def strip(params):
    yt_store = YtStore2(
        params.yt_proxy,
        params.yt_dir,
        token=params.yt_token,
        readonly=params.dry_run,
        max_cache_size=params.yt_max_cache_size,
        ttl=params.yt_store_ttl,
        name_re_ttls=params.yt_name_re_ttls,
        operation_pool=params.yt_pool,
    )
    # Run in the separate thread to allow INT signal processing in the main thread
    future(yt_store.strip)()


def data_gc(params):
    yt_store = YtStore2(
        params.yt_proxy,
        params.yt_dir,
        token=params.yt_token,
        readonly=params.dry_run,
        operation_pool=params.yt_pool,
    )

    def do_data_gc():
        yt_store.data_gc(
            data_size_per_job=params.data_size_per_job, data_size_per_key_range=params.data_size_per_key_range
        )

    # Run in the separate thread to allow INT signal processing in the main thread
    future(do_data_gc)()
