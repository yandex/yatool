import os
import shutil
import logging
import stat

from devtools.ya.test.dependency import testdeps, sandbox_resource

from devtools.ya.test.util import tools
from devtools.ya.test import const as constants
import exts.fs
import exts.windows

logger = logging.getLogger(__name__)


class EnvDataMode(constants.Enum):
    Symlinks = "symlinks"
    Copy = "copy"
    CopyReadOnly = "copyro"


class EnvDataModeException(Exception):
    pass


def stat_writable():
    return stat.S_IWUSR | stat.S_IWGRP  # writable for user and group only


def stat_notwritable():
    return stat.S_IWUSR | stat.S_IWGRP | stat.S_IWOTH  # not writable for all


def apply_env_data_mode_recursive(env_path, env_data_mode):
    apply_env_data_mode(env_path, env_data_mode)
    for dirpath, dirnames, filenames in os.walk(env_path):
        for name in dirnames + filenames:
            env_name = os.path.join(dirpath, name)
            apply_env_data_mode(env_name, env_data_mode)


def apply_env_data_mode(env_name, env_data_mode):
    perm = stat.S_IMODE(os.stat(env_name).st_mode)
    if env_data_mode == EnvDataMode.Copy:
        writable = stat_writable()
        if (perm & writable) != writable:
            os.chmod(env_name, perm | writable)
    elif env_data_mode == EnvDataMode.CopyReadOnly:
        notwritable = stat_notwritable()
        if (perm & notwritable) != 0:
            os.chmod(env_name, perm & ~notwritable)
    else:
        raise EnvDataModeException('Unsupported environment data mode "{}"'.format(env_data_mode))


def get_perm(env_path):
    return stat.S_IMODE(os.stat(env_path).st_mode)


def is_writable(env_path):
    writable = stat_writable()
    return (get_perm(env_path) & writable) == writable


def is_not_writable(env_path):
    return (get_perm(env_path) & stat_notwritable()) == 0


class ResourceConflictException(Exception):
    pass


def create_environment(
    test_source_paths,
    test_data_paths,
    source_root,
    build_root,
    data_root,
    destination,
    env_data_mode=EnvDataMode.Symlinks,
    create_root_guidance_file=False,
    pycache_prefix=None,
):
    def create_links(root, env_root, paths):
        paths = testdeps.remove_redundant_paths(paths)
        for path in paths:
            root_rel_path = os.path.relpath(path, root)
            link_dir_path = os.path.join(env_root, os.path.dirname(root_rel_path))
            if not os.path.exists(link_dir_path):
                os.makedirs(link_dir_path)
            link_path = os.path.join(env_root, root_rel_path)
            if os.path.exists(link_path):
                for filename in os.listdir(link_path):
                    src = os.path.join(path, filename)
                    dst = os.path.join(link_path, filename)
                    try:
                        tools.link_dir(src, dst)
                    except Exception as e:
                        logger.debug("Failed to link %s -> %s: %s", src, dst, e)
            else:
                tools.link_dir(path, link_path)

    def copy_srcs(root, env_root, paths, env_data_mode):
        paths = testdeps.remove_redundant_paths(paths)
        for path in paths:
            root_rel_path = os.path.relpath(path, root)
            env_path = os.path.join(env_root, root_rel_path)
            realpath = os.path.realpath(path)  # else symlinks may be copy
            if os.path.isdir(realpath):

                def ignore_symlinks(ipath, inames):
                    def ignore_symlink(iname):
                        return os.path.islink(os.path.join(ipath, iname))

                    return filter(ignore_symlink, inames)

                shutil.copytree(realpath, env_path, ignore=ignore_symlinks)
            else:
                exts.fs.ensure_dir(os.path.dirname(env_path))  # prepare directory for copy file
                shutil.copy(realpath, env_path)
            apply_env_data_mode_recursive(env_path, env_data_mode)

    test_env_root = os.path.join(destination, "environment")
    # delete if existing
    if os.path.exists(test_env_root):
        shutil.rmtree(test_env_root)

    if exts.windows.on_win():
        env_arcadia_root = source_root
        env_data_root = data_root or ""  # If set to None, won't work on windows and py3.
    else:
        env_arcadia_root = os.path.join(test_env_root, "arcadia")
        env_data_root = os.path.join(test_env_root, "arcadia_tests_data")
        if env_data_mode == EnvDataMode.Symlinks:
            create_links(source_root, env_arcadia_root, test_source_paths)
        elif env_data_mode == EnvDataMode.Copy or env_data_mode == EnvDataMode.CopyReadOnly:
            copy_srcs(source_root, env_arcadia_root, test_source_paths, env_data_mode)
        else:
            raise EnvDataModeException('Unknown environment data mode "{}"'.format(env_data_mode))
        create_links(data_root, env_data_root, test_data_paths)

    env_build_root = os.path.join(test_env_root, "build")
    try:
        tools.link_dir(build_root, env_build_root)
    except Exception as e:
        logger.debug("Could not create symlink to build root: %s", e)
        env_build_root = build_root

    if create_root_guidance_file:
        with open(os.path.join(env_build_root, ".root.path"), "w") as afile:
            afile.write(source_root)

    if pycache_prefix:
        with open(os.path.join(env_build_root, ".pycache.path"), "w") as afile:
            afile.write(pycache_prefix)

    return os.path.abspath(env_arcadia_root), os.path.abspath(env_build_root), os.path.abspath(env_data_root)


def prepare_work_dir(
    build_root,
    work_dir,
    sandbox_resources,
    sandbox_storage,
    external_local_files=None,
    project_path=None,
):
    # Convert to unified slashes style
    build_root = os.path.abspath(build_root)
    work_dir = os.path.abspath(work_dir)

    if sandbox_resources:
        for entry in sandbox_resources:
            resource_id, _, _ = str(entry).partition('=')
            resource = sandbox_resource.create(entry)
            if not sandbox_storage.is_resource_prepared_for_dir_outputs(resource_id):
                continue
            sandbox_storage.dir_outputs_process_prepared_resource(resource, build_root)

    if external_local_files:
        assert project_path, "Set project_path"
        _prepare_external_local_files(build_root, external_local_files, work_dir, project_path)

    if sandbox_resources:
        _prepare_sandbox_resources(build_root, sandbox_resources, sandbox_storage, work_dir)


def _prepare_external_local_files(build_root, external_local_files, work_dir, project_path):
    for entry in external_local_files:
        src = os.path.join(build_root, "external_local", entry)
        dst = os.path.abspath(os.path.join(work_dir, os.path.relpath(entry, project_path)))

        dst_dir = os.path.dirname(dst)
        if not os.path.exists(dst_dir):
            logger.debug("Create dir %s", dst_dir)
            exts.fs.create_dirs(dst_dir)

        if os.path.isfile(src):
            logger.debug("%s is file, create link to %s", src, dst)
            exts.fs.hardlink(src, dst)
        else:
            logger.warning("Don't know what to do with local external entry (which isn't file): %s", src)


def _prepare_sandbox_resources(build_root, sandbox_resources, sandbox_storage, work_dir):
    dir_to_res = {}
    #  may be object or string or id
    for entry in sandbox_resources:
        resource = sandbox_resource.create(entry)
        resource_id = resource.get_id()
        target_dir = resource.get_target_dir()
        # XXX https://ml.yandex-team.ru/thread/devtools/170573835886736842/
        target_dir = target_dir.replace('${ARCADIA_BUILD_ROOT}', build_root)
        download_log = sandbox_storage.get_resource_download_log_path(build_root, resource_id)
        if os.path.exists(download_log):
            with open(download_log) as log:
                logger.debug("Resource %s download log", resource_id)
                logger.debug("\n    ".join([""] + log.read().split("\n")))
                logger.debug("End of resource %s download log", resource_id)
        else:
            logger.warning("Resource download log %s does not exist", download_log)

        resource_path = sandbox_storage.get(resource_id).path
        logger.debug(
            "Reading contents of resource %s from %s with target dir %s", resource_id, resource_path, target_dir
        )
        for path in os.listdir(resource_path):
            full_res_path = os.path.join(resource_path, path)
            dest_subpath = os.path.join(target_dir, path)
            if dest_subpath in dir_to_res:
                raise ResourceConflictException(
                    'Conflict between sandbox resources {} and {} on entry {}'.format(
                        dir_to_res[dest_subpath], resource, dest_subpath
                    )
                )
            else:
                dir_to_res[dest_subpath] = resource

            dest_path = os.path.abspath(os.path.join(work_dir, dest_subpath))
            if os.path.exists(dest_path):
                raise ResourceConflictException(
                    'Sandbox resource {} overwrites existing file/dir "{}"'.format(resource, dest_path)
                )
            if not dest_path.startswith(build_root):
                raise ResourceConflictException(
                    "You can't put sandbox resource {} outside ('{}') of node's build_root '{}'".format(
                        resource, dest_path, build_root
                    )
                )

            dest_path_dir = os.path.dirname(dest_path)
            if not os.path.exists(dest_path_dir):
                exts.fs.create_dirs(dest_path_dir)

            if os.path.isfile(full_res_path):
                logger.debug("%s is file, create link to %s", full_res_path, dest_path)
                exts.fs.hardlink(full_res_path, dest_path)
            else:
                logger.debug("%s is dir, create symlink to %s", full_res_path, dest_path)
                tools.link_dir(full_res_path, dest_path)
