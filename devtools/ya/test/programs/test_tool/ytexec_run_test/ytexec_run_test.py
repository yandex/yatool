# coding: utf-8

import argparse
import datetime
import json
import logging
import os
import sys
import time
from hashlib import new
import six

from dateutil import parser as date_parser
from devtools.ya.test.system import process
from devtools.ya.test import const
from devtools.ya.test.util import shared

import devtools.ya.core.config
import exts.archive
import exts.fs
import exts.func
import yt.yson as yson

from devtools.ya.test.programs.test_tool.run_test import run_test
from devtools.ya.test.programs.test_tool.lib import secret

logger = logging.getLogger(__name__)

OUTPUT_TTL = 1 * 24 * 60 * 60 * 1000**3  # 1 day in nanoseconds
BLOB_TTL = 1 * 24 * 60 * 60 * 1000**3  # 1 day in nanoseconds
DEVTOOLS_OUTPUT_TTL = 2 * 60 * 60 * 1000**3
DEFAULT_TIMEOUT = 60 * 60 * 1000**3  # 1 hour
DEFAULT_MEMORY_LIMIT = 4 * 1 << 30  # 4 gb
DEFAULT_TMPFS_LIMIT = 4 * 1 << 30  # 4 gb
DEFAULT_POOL = "devtools"
DEFAULT_CYPRESS_ROOT = "//home/devtools/tmp/ytexec"
DEVTOOLS_CYPRESS_PREFIX = "//home/devtools"
ENV_SKIP = {
    'ARC_TOKEN',
    'HOME',
    'PORT_SYNC_PATH',
    'PWD',
    'USER',
    'WINEPREFIX',
    'YA_TOKEN',
    'YT_TOKEN',
    const.DISTBUILD_STATUS_REPORT_ENV_NAME,
}
DIRS_TO_UPLOAD = {"sandbox-storage", "yt_run_test"}
DIRS_TO_SKIP = {"environment"}
FILES_TO_SKIP = {"yt_run_test.log"}
CPU_LIMIT = 1
TIMEOUT_DELAY = 60
TIMEOUT_SIGKILL_DELAY = 45


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-tar", required=True)
    parser.add_argument("--ytexec-tool", required=True)
    parser.add_argument("--yt-spec-file", dest='yt_spec_files', action='append', default=[])
    parser.add_argument("--ytexec-outputs", dest='outputs', action='append', default=[])
    parser.add_argument("--description")
    parser.add_argument("--yt-token-path")
    parser.add_argument("--cpu", type=int)
    parser.add_argument("--network")
    parser.add_argument("--ram-disk", type=int)
    parser.add_argument("--ram", type=int)
    parser.add_argument("--ytexec-node-timeout")
    parser.add_argument("run_test_command", nargs=argparse.REMAINDER)
    args = parser.parse_args()

    args.output_dir = os.path.splitext(args.output_tar)[0]

    args.description = f'[TS-{os.environ["TEST_NODE_SUITE_UID"]}] {args.description}'

    # Steal args from run_test (skipping test_tool binary path and first argument - run_test handler)
    test_args = run_test.parse_args(args.run_test_command[2:])

    for field in dir(test_args):
        if field.startswith('_'):
            continue
        assert not hasattr(args, field), 'Args collision for {}'.format(field)
        setattr(args, field, getattr(test_args, field))

    return args


def get_dir_size(directory_path):
    total_size = 0
    for dirpath, dirnames, filenames in os.walk(directory_path):
        for f in filenames:
            fp = os.path.join(dirpath, f)
            if not os.path.islink(fp):
                total_size += os.path.getsize(fp)
    return total_size


def setup_yt_token(args):
    """
    setup yt token variable and returns variable name
    """
    if os.environ.get('YT_TOKEN', None):
        return 'YT_TOKEN'
    else:
        yt_token_var_name = "YTEXEC_YT_TOKEN"
        if "YA_COMMON_YT_TOKEN" in os.environ:
            decoded_yt_token = secret.decode_dist_secret(os.environ["YA_COMMON_YT_TOKEN"])
            logger.debug("Use YA_COMMON_YT_TOKEN variable")
            os.environ[yt_token_var_name] = decoded_yt_token
        elif 'YT_TOKEN_PATH' in os.environ:
            logger.debug("Use provided YT_TOKEN_PATH variable: %s", os.environ['YT_TOKEN_PATH'])
            with open(os.environ["YT_TOKEN_PATH"], 'r') as token_file:
                os.environ[yt_token_var_name] = token_file.read().strip()
        elif args.yt_token_path and os.path.exists(args.yt_token_path):
            logger.debug("Use provided yt_token_path: %s", args.yt_token_path)
            with open(args.yt_token_path, 'r') as token_file:
                os.environ[yt_token_var_name] = token_file.read().strip()
        else:
            logger.debug("Use oauth to get yt_token")
            from yalibrary import oauth

            token = oauth.get_token(devtools.ya.core.config.get_user())
            if token is not None:
                os.environ[yt_token_var_name] = token
            else:
                logger.error("Can't setup yt token. specify YT_TOKEN_PATH variable in your environment")
                return None
        return yt_token_var_name


class YtexecConfig(object):
    class Exec(object):
        def __init__(
            self,
            prepared_file="",
            result_file="",
            readme_file="",
            download_script="",
            exec_log="",
            job_log="",
            yt_token_env="",
        ):
            self.prepared_file = prepared_file
            self.result_file = result_file
            self.readme_file = readme_file
            self.download_script = download_script
            self.exec_log = exec_log
            self.job_log = job_log
            if yt_token_env is not None:
                self.yt_token_env = yt_token_env

    class Operation(object):
        def __init__(
            self,
            cluster="",
            pool="",
            title="",
            cypress_root="",
            output_ttl=OUTPUT_TTL,
            blob_ttl=OUTPUT_TTL,
            coordinate_upload=True,
            cpu_limit=CPU_LIMIT,
            memory_limit=DEFAULT_MEMORY_LIMIT,
            timeout=DEFAULT_TIMEOUT,
            enable_porto=True,
            enable_network=False,
            spec_patch=None,
            task_patch=None,
            tmpfs_size=DEFAULT_MEMORY_LIMIT,
            run_as_root=False,
            enable_research_fallback=True,
        ):
            self.cluster = cluster
            self.pool = pool
            self.title = title
            self.cypress_root = cypress_root
            self.output_ttl = output_ttl
            self.blob_ttl = blob_ttl
            self.coordinate_upload = coordinate_upload
            self.cpu_limit = cpu_limit
            self.memory_limit = memory_limit
            self.timeout = timeout
            self.enable_porto = enable_porto
            self.enable_network = enable_network
            self.spec_patch = spec_patch
            self.task_patch = task_patch
            self.tmpfs_size = tmpfs_size
            self.run_as_root = run_as_root
            self.enable_research_fallback = enable_research_fallback

    class Cmd(object):
        def __init__(self, args=None, cwd="", environ=None, sigusr2_timeout=0, sigquit_timeout=0, sigkill_timeout=0):
            self.args = args
            self.cwd = cwd
            self.environ = environ
            self.sigusr2_timeout = sigusr2_timeout
            self.sigquit_timeout = sigquit_timeout
            self.sigkill_timeout = sigkill_timeout

    class Fs(object):
        def __init__(
            self,
            upload_file=None,
            upload_hashed_file=None,
            upload_tar=None,
            upload_hashed_tar=None,
            upload_structure=None,
            outputs=None,
            stdout_file="",
            stderr_file="",
            yt_outputs=None,
            coredump_dir="",
            ext4_dirs=None,
            download=None,
        ):
            self.upload_file = upload_file
            self.upload_hashed_file = upload_hashed_file
            self.upload_tar = upload_tar
            self.upload_hashed_tar = upload_hashed_tar
            self.upload_structure = upload_structure
            self.outputs = outputs
            self.stdout_file = stdout_file
            self.stderr_file = stderr_file
            self.yt_outputs = yt_outputs
            self.coredump_dir = coredump_dir
            self.ext4_dirs = ext4_dirs
            self.download = download

    def __init__(self, exec_fields, cmd_fields, operation_fields, fs_fields):
        self.exec_fields = exec_fields
        self.cmd_fields = cmd_fields
        self.operation_fields = operation_fields
        self.fs_fields = fs_fields

    def __str__(self):
        # Yson dumps returns bytes by default, so we need to explicitly convert it to string
        return six.ensure_str(
            yson.dumps(
                {
                    "exec": vars(self.exec_fields),
                    "operation": vars(self.operation_fields),
                    "cmd": vars(self.cmd_fields),
                    "fs": vars(self.fs_fields),
                },
                yson_format="pretty",
            )
        )

    def dump(self, path):
        with open(path, 'w') as afile:
            afile.write(str(self))


def remove_nested_dirs(paths):
    paths = [os.path.normpath(x) for x in paths]
    paths = sorted(paths)
    res = [paths[0]]
    prev_path = paths[0]
    for i in range(1, len(paths)):
        if prev_path == paths[i] or paths[i].startswith(prev_path + "/"):
            continue
        prev_path = paths[i]
        res.append(paths[i])
    return res


def get_environ(user_yt_spec):
    additional_env = user_yt_spec.get("task_spec", {}).get("environment", {})
    env_copy = os.environ.copy()
    env_copy.update(additional_env)
    for key in env_copy.keys():
        if key in ["TMP", "TMPDIR", "TEMPDIR", "TEMP"]:
            env_copy[key] = "/var/tmp"
    env_copy = ["{}={}".format(k, v) for k, v in six.iteritems(env_copy) if k not in ENV_SKIP]
    return env_copy


def update_operation_fields(operation_fields, user_operation_fields):
    for k, v in six.iteritems(user_operation_fields):
        if not hasattr(operation_fields, k):
            logger.warning("Config.operation has no %s attribute", k)
        setattr(operation_fields, k, v)


def get_all_files(path):
    res = []
    for root, dirs, files in os.walk(path):
        for f in files:
            if f in FILES_TO_SKIP:
                continue
            res.append(os.path.abspath(os.path.join(root, f)))
    return res


def get_top_level_files(path):
    res = []
    for f in os.listdir(path):
        if f in FILES_TO_SKIP:
            continue
        res.append(os.path.abspath(os.path.join(path, f)))
    return res


def discover_build_root_files(args):
    build_root_files = []
    for f in os.listdir(args.build_root):
        if f in DIRS_TO_SKIP:
            continue
        if f in DIRS_TO_UPLOAD:
            build_root_files.extend(get_top_level_files(f))
            continue
        if os.path.isdir(f):
            build_root_files.extend(get_all_files(f))
        else:
            build_root_files.append(os.path.abspath(f))
    return build_root_files


def get_dirs_structure(args):
    dirs_structure = []
    for f in os.listdir(args.build_root):
        if f in DIRS_TO_UPLOAD:
            continue
        if os.path.isdir(f):
            dirs_structure.append(os.path.abspath(f))
    return dirs_structure


def infer_tar_md5(dirs):
    hashed_dirs = []
    bare_dirs = []
    for dir in dirs:
        resource_info_path = os.path.join(dir, "resource_info.json")
        if os.path.exists(resource_info_path):
            resource_info = json.loads(open(resource_info_path).read())

            resource_structure_info = []
            for root, _, files in os.walk(dir):
                for f in files:
                    file_relpath = os.path.relpath(root, dir)
                    resource_structure_info.append(os.path.join(file_relpath, f))
            resource_structure_info = sorted(resource_structure_info)

            resource_id = resource_info[u"id"]

            # Update this string in case of cache poisoning.
            hash_salt = ""

            cache_md5 = new('md5')
            cache_md5.update(six.ensure_binary(hash_salt))
            cache_md5.update(six.ensure_binary("-".join(resource_structure_info)))
            cache_md5.update(six.ensure_binary(str(resource_id)))

            hashed_dirs.append({"path": dir, "md5": cache_md5.hexdigest()})
        else:
            bare_dirs.append(dir)
    return bare_dirs, hashed_dirs


def validate_operaion_fields(operation_fields):
    if operation_fields.cypress_root.startswith(DEVTOOLS_CYPRESS_PREFIX):
        logger.error(
            "You are using devtools cypress_root. The TTL of data in this path is 2 hours\nUse documentation https://docs.yandex-team.ru/devtools/test/yt#autocheck to move tests "
            "to project pool"
        )
        operation_fields.output_ttl = DEVTOOLS_OUTPUT_TTL
        operation_fields.blob_ttl = DEVTOOLS_OUTPUT_TTL


def generate_config_sections(args):
    exec_logs_dir = args.output_dir
    exts.fs.ensure_dir(exec_logs_dir)
    work_dir = run_test.get_test_work_dir(args)
    user_yt_spec, user_operation_fields = get_user_yt_spec(args.yt_spec_files)

    hdd_path = os.path.join(work_dir, "hdd_path")
    os.environ["HDD_PATH"] = hdd_path

    yt_output_dir = os.path.join(work_dir, "yt_output")
    os.environ["YT_OUTPUT"] = yt_output_dir

    # filling cmd section
    env_copy = get_environ(user_yt_spec)
    cmd_fields = YtexecConfig.Cmd(
        args=args.run_test_command,
        cwd=os.getcwd(),
        environ=env_copy,
        sigkill_timeout=(args.timeout + TIMEOUT_SIGKILL_DELAY) * 1000**3,
        sigquit_timeout=(args.timeout + TIMEOUT_DELAY) * 1000**3,
        sigusr2_timeout=(args.timeout + TIMEOUT_DELAY) * 1000**3,
    )

    # filling exec section
    exec_fields = YtexecConfig.Exec(
        job_log=os.path.join(exec_logs_dir, "job.log"),
        exec_log=os.path.join(exec_logs_dir, "exec.log"),
        download_script=os.path.join(exec_logs_dir, "download.sh"),
        readme_file=os.path.join(exec_logs_dir, "README.md"),
        result_file=os.path.join(exec_logs_dir, "result.json"),
        prepared_file=os.path.join(exec_logs_dir, "prepared.json"),
        yt_token_env=setup_yt_token(args),
    )

    # filling operation section
    operation_fields = YtexecConfig.Operation(
        pool=DEFAULT_POOL,
        title=args.description,
        blob_ttl=BLOB_TTL,
        cluster="hahn",
        coordinate_upload=True,
        cpu_limit=args.cpu,
        cypress_root=DEFAULT_CYPRESS_ROOT,
        enable_network=args.network == "full",
        enable_porto=True,
        memory_limit=args.ram * 1 << 30,
        tmpfs_size=args.ram_disk * 1 << 30 or DEFAULT_MEMORY_LIMIT,
        # real timeout = timeout + 5 minutes https://a.yandex-team.ru/arc/trunk/arcadia/yt/go/ytrecipe/internal/job/config.go?rev=7242841&blame=true#L12
        timeout=args.timeout * 1000**3,
        output_ttl=OUTPUT_TTL,
        spec_patch=user_yt_spec.get('operation_spec'),
        task_patch=user_yt_spec.get('task_spec'),
    )
    update_operation_fields(operation_fields, user_operation_fields)
    validate_operaion_fields(operation_fields)
    # filling fs section
    global_resources_paths = get_global_resources_paths(args)
    extra_tools = (
        '/gdb/bin/gdb',
        '/python',
    )
    extra_inputs = set()
    for arg in args.run_test_command:
        for extra_tool in extra_tools:
            if arg.endswith(extra_tool) and not arg.startswith(args.source_root):
                extra_inputs.add(arg[: -len(extra_tool)])  # upload full resource dir

    build_root_files = discover_build_root_files(args)
    upload_structure = get_dirs_structure(args)
    test_tool_bin_path = args.run_test_command[0]
    upload_data = remove_nested_dirs(
        build_root_files + args.test_related_paths + global_resources_paths + [test_tool_bin_path]
    )
    upload_dirs = list(extra_inputs)
    upload_files = []
    for data in upload_data:
        if os.path.isdir(data):
            upload_dirs.append(data)
        else:
            upload_files.append(data)

    coredump_dir = os.path.join(work_dir, "coredump_dir")
    yt_outputs = [hdd_path, yt_output_dir]

    download_map = {}
    for o in yt_outputs + [coredump_dir]:
        download_map[o] = os.path.basename(o)

    outputs = args.outputs

    upload_dirs, upload_hashed_dirs = infer_tar_md5(upload_dirs)

    fs_fields = YtexecConfig.Fs(
        ext4_dirs=[hdd_path],
        yt_outputs=yt_outputs,
        download=download_map,
        outputs=outputs,
        upload_tar=upload_dirs,
        upload_hashed_tar=upload_hashed_dirs,
        upload_structure=upload_structure,
        upload_file=list(set(upload_files)),
        coredump_dir=coredump_dir,
        stdout_file=os.path.join(exec_logs_dir, "stdout"),
        stderr_file=os.path.join(exec_logs_dir, "stderr"),
    )

    return exec_fields, cmd_fields, operation_fields, fs_fields


def prepare_toolchain_subset(toolchain_path, target_path):
    tools = {
        # test machinery uses symbolizer to resolve addresses from backtraces
        'bin/llvm-symbolizer': [
            'lib/libc++.so.1',
            'lib/libc++.so.1.0',
            'lib/libc++abi.so.1',
            'lib/libc++abi.so.1.0',
            'lib/libunwind.so.1',
            'lib/libunwind.so.1.0',
        ],
    }

    def install(filename):
        dst = os.path.join(target_path, filename)
        exts.fs.ensure_dir(os.path.dirname(dst))
        exts.fs.hardlink_or_copy(os.path.join(toolchain_path, filename), dst)

    for tool, libs in tools.items():
        install(tool)
        for lib in libs:
            if os.path.exists(os.path.join(toolchain_path, lib)):
                install(lib)


def get_wd(path):
    return os.path.join(os.path.abspath('yt_run_test'), path)


def setup_env(params):
    exts.fs.ensure_dir(params.output_dir)
    log_path = os.path.join(params.output_dir, 'yt_run_test.log')
    shared.setup_logging(logging.ERROR, log_path)


def get_user_yt_spec(filenames):
    res = {}
    user_operation_fields = {}
    for filename in filenames:
        logger.debug("Loading user specified YT spec: %s", filename)
        with open(filename, 'rb') as afile:
            string_data = afile.read()
            try:
                data = json.loads(string_data)
            except ValueError:
                data = yson.loads(string_data)
        for field_name, value in data.items():
            if field_name in ['operation_spec', 'task_spec']:
                res[field_name] = res.get(field_name, {})
                res[field_name].update(value)
            else:
                user_operation_fields[field_name] = value

    return res, user_operation_fields


def dump_test_info(params, metrics=None, error=None, status=const.Status.FAIL):
    logger.debug("Loading suite results")
    logs_dir = os.path.relpath(params.output_dir, params.build_root)
    logs_dir = "$(BUILD_ROOT)/{}".format(logs_dir)
    work_dir = run_test.get_test_work_dir(params)
    suite = run_test.generate_suite(params, work_dir)

    trace_report = os.path.join(work_dir, const.TRACE_FILE_NAME)
    suite.load_run_results(trace_report, relaxed=True)
    chunk = suite.chunk

    if error:
        chunk.add_error("[[bad]]{}".format(error), status=status)
    if metrics:
        chunk.metrics.update(metrics)

    for t in chunk.tests:
        t.logs.update({'operation_logs': logs_dir})
    chunk.logs.update({'operation_logs': logs_dir})
    chunk.logs.update({'download_script': os.path.join(logs_dir, "download.sh")})

    def fix_log_path(path):
        prefix = "$(BUILD_ROOT)"
        if not path.startswith(prefix):
            logger.warning("Fixing log path: %s", path)
            fixed_path = path.replace(params.build_root, prefix)
            logger.info("Fixed path: %s", fixed_path)
            return fixed_path
        else:
            return path

    # Fix paths to logs if something went wrong in YT and run_test node crashed
    for t in chunk.tests:
        for name, path in t.logs.items():
            if name and path:
                t.logs[name] = fix_log_path(path)
            else:
                logger.error("Broken log entry: %r %r", name, path)

    for name, path in chunk.logs.items():
        if name and path:
            chunk.logs[name] = fix_log_path(path)
        else:
            logger.error("Broken log entry: %r %r", name, path)

    logger.debug("Dumping suite results")
    suite.generate_trace_file(trace_report)
    return suite


def get_global_resources_paths(args):
    return list(args.global_resources.values())


def print_file(filename):
    if os.path.exists(filename):
        with open(filename, 'r') as afile:
            sys.stderr.write(afile.read())
    else:
        logger.warning("{} file is not found after ytexec run".format(os.path.basename(filename)))


@exts.func.memoize()
def load_prepared_info(output_dir):
    prepared_path = os.path.join(output_dir, "prepared.json")
    if os.path.exists(prepared_path):
        with open(prepared_path, 'r') as afile:
            return json.load(afile)
    return {}


def extract_operation_id(output_dir):
    return load_prepared_info(output_dir).get("operation_id", None)


def extract_operation_url(output_dir):
    return load_prepared_info(output_dir).get("operation_url", None)


def get_operation_creation_time(output_dir):
    exec_log_path = os.path.join(output_dir, "exec.log")
    op_id = extract_operation_id(output_dir)
    # Check exec log only when operation was registered as started
    if op_id:
        with open(exec_log_path, 'r') as afile:
            for line in afile:
                entry = json.loads(line)
                if entry.get("method", None) == "start_operation":
                    return entry.get("ts", None)


def get_upload_size(ytexec_config):
    total_size = 0
    for f in ytexec_config.fs_fields.upload_file:
        total_size += os.path.getsize(f)
    for d in ytexec_config.fs_fields.upload_tar:
        total_size += get_dir_size(d)
    for dir_hash_map in ytexec_config.fs_fields.upload_hashed_tar:
        total_size += get_dir_size(dir_hash_map["path"])
    return total_size


def process_test_results(args, ytexec_config, exit_code, start_time):
    stderr_path = ytexec_config.fs_fields.stderr_file
    result_path = ytexec_config.exec_fields.result_file

    print_file(stderr_path)
    yt_metrics = {
        "ytexec_upload_files_count": len(ytexec_config.fs_fields.upload_tar) + len(ytexec_config.fs_fields.upload_file),
        "ytexec_upload_size_mb": get_upload_size(ytexec_config) / 2**20,
    }

    task_patch = getattr(ytexec_config.operation_fields, 'task_patch', None) or {}
    if gpu_limit := task_patch.get('gpu_limit', None):
        yt_metrics["ytexec_gpu_limit"] = gpu_limit

    if os.path.exists(result_path):
        with open(result_path, 'r') as res_file:
            yt_res = json.load(res_file)
            yt_metrics.update({"ytexec_" + k: float(v) / 1000**3 for k, v in yt_res['statistics'].items()})
            logger.debug("metrics:\n%s", json.dumps(yt_metrics, indent=4))
            exit_code = yt_res["exit_code"]
            run_test.dump_meta(args, exit_code, start_time, time.time())
            if exit_code:
                op_url = extract_operation_url(args.output_dir)
                if yt_res["is_oom"]:
                    # In case of OOM there is no metadata
                    parts = ["YT operation has reported out-of-memory error"]
                    status = const.Status.CRASHED
                elif yt_res["exit_signal"]:
                    parts = ["Run_test node was killed by signal {}".format(yt_res["exit_signal"])]
                    status = const.Status.TIMEOUT
                else:
                    parts = ["Unknown yt error"]
                    status = const.Status.FAIL
                if op_url:
                    parts.append("For more info see operation: [[imp]]{}[[rst]]".format(op_url))
                suite = dump_test_info(args, yt_metrics, ". ".join(parts), status)
            else:
                suite = dump_test_info(args, yt_metrics)
    else:
        run_test.dump_meta(args, exit_code, start_time, time.time())
        msg = "ytexec was killed by timeout.\n"
        if exit_code == const.TestRunExitCode.TimeOut:
            operation_creation_time = get_operation_creation_time(args.output_dir)
            if operation_creation_time:
                start_time_obj = datetime.datetime.fromtimestamp(start_time)
                creation_time_obj = date_parser.parse(operation_creation_time).replace(tzinfo=None)
                timediff = creation_time_obj - start_time_obj
                timediff = timediff.seconds
                yt_metrics["ytexec_operation_creation_delay_s"] = timediff
            else:
                msg += "operation was not created\n"
            msg += "test metrics:\n{}".format(yt_metrics)
            suite = dump_test_info(args, metrics=yt_metrics, error=msg, status=const.Status.TIMEOUT)
        else:
            msg = "ytexec result file is not found."
            if os.path.exists(stderr_path):
                msg += "\nytexec error:\n{}".format(shared.read_tail(stderr_path, 5000))
            suite = dump_test_info(args, metrics=yt_metrics, error=msg, status=const.Status.FAIL)

    run_test.restore_chunk_identity(suite, args)
    run_test.finalize_node(suite)


def main():
    start_time = time.time()
    args = parse_args()
    setup_env(args)
    if 'ASAN_SYMBOLIZER_PATH' in os.environ:
        toolchain = os.path.dirname(os.path.dirname(os.environ['ASAN_SYMBOLIZER_PATH']))
        toolchain_subset = get_wd("toolchain_subset")
        prepare_toolchain_subset(toolchain, toolchain_subset)

    exts.fs.ensure_dir(args.output_dir)
    ytexec_config = YtexecConfig(*generate_config_sections(args))
    run_test.create_empty_outputs(args, overwrite=False)
    conf_path = os.path.join(args.output_dir, "config.yson")
    ytexec_config.dump(conf_path)

    ytexec_path = args.ytexec_tool
    logger.debug("ytexec path: %s", ytexec_path)
    exit_code = 0
    try:
        with open(os.path.join(args.output_dir, "stderr"), 'w') as errfile:
            if args.ytexec_node_timeout:
                ytexec_timeout = int(args.ytexec_node_timeout)
            else:
                ytexec_timeout = args.node_timeout - 20 if args.node_timeout else None
            process.execute([ytexec_path, '--config-yson', conf_path], stderr=errfile, timeout=ytexec_timeout)
    except process.ExecutionTimeoutError:
        exit_code = const.TestRunExitCode.TimeOut
    except Exception:
        logger.exception("unexpected error")
        exit_code = const.TestRunExitCode.Failed

    process_test_results(args, ytexec_config, exit_code, start_time)
    exts.archive.create_tar(args.output_dir, args.output_tar)


if __name__ == "__main__":
    exit(main())
