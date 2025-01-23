from __future__ import annotations

import os
import logging

import exts.yjson as json

from package.utils import timeit
from package.package_tree.consts import TraversalType
from package.package_tree.visitor import (
    Visitor,
    LoopDetectorVisitor,
    UpToBottomSectionsVisitor,
    BuildSectionVisitor,
    DataSectionVisitor,
    PostprocessesSectionVisitor,
)

logger = logging.getLogger(__name__)


class TreeInfo:
    def __init__(
        self,
        root: TreeNode,
        meta: dict,
        data: list,
        userdata: dict | None = None,
        params: dict | None = None,
        build: dict | None = None,
        postprocess: list | None = None,
        includes: list[str | dict] | None = None,
    ):
        self.root = root

        self.meta = meta
        self.data = data
        self.userdata = userdata
        self.params = params
        self.build = build
        self.postprocess = postprocess
        self.includes = includes

    def get_recursive_includes(self, arcadia_root: str) -> list[str]:
        """
        All package files we've seen including target package file
        """

        all_includes = self._get_recursive_includes_helper(self.root)

        for idx in range(len(all_includes)):
            all_includes[idx] = self._get_clean_package_file(arcadia_root, all_includes[idx])

        return all_includes

    def _get_recursive_includes_helper(self, node: TreeNode) -> list[str]:
        res = []

        if not node.parent:
            res.append(node.package_file)

        for include in node.includes:
            res.append(include.package_file)

        for include in node.includes:
            res.extend(self._get_recursive_includes_helper(include))

        return res

    @staticmethod
    def _get_clean_package_file(arcadia_root: str, package_file: str) -> str:
        if package_file.startswith(arcadia_root):
            return package_file[len(arcadia_root) + 1 :]
        return package_file


class TreeNode:
    def __init__(
        self,
        package_file: str,
        parsed_json: dict,
        includes: list | None = None,
        parent: TreeNode | None = None,
        my_targets_root: str = "",
    ):
        self.package_file = package_file
        self.parsed_json = parsed_json
        self.includes = includes or []  # links to other tree nodes via includes
        self.parent = parent
        self.my_targets_root = my_targets_root

    @property
    def build_key(self) -> str:
        return self.package_file.lstrip(os.sep)

    @property
    def full_targets_root_path(self) -> str:
        """
        Chain path up to the root
        """
        target_roots = []
        current = self

        while current:
            target_roots.append(current.my_targets_root.lstrip("/"))
            current = current.parent

        target_roots.reverse()
        return os.path.join(*target_roots)

    def visit(self, visitor: Visitor) -> None:
        visitor.visit(self)


class TreeConstructor:
    def __init__(self, arcadia_root: str):
        self.arcadia_root = arcadia_root

    def construct(self, package_file: str, parent: TreeNode | None = None, my_targets_root: str = "") -> TreeNode:
        from package.packager import is_old_format, YaPackageException

        parsed_package = self._parse_package(package_file)
        includes = parsed_package.get("include", [])

        new_node = TreeNode(
            package_file=package_file,
            parsed_json=parsed_package,
            parent=parent,
            my_targets_root=my_targets_root,
        )

        for include in includes:
            if isinstance(include, dict):
                include_package_path = include["package"]
                include_package_root = include.get("targets_root") or ""
            elif isinstance(include, str):
                include_package_path = include
                include_package_root = ""
            else:
                raise YaPackageException("Unknown include type: {}".format(type(include)))

            new_node.includes.append(self.construct(include_package_path, new_node, include_package_root))

        old = is_old_format(parsed_package)
        logger.debug("Detected package file format: %s", "new" if not old else "old")
        if old:
            raise YaPackageException("The old package format is no longer supported.")

        return new_node

    def _parse_package(self, package_file: str) -> dict:
        from package.packager import get_package_file, YaPackageException

        with open(get_package_file(self.arcadia_root, package_file)) as afile:
            try:
                return json.load(afile)
            except ValueError as e:
                raise YaPackageException('JSON loading error: {} in {}'.format(e, package_file)) from e


@timeit
def get_tree_info(arcadia_root: str, package_file: str, traversal_variant_param: str | None = None) -> TreeInfo:
    tree_constructor = TreeConstructor(arcadia_root)
    tree = tree_constructor.construct(package_file)

    loop_detector_visitor = LoopDetectorVisitor()
    up_to_bottom_visitor = UpToBottomSectionsVisitor()

    tree.visit(loop_detector_visitor)
    tree.visit(up_to_bottom_visitor)

    traversal_variant = TraversalType.POSTORDER
    if traversal_variant_param is None:
        traversal_variant_param = up_to_bottom_visitor.params.get("include_traversal_variant")
    if traversal_variant_param is not None:
        traversal_variant = TraversalType(traversal_variant_param)

    build_section_visitor = BuildSectionVisitor(traversal_variant)
    data_section_visitor = DataSectionVisitor(traversal_variant)
    postprocesses_section_visitor = PostprocessesSectionVisitor(traversal_variant)

    tree.visit(build_section_visitor)
    tree.visit(data_section_visitor)
    tree.visit(postprocesses_section_visitor)

    tree_info = TreeInfo(
        root=tree,
        meta=up_to_bottom_visitor.meta,
        data=data_section_visitor.data,
        userdata=up_to_bottom_visitor.userdata,
        params=up_to_bottom_visitor.params,
        build=build_section_visitor.build,
        postprocess=postprocesses_section_visitor.postprocess,
        includes=up_to_bottom_visitor.includes,
    )

    return tree_info
