import os
import sys
import enum
import json
import logging
import pathlib
import collections

import typing as tp

import devtools.ya.tools.analyze_make.common as common
import devtools.ya.test.filter as test_filter
import tqdm

import library.python.resource as resource


CSS_TYPE_ENTRY = """
.webtreemap-type-%(type)s {
  background: %(color)s;
}
"""

CSS_LEVEL_ENTRY = """
.webtreemap-level%(level)s {
  filter: hue-rotate(-%(val)sdeg);
}
"""

RESOURCE_PREFIX = pathlib.Path('/bloat2/static')
TREEMAP_CSS = 'webtreemap.css'
INDEX_HTML = 'index.html'
HMTL_LEGEND_PLACEHOLDER = '<!--LEGEND-->'
JSON_PLACEHOLDER = '<!--JSON-->'
HTML_LEGEND_TEMPLATE = '<div class="webtreemap-node webtreemap-type-{entry}" style="filter: hue-rotate(-{hue_rotate}deg); z-index: 100;">{entry}<span class="tooltip">{hint}</span></div>'

Color = collections.namedtuple('Color', ['type', 'color'])

logger = logging.getLogger(__name__)


def unify_paths(paths: list[str]) -> str:
    if len(paths) == 1:
        return paths[0]
    paths.sort()
    result = ""
    residues = []
    for index, checked_symbol in enumerate(paths[0]):
        checker = []
        for s in paths[1:]:
            try:
                checker.append(s[index] == checked_symbol)
            except IndexError:
                checker.append(False)
        if all(checker):
            result += checked_symbol
        else:
            break

    for p in paths:
        residue = p[len(result) :]
        residues.append(residue)

    return result + "{" + ", ".join(sorted(residues)) + "}"


class TreeNode:
    def __init__(
        self,
        node: tp.Optional[common.Node] = None,
        name: tp.Optional[str] = None,
        is_root: bool = False,
        path: tp.Optional[str] = None,
        is_build_node: bool = False,
    ):
        self.node: tp.Optional[common.Node] = node
        self.children: dict[str, TreeNode] = {}
        self.is_root: bool = is_root
        self._path: tp.Optional[str] = path

        self._name: str = name or "tree node"
        self._colorizer: ColorType = ColorType
        self._is_build_node: bool = is_build_node

        self._relative_size: tp.Optional[float] = None
        self._real_size: tp.Optional[float] = None
        self._size: tp.Optional[float] = None

    @property
    def path(self) -> list[str]:
        if self._path:
            return self._path.split("/")
        if self.node:
            return self.get_node_path(self.node)
        return []

    @staticmethod
    def get_node_path(node: common.Node) -> list[str]:
        try:
            for check in {"Resource", "Pattern", "PutInCache", "WriteThroughCaches"}:
                if check in node.name.split("(")[0]:
                    return [node.name]
            paths = []
            for file in node.name.split(" "):
                try:
                    _, path = file.split("$(BUILD_ROOT)/")
                    path = path.removesuffix(")")
                    paths.append(path)
                except Exception:
                    pass
            return unify_paths(paths).split("/")
        except Exception:
            return []

    @property
    def name(self) -> str:
        res = []
        if self.node is not None:
            if "/" in self.node.name:
                res.append(self.node.tag)
                node_path = self.get_node_path(self.node)
                if len(node_path) > 0:
                    res.append(node_path[-1])
                else:
                    logger.debug("Failed to get node path for node %s", self.node.name)
            else:
                res.append(self.node.name)

        suffix = "/" if not self.is_root else ""

        return self._name + suffix if not res else ' '.join(res)

    def set_relative_size(self, relative_to: tp.Self) -> None:
        for child in self.children.values():
            child.set_relative_size(relative_to)

        if self.children == {} or (
            self.node is not None and sum([i.get_size() for i in self.children.values()]) < self.get_size_by_node()
        ):
            self._relative_size = (
                self.get_size_by_node() / relative_to.get_real_size()
            ) * relative_to.get_size_by_node()

    def get_size_by_node(self) -> tp.Optional[float]:
        if self.node is not None:
            return self.node.end - self.node.start
        return None

    def get_real_size(self, recalculate_size: bool = False) -> float:
        if self._real_size is not None and not recalculate_size:
            return self._real_size

        r = self.get_size_by_node() if self.get_size_by_node() is not None else 0

        self._real_size = max(r, sum([i.get_real_size() for i in self.children.values()]))

        return self._real_size

    def get_size(self, recalculate_size=False) -> float:
        if self._size is not None and not recalculate_size:
            return self._size

        r = self.get_size_by_node() if self.get_size_by_node() is not None else 0

        self._size = max(r, sum([i._relative_size or i.get_size() for i in self.children.values()]))

        return self._size

    def as_filtered_dict(self, path_filters, threshold, show_leaf_nodes) -> tp.Optional[dict]:
        """
        Filters out and returns a dictionary representation of the node and its children.

        All filters are applied to build nodes.
        Path filters are applied recursively.
        If node itself or any descendant matches path filters, the checked node won't be filtered out.

        :param path_filters: path filters to apply.
        :param threshold: minimal allowed duration to be shown (seconds).
        :param show_leaf_nodes: whether leaf nodes (files, PREPARE nodes, etc.) should be shown.
        :return: a JSON-serializable representation of the node and its children (or None if the node is filtered out)
        """
        if self._is_build_node:
            if self.duration < threshold:
                return None

            if self.path:
                if not self.match_filters(path_filters):
                    return None

                if len(self.children.keys()) == 0 and not show_leaf_nodes:
                    return None

        children = []
        for child in self.children.values():
            dct = child.as_filtered_dict(
                path_filters,
                threshold,
                show_leaf_nodes,
            )
            if dct is not None:
                children.append(dct)

        return dict(
            name='{} {}'.format(self.name, self.text_size),
            size=self._relative_size or self.get_size(),
            type=self._colorizer.css_name(self).value,
            children=children,
        )

    def _do_insert_at_path(self, node: common.Node, path: list[str], prefix: list[str] = None) -> tp.Self:
        prefix = prefix or []
        if len(path) == 0:
            self.node = node
            return self

        cur_dir = path[0]
        prefix = prefix + [cur_dir]
        if len(path) == 1:
            node_key = cur_dir + str(node.start)
        else:
            node_key = cur_dir
        if node_key not in self.children:
            self.children[node_key] = TreeNode(
                node=None,
                name=cur_dir,
                path="/".join(prefix),
                is_build_node=True,
            )

        return self.children[node_key]._do_insert_at_path(node, path[1:], prefix)

    def _do_insert(self, node: common.Node) -> tp.Self:
        path = self.get_node_path(node)
        if not path:
            n = TreeNode(node)
            self.children[node.name + str(node.start)] = n
            return n
        else:
            return self._do_insert_at_path(node, path)

    def match_filters(self, filters: list[str]) -> bool:
        if len(filters) == 0:
            return True
        full_path = "/".join(self.path)
        path_filter = test_filter.make_name_filter(filters)

        if path_filter(full_path):
            return True
        else:
            children = []
            for _, child in self.children.items():
                children.append(child.match_filters(filters))
            return any(children)

    def insert_node(self, node: common.Node) -> tp.Optional[tp.Self]:
        if self.name == "dispatch_build" and node.thread_name != "MainThread":
            # Fast insert is available since dispatch_build is parent and non-mainthread nodes are scheduled by it.
            return self._do_insert(node)

        for child in self.children.values():
            if can_be_parent_node(child.node, node):
                return child.insert_node(node)
        else:
            return self._do_insert(node)

    def iter_nodes(self, level=0) -> tp.Iterator[tp.Tuple[tp.Self, int]]:
        yield self, level
        for child in self.children.values():
            yield from child.iter_nodes(level + 1)

    def max_level(self, level=0) -> int:
        levels = [-1]
        for child in self.children.values():
            levels.append(child.max_level(level + 1))
        return max(level, *levels)

    @property
    def duration(self) -> float:
        r = 0
        if self.node is not None:
            if self.node.thread_name != "MainThread":
                r = self.get_real_size()
            else:
                r = self.get_size()
        else:
            if self.is_root:
                r = self.get_size()
            else:
                r = self.get_real_size()
        return r

    @property
    def text_size(self) -> str:
        return Time.time_to_str(round(self.duration, 2))


class Time:
    @staticmethod
    def time_to_str(time_sec: float) -> str:
        res_string = ''
        sec = round(time_sec % 60, 3)
        mins = int((time_sec // 60) % 60)
        hours = int(time_sec // 3600)

        if hours:
            res_string += f"{hours}h "
        if mins:
            res_string += f"{mins}m "
        res_string += f"{sec}s"
        return res_string


class ColorType(enum.StrEnum):
    DEFAULT = "default"
    OTHER = "other"
    MAINTHREAD = "mainthread"
    TESTS = "tests"

    @staticmethod
    def color(node: TreeNode) -> str:
        MAP = {
            ColorType.DEFAULT: "#8DA0CB",
            ColorType.OTHER: "#ADFF00",
            ColorType.MAINTHREAD: "#1BBEAE",
            ColorType.TESTS: "#FF8225",
        }
        return MAP[ColorType.css_name(node)]

    @staticmethod
    def css_name(node: TreeNode) -> tp.Self:
        if "test-results" in node.path:
            return ColorType.TESTS
        if node.node is None:
            if not node.is_root:
                return ColorType.OTHER
            return ColorType.DEFAULT

        if node.node.thread_name == "MainThread":
            return ColorType.MAINTHREAD
        return ColorType.OTHER

    @staticmethod
    def color_as_type(node: TreeNode) -> Color:
        return Color(ColorType.css_name(node), ColorType.color(node))


def can_be_parent_node(a: common.Node, b: common.Node) -> bool:
    # General assumption is that node A is either scheduled by someone in its thread (node B)
    # Or by someone in the main thread (also node B).
    if a is None:
        return False
    if b.start >= a.start and b.end <= a.end:
        if b.thread_name == a.thread_name or a.thread_name == "MainThread":
            return True

    return False


def insert_nodes(root: TreeNode, nodes: list[common.Node]) -> None:
    for node in tqdm.tqdm(nodes, unit="node", file=sys.stderr):
        root.insert_node(node)


def find_nodes_to_normalize(root: TreeNode) -> list[TreeNode]:
    seen = set()
    to_normalize = []

    for node, _ in root.iter_nodes():
        added = False
        if node in seen:
            continue
        seen.add(node)
        for child in node.children.values():
            if ColorType.css_name(child) in {ColorType.OTHER, ColorType.TESTS}:
                to_normalize.append(node)
                added = True
                break
        if added:
            for child, _ in node.iter_nodes():
                seen.add(child)
    return to_normalize


def gather_color_statistics(root: TreeNode) -> dict[Color, int]:
    colors = collections.defaultdict(lambda: float('inf'))
    for node, level in root.iter_nodes():
        colors[ColorType.color_as_type(node)] = min(colors[ColorType.color_as_type(node)], level)

    return colors


def copy_html_resources_without_changes(resources: list[str], to: pathlib.Path):
    for item in resources:
        r = resource.find(str(RESOURCE_PREFIX / item))
        with (to / item).open("wb") as afile:
            afile.write(r)


def get_css_colors_and_legend(colors: dict[Color, int], n_top=9, levels=10) -> tuple[list[str], list[str]]:
    hint_map = {
        ColorType.DEFAULT: "Total execution time",
        ColorType.MAINTHREAD: "Duration of a stage",
        ColorType.OTHER: "Sum of durations of children or stage duration, what is longer",
        ColorType.TESTS: "Tests duration",
    }

    color_list = []
    legend = []
    for color, level_first_seen in list(sorted(colors.items(), key=lambda x: x[0])):
        color_list.append(CSS_TYPE_ENTRY % dict(type=color.type, color=color.color))
        if len(legend) < n_top:
            legend.append(
                HTML_LEGEND_TEMPLATE.format(
                    entry=color.type,
                    hint=hint_map[color.type],
                    hue_rotate=sum(range(level_first_seen + 1)),
                )
            )

    for level in range(levels):
        color_list.append(CSS_LEVEL_ENTRY % dict(level=level, val=level))
    return color_list, legend


def main(opts):
    display = common.get_display(sys.stdout)

    file_name, nodes = common.load_evlog(opts, display, check_for_distbuild=True)

    if not nodes:
        raise RuntimeError(f"No nodes could be parsed from {file_name}")

    nodes.sort(key=lambda x: (x.start, -x.end))  # Longest nodes first to build trees correctly

    root = TreeNode(name="total build", is_root=True)

    display.emit_message('Inserting nodes')
    insert_nodes(root, nodes)

    display.emit_message('Adjusting sizes')
    to_normalize = find_nodes_to_normalize(root)
    for node in to_normalize:
        node.set_relative_size(relative_to=node)

    display.emit_message('Setting colors')
    colors = gather_color_statistics(root)

    directory = pathlib.Path(os.getcwd(), 'bloat')
    os.makedirs(directory, exist_ok=True)

    copy_html_resources_without_changes(['bloat.css', 'webtreemap.js', TREEMAP_CSS], to=directory)

    css_colors, legend = get_css_colors_and_legend(colors, levels=root.max_level())

    with (directory / TREEMAP_CSS).open('a') as afile:
        afile.write('\n'.join(css_colors))

    raw_html = resource.find(f'{RESOURCE_PREFIX / INDEX_HTML}').decode()
    html_with_legend = raw_html.replace(HMTL_LEGEND_PLACEHOLDER, '\n'.join(legend))
    full_html = html_with_legend.replace(
        JSON_PLACEHOLDER,
        json.dumps(
            {
                "tree": root.as_filtered_dict(
                    opts.file_filters,
                    opts.threshold,
                    opts.show_leaf_nodes,
                ),
            },
        ),
    )

    with (directory / INDEX_HTML).open('w') as f:
        f.write(full_html)

    display.emit_message(f'[[imp]]Open bloat/{INDEX_HTML} in your browser.')
