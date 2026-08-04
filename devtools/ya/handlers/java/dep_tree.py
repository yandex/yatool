"""Rendering of the dependency tree ymake produces for `ya java dependency-tree`.

ymake prints the tree as json (`--managed-dep-tree-json`, see ExplainDM in
devtools/ymake/dependency_management.cpp), so nothing here parses the printed tree. A node is

    {"path": …, "status": …, "version"?: …, "module_type"?: …,
     "replaced_from"?: …, "conflict_with"?: …, "children"?: [ … ]}

and the whole document is {"roots": [<node>, …]}. This module adds the numbers the viewer needs
on top of that and renders a standalone html page.
"""

import json
import os
import webbrowser

import library.python.resource as resource

RESOURCE_KEY = '/java_dep_tree/static/index.html'
JSON_PLACEHOLDER = '<!--JSON-->'
TITLE_PLACEHOLDER = '<!--TITLE-->'

FORMAT_TEXT = 'text'
FORMAT_JSON = 'json'
FORMAT_HTML = 'html'
FORMATS = (FORMAT_TEXT, FORMAT_JSON, FORMAT_HTML)

DEFAULT_OUTPUT = {FORMAT_JSON: 'dep-tree.json', FORMAT_HTML: 'dep-tree.html'}


def loads(text):
    return json.loads(text)


def _key(node):
    """Path without the version: contrib/java/junit/junit/4.13 -> contrib/java/junit/junit."""
    path = node['path']
    version = node.get('version')
    return path[: -(len(version) + 1)] if version else path


def collect_stats(roots):
    """Count nodes and find dependencies present in the tree with more than one version."""
    versions = {}
    keys = set()
    total = [0]

    def walk(node):
        total[0] += 1
        key = _key(node)
        keys.add(key)
        version = node.get('version')
        if version:
            by_version = versions.setdefault(key, {})
            by_version[version] = by_version.get(version, 0) + 1
        for child in node.get('children', ()):
            walk(child)

    for root in roots:
        walk(root)

    return {
        'total': total[0],
        'unique': len(keys),
        'versions': versions,
        'conflicts': {key: by_version for key, by_version in versions.items() if len(by_version) > 1},
    }


def to_data(tree):
    roots = tree['roots']
    return {'roots': roots, 'stats': collect_stats(roots)}


def render_html(data, title):
    page = resource.find(RESOURCE_KEY).decode('utf-8')
    # `</` is escaped so that no path can close the <script> tag the data is embedded into.
    payload = json.dumps(data, ensure_ascii=False).replace('</', '<\\/')
    return page.replace(TITLE_PLACEHOLDER, title).replace(JSON_PLACEHOLDER, payload)


def dump(tree, fmt, targets, output=None, open_in_browser=False):
    """Write the tree as json or html, return the path of the file written."""
    if fmt not in (FORMAT_JSON, FORMAT_HTML):
        raise ValueError('Unsupported dependency tree format: {}'.format(fmt))

    data = to_data(tree)
    path = os.path.abspath(output or DEFAULT_OUTPUT[fmt])

    if fmt == FORMAT_JSON:
        content = json.dumps(data, ensure_ascii=False, indent=2)
    else:
        content = render_html(data, 'dependency-tree: {}'.format(', '.join(targets)))

    with open(path, 'w', encoding='utf-8') as afile:
        afile.write(content)

    if open_in_browser:
        webbrowser.open('file://' + path)

    return path
