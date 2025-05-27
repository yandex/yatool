# coding: utf-8

"Shared metatests integration tools."

import os
import io
import shutil
import logging

import exts.fs
import exts.windows
from devtools.ya.test import const

from yalibrary.loggers.file_log import TokenFilterFormatter


logger = logging.getLogger(__name__)


def append_python_paths(env, paths, overwrite=False):
    """
    Appends PYTHONPATH in the given env
    :param env: environment dict to be updated
    :param paths: paths to update
    """
    python_path_key = 'PYTHONPATH'
    python_paths = []
    if python_path_key in env and not overwrite:
        python_paths.append(env[python_path_key])
    python_paths.extend(paths)

    env[python_path_key] = os.pathsep.join(python_paths)


def get_python_paths(env):
    return env.get("PYTHONPATH", "").split(os.pathsep)


def get_common_logging_file_handler(path, mode="a"):
    """
    Get a common for test logs logging file handler
    :param path: path to the log file
    :param mode: file open mode
    :return: logging file handler
    """
    file_handler = logging.FileHandler(path, mode=mode)
    file_handler.setFormatter(
        TokenFilterFormatter("%(asctime)s - %(levelname)s - %(name)s - %(funcName)s: %(message)s")
    )
    file_handler.setLevel(logging.DEBUG)
    return file_handler


def link_dir(src, dst):
    """
    Links directory, choosing the best platform approach
    """

    if exts.windows.on_win():
        return exts.fs.hardlink_tree(src, dst)
    return exts.fs.symlink(src, dst)


def link_file(src, dst):
    """
    Links directory, choosing the best platform approach
    """
    if exts.windows.on_win():
        return exts.fs.hardlink_or_copy(src, dst)
    return exts.fs.symlink(src, dst)


def copy_dir_contents(src_dir, dest_dir, ignore_list=[], skip_links=True):
    """Copy src_dir directory content to dest_dir

    Args:
        src_dir (path): Source directory
        dest_dir (path): Destination directory
        ignore_list (list, optional): Top level items to be ignored. Defaults to [].
        skip_links (bool, optional): Ignore top level links. Defaults to True.
    """
    if not os.path.exists(dest_dir):
        os.makedirs(dest_dir)

    for entry in os.listdir(src_dir):
        if entry in ignore_list:
            continue

        src = os.path.join(src_dir, entry)
        dst = os.path.normpath(os.path.join(dest_dir, entry))

        if os.path.islink(src) and skip_links:
            continue

        if os.path.isdir(src):
            copy_dir_contents(src, dst)

        if os.path.isfile(src) and not os.path.exists(dst):
            shutil.copy(src, dst)


def get_log_results_link(opts):
    resource_results_id = getattr(opts, "build_results_resource_id", None)
    if resource_results_id:
        return "http://proxy.sandbox.yandex-team.ru/" + resource_results_id
    return None


def get_results_root(opts):
    log_result_link = get_log_results_link(opts)
    output_root = opts and opts.output_root
    if log_result_link or output_root:
        return log_result_link or output_root
    else:
        if opts and getattr(opts, "create_symlinks"):
            return opts.symlink_root or "$(SOURCE_ROOT)"
    return None


def truncate_tail(filename, size):
    if os.stat(filename).st_size <= size:
        return
    with open(filename, 'r+') as afile:
        afile.truncate(size)


def truncate_middle(filename, size, msg=None):
    """
    Truncates file from the middle to the specified size
    """
    msg = "..." if msg is None else msg
    filesize = os.stat(filename).st_size
    if filesize <= size:
        return

    data = msg
    msgsize = len(msg)
    if msgsize < size:
        lend = size // 2 - msgsize // 2
    else:
        lend = size // 2
    with io.open(filename, "r+", errors='ignore') as afile:  # XXX until moved to py3
        if msgsize < size - lend:
            rsize = size - lend - msgsize
        else:
            rsize = size - lend
        rstart = filesize - rsize

        afile.seek(rstart, os.SEEK_SET)
        data += afile.read(rstart)

        afile.seek(lend, os.SEEK_SET)
        afile.write(data)
        afile.truncate(size)


def truncate_logs(files, size):
    for filename in files:
        truncate_middle(filename, size, msg="\n[..truncated..]\n")


def remove_links(dir_path):
    for root, dirs, files in os.walk(dir_path):
        for file_path in files + dirs:
            file_path = os.path.join(root, file_path)
            if os.path.islink(file_path):
                logging.debug("Removing symlink %s", file_path)
                exts.fs.ensure_removed(file_path)


def get_test_tool_path(opts, global_resources, run_on_target):
    local_const_name = const.TEST_TOOL_TARGET_LOCAL if run_on_target else const.TEST_TOOL_HOST_LOCAL
    resource_const_name = const.TEST_TOOL_TARGET if run_on_target else const.TEST_TOOL_HOST
    path = '{}/test_tool'.format(global_resources.get(resource_const_name, '$({})'.format(resource_const_name)))
    if opts and local_const_name in opts.flags:
        path = opts.flags[local_const_name]
    assert path, local_const_name
    return path


def get_wine64_path(global_resources):
    return '{}/bin/wine64'.format(global_resources.get(const.WINE_TOOL, '$({})'.format(const.WINE_TOOL)))


def jdk_tool(name, jdk_path):
    return os.path.join(jdk_path, 'bin', name)


def jacoco_agent_tool(jacoco_agent_resource):
    return (
        jacoco_agent_resource
        if jacoco_agent_resource.endswith('.jar')
        else os.path.join(jacoco_agent_resource, 'devtools-jacoco-agent.jar')
    )


def get_test_tool_cmd(opts, tool_name, global_resources, wrapper=False, run_on_target_platform=False, python=None):
    cmd = [
        get_test_tool_path(
            opts, global_resources, run_on_target_platform and const.TEST_TOOL_TARGET in global_resources
        ),
        tool_name,
    ]
    target_tools = getattr(opts, "profile_test_tool", [])
    if target_tools and tool_name in target_tools:
        if wrapper:
            cmd.append("--profile-wrapper")
        else:
            cmd.append("--profile-test-tool")
    return cmd


def get_corpus_data_path(project_path, root=None):
    if "YA_TEST_CORPUS_DATA_PATH" in os.environ:
        target = os.environ.get("YA_TEST_CORPUS_DATA_PATH")
    else:
        target = os.path.join(const.CORPUS_DATA_ROOT_DIR, project_path, const.CORPUS_DATA_FILE_NAME)

    if root:
        return os.path.join(root, target)
    return target
