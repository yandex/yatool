import os
import sys
import enum
import json
import pathlib
import collections

import typing as tp

import devtools.ya.tools.analyze_make.common as common


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
HTML_LEGEND_TEMPLATE = (
    '<div class="webtreemap-node webtreemap-type-{entry}">{entry}<span class="tooltip">{hint}</span></div>'
)

Color = collections.namedtuple('Color', ['type', 'color'])


class TreeNode(object):
    def __init__(self, node: tp.Optional[common.Node] = None, name: tp.Optional[str] = None, is_root: bool = False):
        self.node: tp.Optional[common.Node] = node
        self.children: dict[str, TreeNode] = {}
        self.is_root: bool = is_root

        self._name: str = name or "tree node"
        self._colorizer: ColorType = ColorType

        self._relative_size: tp.Optional[float] = None
        self._real_size: tp.Optional[float] = None
        self._size: tp.Optional[float] = None

    @property
    def name(self) -> str:
        res = []
        if self.node is not None:
            if "/" in self.node.name:
                res.append(self.node.tag)
                res.append(self.node.name.split("/")[-1][:-1])
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

    def as_dict(self) -> dict:
        return dict(
            name='%s %s' % (self.name, self.text_size()),
            size=self._relative_size or self.get_size(),
            type=self._colorizer.css_name(self).value,
            children=[child.as_dict() for child in self.children.values()],
        )

    def get_path(self, node: common.Node) -> list[str]:
        try:
            _, path = node.name.split("$(BUILD_ROOT)/")
            path = path.removesuffix(")")
            return path.split("/")
        except Exception:
            return []

    def insert_at_path(self, node: common.Node, path: list[str]) -> tp.Self:
        if len(path) == 0:
            self.node = node
            return self

        d = path[0]
        if d not in self.children:
            self.children[d] = TreeNode(node=None, name=d)

        return self.children[d].insert_at_path(node, path[1:])

    def insert_node(self, node: common.Node) -> tp.Self:
        for child in self.children.values():
            if can_be_parent_node(child.node, node):
                return child.insert_node(node)
        else:
            path = self.get_path(node)
            if not path:
                n = TreeNode(node)
                self.children[node.name + str(node.start)] = n
                return n
            else:
                return self.insert_at_path(node, path)

    def iter_nodes(self) -> tp.Iterator[tp.Self]:
        yield self
        for child in self.children.values():
            for node in child.iter_nodes():
                yield node

    def max_level(self, level=0) -> int:
        levels = [-1]
        for child in self.children.values():
            levels.append(child.max_level(level + 1))
        return max(level, *levels)

    def text_size(self) -> str:
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
        return Time.time_to_str(round(r, 2))


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

    @staticmethod
    def color(node: TreeNode) -> str:
        MAP = {
            ColorType.DEFAULT: "#8DA0CB",
            ColorType.OTHER: "#ADFF00",
            ColorType.MAINTHREAD: "#1BBEAE",
        }
        return MAP[ColorType.css_name(node)]

    @staticmethod
    def css_name(node: TreeNode) -> tp.Self:
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
    for node in nodes:
        root.insert_node(node)


def find_nodes_to_normalize(root: TreeNode) -> list[TreeNode]:
    seen = set()
    to_normalize = []

    for node in root.iter_nodes():
        added = False
        if node in seen:
            continue
        seen.add(node)
        for child in node.children.values():
            if ColorType.css_name(child) == ColorType.OTHER:
                to_normalize.append(node)
                added = True
                break
        if added:
            for child in node.iter_nodes():
                seen.add(child)
    return to_normalize


def gather_color_statistics(root: TreeNode) -> dict[Color, int]:
    colors = collections.defaultdict(int)
    for node in root.iter_nodes():
        colors[ColorType.color_as_type(node)] += 1

    return colors


def copy_html_resources_without_changes(resources: list[str], to: pathlib.Path):
    for item in resources:
        r = resource.find(str(RESOURCE_PREFIX / item))
        with (to / item).open("wb") as file:
            file.write(r)


def get_css_colors_and_legend(colors: dict[Color, int], n_top=9, levels=10) -> tuple[list[str], list[str]]:
    hint_map = {
        ColorType.DEFAULT: "Total execution time",
        ColorType.MAINTHREAD: "Duration of a stage",
        ColorType.OTHER: "Sum of durations of children or stage duration, what is longer",
    }

    color_list = []
    legend = []
    for color, _ in list(sorted(colors.items(), key=lambda x: -x[1])):
        color_list.append(CSS_TYPE_ENTRY % dict(type=color.type, color=color.color))
        if len(legend) < n_top:
            legend.append(HTML_LEGEND_TEMPLATE.format(entry=color.type, hint=hint_map[color.type]))

    for level in range(levels):
        color_list.append(CSS_LEVEL_ENTRY % dict(level=level, val=level))
    return color_list, legend


def main(opts):
    import app_ctx

    display = common.get_display(sys.stdout)

    file_name, nodes = common.load_evlog(opts, display, app_ctx.evlog.get_latest, check_for_distbuild=True)

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
    full_html = html_with_legend.replace(JSON_PLACEHOLDER, json.dumps({"tree": root.as_dict()}))

    with (directory / INDEX_HTML).open('w') as f:
        f.write(full_html)

    display.emit_message(f'[[imp]]Open bloat/{INDEX_HTML} in your browser.')
