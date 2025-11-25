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
        self.yt_proxy_role = None

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
            yarg.ArgConsumer(
                ["--yt-proxy-role"],
                help="YT proxy role",
                hook=yarg.SetValueHook("yt_proxy_role"),
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
            yarg.EnvConsumer(
                "YA_YT_PROXY_ROLE",
                help="YT proxy role",
                hook=yarg.SetValueHook("yt_proxy_role"),
            ),
        ]

    def postprocess(self):
        super().postprocess()
        if self.yt_proxy is None:
            raise yarg.ArgsValidatingException("Missing mandatory --yt-proxy option")
        if self.yt_dir is None:
            raise yarg.ArgsValidatingException("Missing mandatory --yt-dir option")
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


class CreateTablesOptions(yarg.Options):
    def __init__(self):
        self.cache_version = 2
        self.replicated = False
        self.tracked = False
        self.in_memory = False
        self.mount = False
        self.ignore_existing = False
        self.metadata_tablet_count = None
        self.data_tablet_count = None

    @staticmethod
    def consumer():
        return [
            yarg.ArgConsumer(
                ["--version"],
                help="Version of the cache (2 - simple cache, 3 - with content uids support)",
                hook=yarg.SetValueHook("cache_version", transform=int),
            ),
            yarg.ArgConsumer(
                ["--replicated"],
                help="Create replicated tables",
                hook=yarg.SetConstValueHook("replicated", True),
            ),
            yarg.ArgConsumer(
                ["--tracked"],
                help="Enable replicated table tracker",
                hook=yarg.SetConstValueHook("tracked", True),
            ),
            yarg.ArgConsumer(
                ["--in-memory"],
                help="Load metadata table into RAM",
                hook=yarg.SetConstValueHook("in_memory", True),
            ),
            yarg.ArgConsumer(
                ["--mount"],
                help="Mount tables after creation",
                hook=yarg.SetConstValueHook("mount", True),
            ),
            yarg.ArgConsumer(
                ["--ignore-existing"],
                help="Ignore existing tables",
                hook=yarg.SetConstValueHook("ignore_existing", True),
            ),
            yarg.ArgConsumer(
                ["--metadata-tablet-count"],
                help="metadata table tablet count",
                hook=yarg.SetValueHook("metadata_tablet_count", transform=int),
            ),
            yarg.ArgConsumer(
                ["--data-tablet-count"],
                help="data table tablet count",
                hook=yarg.SetValueHook("data_tablet_count", transform=int),
            ),
        ]

    def postprocess(self):
        super().postprocess()
        if self.cache_version < 2 or self.cache_version > 3:
            raise yarg.ArgsValidatingException(f"Invalid version value: {self.version}")
        if self.tracked and not self.replicated:
            self.tracked = False
            logger.warning("The '--tracked' option only applies to replicated tables")
        if self.in_memory and self.replicated:
            self.in_memory = False
            logger.warning("The '--in-memory' option only applies to non-replicated tables")


class ReplicaOptions(yarg.Options):
    def __init__(self):
        self.replica_proxy = None
        self.replica_dir = None

    @staticmethod
    def consumer():
        return [
            yarg.ArgConsumer(
                ["--replica-proxy"],
                help="Replica target proxy",
                hook=yarg.SetValueHook("replica_proxy"),
            ),
            yarg.ArgConsumer(
                ["--replica-dir"],
                help="Replica target cypress directory",
                hook=yarg.SetValueHook("replica_dir"),
            ),
        ]

    def postprocess(self):
        super().postprocess()
        if self.replica_proxy is None:
            raise yarg.ArgsValidatingException("Missing mandatory --replica-proxy option")
        if self.replica_dir is None:
            raise yarg.ArgsValidatingException("Missing mandatory --replica-dir option")


class SetupReplicaOptions(yarg.Options):
    def __init__(self):
        self.enable_replica = None
        self.replica_sync_mode = None

    @staticmethod
    def consumer():
        return [
            yarg.ArgConsumer(
                ["--sync"],
                help="Synchronous replication mode",
                hook=yarg.SetConstValueHook("replica_sync_mode", True),
            ),
            yarg.ArgConsumer(
                ["--async"],
                help="Asynchronous replication mode",
                hook=yarg.SetConstValueHook("replica_sync_mode", False),
            ),
            yarg.ArgConsumer(
                ["--enable"],
                help="Enable replica",
                hook=yarg.SetConstValueHook("enable_replica", True),
            ),
            yarg.ArgConsumer(
                ["--disable"],
                help="Disable replica",
                hook=yarg.SetConstValueHook("enable_replica", False),
            ),
        ]


class PutStatOptions(yarg.Options):
    def __init__(self):
        self.put_stat_key = None
        self.put_stat_value_file = None

    @staticmethod
    def consumer():
        return [
            yarg.SingleFreeArgConsumer(help='key', hook=yarg.SetValueHook('put_stat_key')),
            yarg.SingleFreeArgConsumer(help='value_file', hook=yarg.SetValueHook('put_stat_value_file')),
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
        self["create-tables"] = yarg.OptsHandler(
            action=devtools.ya.app.execute(create_tables, respawn=devtools.ya.app.RespawnType.NONE),
            description="Create cache tables",
            opts=get_common_opts()
            + [
                CreateTablesOptions(),
            ],
        )
        self["mount"] = yarg.OptsHandler(
            action=devtools.ya.app.execute(mount, respawn=devtools.ya.app.RespawnType.NONE),
            description="Mount cache tables",
            opts=get_common_opts(),
        )
        self["unmount"] = yarg.OptsHandler(
            action=devtools.ya.app.execute(unmount, respawn=devtools.ya.app.RespawnType.NONE),
            description="Unmount cache tables",
            opts=get_common_opts(),
        )
        self["setup-replica"] = yarg.OptsHandler(
            action=devtools.ya.app.execute(setup_replica, respawn=devtools.ya.app.RespawnType.NONE),
            description="Create and modify replica",
            opts=get_common_opts()
            + [
                ReplicaOptions(),
                SetupReplicaOptions(),
            ],
        )
        self["remove-replica"] = yarg.OptsHandler(
            action=devtools.ya.app.execute(remove_replica, respawn=devtools.ya.app.RespawnType.NONE),
            description="Remove replica",
            opts=get_common_opts()
            + [
                ReplicaOptions(),
            ],
        )
        self["put-stat"] = yarg.OptsHandler(
            action=devtools.ya.app.execute(put_stat, respawn=devtools.ya.app.RespawnType.NONE),
            description="Put data into the stat table (auxiliary handler for yt heaters)",
            visible=False,
            opts=get_common_opts() + [PutStatOptions()],
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
        proxy_role=params.yt_proxy_role,
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
        proxy_role=params.yt_proxy_role,
        readonly=params.dry_run,
        operation_pool=params.yt_pool,
    )

    def do_data_gc():
        yt_store.data_gc(
            data_size_per_job=params.data_size_per_job, data_size_per_key_range=params.data_size_per_key_range
        )

    # Run in the separate thread to allow INT signal processing in the main thread
    future(do_data_gc)()


def create_tables(params):
    YtStore2.create_tables(
        params.yt_proxy,
        params.yt_dir,
        version=params.cache_version,
        token=params.yt_token,
        proxy_role=params.yt_proxy_role,
        replicated=params.replicated,
        tracked=params.tracked,
        in_memory=params.in_memory,
        mount=params.mount,
        ignore_existing=params.ignore_existing,
        metadata_tablet_count=params.metadata_tablet_count,
        data_tablet_count=params.data_tablet_count,
    )


def mount(params):
    YtStore2.mount(params.yt_proxy, params.yt_dir, token=params.yt_token, proxy_role=params.yt_proxy_role)


def unmount(params):
    YtStore2.unmount(params.yt_proxy, params.yt_dir, token=params.yt_token, proxy_role=params.yt_proxy_role)


def setup_replica(params):
    YtStore2.setup_replica(
        params.yt_proxy,
        params.yt_dir,
        params.replica_proxy,
        params.replica_dir,
        token=params.yt_token,
        proxy_role=params.yt_proxy_role,
        replica_sync_mode=params.replica_sync_mode,
        enable=params.enable_replica,
    )


def remove_replica(params):
    YtStore2.remove_replica(
        params.yt_proxy,
        params.yt_dir,
        params.replica_proxy,
        params.replica_dir,
        token=params.yt_token,
        proxy_role=params.yt_proxy_role,
    )


def put_stat(params):
    with open(params.put_stat_value_file, "rb") as f:
        value = f.read()
    yt_store = YtStore2(
        params.yt_proxy,
        params.yt_dir,
        token=params.yt_token,
        proxy_role=params.yt_proxy_role,
        readonly=False,
    )
    yt_store.put_stat(params.put_stat_key, value)
