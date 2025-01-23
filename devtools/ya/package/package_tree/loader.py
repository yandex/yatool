import logging
import functools

import exts.yjson as json
import library.python.resource as rs
from jsonschema import Draft4Validator, ValidationError


from package.utils import timeit
from package.package_tree.tree import TreeInfo

logger = logging.getLogger(__name__)


def _load_package(tree_info: TreeInfo) -> dict:
    result = {}

    meta = tree_info.meta
    userdata = tree_info.userdata
    params = tree_info.params
    build = tree_info.build
    data = tree_info.data
    postprocess = tree_info.postprocess
    includes = tree_info.includes

    if meta is not None:
        result["meta"] = meta
    if userdata is not None:
        result["userdata"] = userdata
    if params is not None:
        result["params"] = params
    if build is not None:
        result["build"] = build
    if data is not None:
        result["data"] = data
    if postprocess is not None:
        result["postprocess"] = postprocess
    if includes is not None:
        result["include"] = includes

    return result


@functools.cache
def _get_validator() -> Draft4Validator:
    schema = json.loads(rs.resfs_read("package.schema.json"))
    return Draft4Validator(schema)


def _validate_package(package_file: str, package: dict) -> None:
    try:
        _get_validator().validate(package)
    except ValidationError as error:
        if error.message.startswith('Additional properties are not allowed'):
            logger.error("Package %s has either invalid or old format: ", package_file)
            raise
        # XXX Some tests are not ready for this check, see https://a.yandex-team.ru/review/5823503/details#comment-8692504
        logger.warning("Package %s has either invalid or old format: %s", package_file, error)


@timeit
def load_package(tree_info: TreeInfo) -> dict:
    result_package = _load_package(tree_info)
    package_file = tree_info.root.package_file
    _validate_package(package_file, result_package)
    return result_package
