from __future__ import annotations

import abc
import os
import typing as tp

from package.package_tree.consts import DEFAULT_BUILD_KEY, TraversalType

if tp.TYPE_CHECKING:
    from package.package_tree.tree import TreeNode


class Visitor(abc.ABC):
    @abc.abstractmethod
    def visit(self, node: TreeNode) -> None:
        pass

    def go_on(self, node: TreeNode) -> None:
        for include in node.includes:
            self.visit(include)


class LoopDetectorVisitor(Visitor):
    def __init__(self):
        self.visited = []

    def visit(self, node: TreeNode):
        from package.packager import YaPackageException

        if node in self.visited:
            raise YaPackageException(f"Include loop detected: {self._get_cycle(node)}")

        self.visited.append(node)

        self.go_on(node)

    def _get_cycle(self, node: TreeNode) -> str:
        cycle = []
        current = node

        while current:
            cycle.append(current.package_file)
            current = current.parent

        cycle.reverse()
        return " -> ".join(cycle)


class UpToBottomSectionsVisitor(Visitor):
    def __init__(self):
        self._params = {}
        self._meta = {}
        self._userdata = {}
        self._includes = []

    def visit(self, node: TreeNode):
        package_params = node.parsed_json.get("params", {})
        for k, v in package_params.items():
            self._params.setdefault(k, v)

        package_meta = node.parsed_json.get("meta", {})
        for k, v in package_meta.items():
            self._meta.setdefault(k, v)

        package_userdata = node.parsed_json.get("userdata", {})
        for k, v in package_userdata.items():
            self._userdata.setdefault(k, v)

        include_section = node.parsed_json.get("include", [])
        self._includes.extend(include_section)

        self.go_on(node)

    @property
    def params(self) -> dict:
        return self._params

    @property
    def meta(self) -> dict:
        return self._meta

    @property
    def userdata(self) -> dict:
        return self._userdata

    @property
    def includes(self) -> list:
        return self._includes


class BuildSectionVisitor(Visitor):
    def __init__(self, traversal_type: TraversalType):
        self.traversal_type = traversal_type
        self._build = {}

    def visit(self, node: TreeNode):
        if self.traversal_type == TraversalType.PREORDER:
            self.go_on(node)

        build_section = node.parsed_json.get("build", {})
        builds = [(DEFAULT_BUILD_KEY, build_section)] if "targets" in build_section else build_section.items()

        for k, v in builds:
            self._build["{}::{}".format(node.build_key, k) if '::' not in k else k] = v

        if self.traversal_type == TraversalType.POSTORDER:
            self.go_on(node)

    @property
    def build(self) -> dict:
        return self._build


class DataSectionVisitor(Visitor):
    def __init__(self, traversal_type: TraversalType):
        self.traversal_type = traversal_type
        self._data = []

    def visit(self, node: TreeNode):
        if self.traversal_type == TraversalType.PREORDER:
            self.go_on(node)

        package_data = node.parsed_json.get("data", [])

        for data in package_data:
            if data.get("source", {}).get("type") == "BUILD_OUTPUT":
                current = data["source"].get("build_key", DEFAULT_BUILD_KEY)
                if '::' not in current:
                    data["source"]["build_key"] = "{}::{}".format(node.build_key, current)
            if node.full_targets_root_path:
                package_targets_root = "/" + node.full_targets_root_path.lstrip("/")
                data["destination"]["path"] = os.path.join(
                    package_targets_root, data["destination"]["path"].lstrip("/")
                )

            self._data.append(data)

        if self.traversal_type == TraversalType.POSTORDER:
            self.go_on(node)

    @property
    def data(self) -> list:
        return self._data


class PostprocessesSectionVisitor(Visitor):
    def __init__(self, traversal_type: TraversalType):
        self.traversal_type = traversal_type
        self._postprocess = []

    def visit(self, node: TreeNode):
        if self.traversal_type == TraversalType.PREORDER:
            self.go_on(node)

        postprocceses = node.parsed_json.get("postprocess", [])

        for postproc in postprocceses:
            if postproc.get("source", {}).get("type") == "BUILD_OUTPUT":
                current = postproc["source"].get("build_key", DEFAULT_BUILD_KEY)
                if '::' not in current:
                    postproc["source"]["build_key"] = "{}::{}".format(node.build_key, current)

            if self.traversal_type == TraversalType.PREORDER:  # XXX: think over postorder traversal
                if 'cwd' not in postproc and node.full_targets_root_path:
                    postproc['cwd'] = node.full_targets_root_path

            self._postprocess.append(postproc)

        if self.traversal_type == TraversalType.POSTORDER:
            self.go_on(node)

    @property
    def postprocess(self) -> list:
        return self._postprocess
