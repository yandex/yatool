import logging
import os

import exts.func
from .sandbox_resource import Reference
from devtools.ya.test.util import path as util_path
from yalibrary.large_files import ExternalFile

logger = logging.getLogger(__name__)


def get_test_related_paths(test, arc_root, opts):
    """
    Get all paths on file system that a test may depend on
    :param test:
    :return:
    """
    test_related_paths = []

    # add arcadia folders that are specified in the test CMakeLists.txt
    for data in test.get_arcadia_test_data():
        data = data.replace("/", os.path.sep)
        test_related_paths.append(os.path.join(arc_root, data))

    # add test related paths required for wrappers (suites)
    for path in reversed(test.get_test_related_paths(arc_root, opts)):
        test_related_paths.insert(0, path)

    return unique_stable(test_related_paths)


def unique_stable(alist):
    added = set()
    result = []
    for data in alist:
        if data not in added:
            added.add(data)
            result.append(data)
    return result


def remove_redundant_paths(paths):
    """
    Removes paths that are nested in some others in the list
    :param paths: list of paths to be optimized
    :return: the optimized path list
    """
    if not paths:
        return []

    canon_path_map = {util_path.canonize_path(p): p for p in paths}
    # Use canonized paths to avoid incorrect order for case [a, a-b, a/b]
    # Sorted canonized paths would give [a-b, a, a/b]
    # which is expected by following algorithm
    paths = sorted(canon_path_map)
    cur = exts.func.first(paths)
    res = [canon_path_map[cur]]

    for path in paths:
        if not util_path.is_sub_path(path, cur):
            cur = path
            res.append(canon_path_map[cur])

    return res


def get_docker_images(test):
    return test.get_test_docker_images()


def get_test_sandbox_resources(test):
    """
    Gets the declared by test sandbox ids
    :param test: test to get ids from
    """
    return [_f for _f in [Reference.from_uri(uri) for uri in test.get_sandbox_resources()] if _f]


def get_test_mds_resources(test):
    res = []
    scheme_prefix = 'mds:'
    prefix_len = len(scheme_prefix)

    for x in test.get_mds_resources():
        assert x.startswith(scheme_prefix)
        res.append(x[prefix_len:])

    return res


def get_test_ext_large_files(test, arc_root):
    res = []
    scheme_prefix = 'ext:'
    prefix_len = len(scheme_prefix)

    for x in test.get_ext_resources():
        assert x.startswith(scheme_prefix), x
        res.append(x[prefix_len:])

    return tuple(ExternalFile(os.path.join(arc_root, test.project_path), item) for item in res)


def get_test_ext_sbr_resources(test, arc_root):
    resources = []
    for large_file in get_test_ext_large_files(test, arc_root):
        if large_file.orig_exist:
            continue

        if not large_file.external_exist:
            logger.error("External file doesn't exist: %s", large_file.external_file_path)
            continue

        ext_info = large_file.external_info()

        if ext_info['storage'] != "SANDBOX":
            continue

        resource = ext_info['resource_id']
        target_dir = ""

        relative_file_path = large_file.relative_file_path
        if os.path.dirname(relative_file_path):
            target_dir = os.path.dirname(relative_file_path)

        resources.append(Reference(resource, target_dir))

    return resources


def get_test_ext_file_resources(test, arc_root):
    file_pathes = []
    for large_file in get_test_ext_large_files(test, arc_root):
        if not large_file.orig_exist:
            continue

        file_pathes.append(os.path.relpath(large_file.orig_file_path, arc_root))

    return file_pathes


def get_suite_requested_input(suite, opts):
    inputs = set()
    for path in get_test_related_paths(suite, "$(SOURCE_ROOT)", opts):
        inputs.add(path)

    return list(inputs)


def unique(lst):
    return sorted(set(lst))
