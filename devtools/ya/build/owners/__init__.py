import os
import collections
import re

import exts.path2

YMAKE_OWNER_LINE_RE = re.compile(r'\$B/(?P<path>.*?)->OWNER=(?P<owners>.*)')
EMPTY_OWNERS = {"logins": [], "groups": []}


def make_logins_and_groups(owners):
    groups = []
    logins = []
    for owner in owners:
        if owner.startswith('rb:'):
            groups.append(owner[3:])
        elif owner.startswith('g:'):
            groups.append(owner[2:])
        else:
            logins.append(owner)
    return logins, groups


def parse_owners(json_graph):
    nodes = collections.deque()
    nodes.append(json_graph)
    owners_list = {'owners': EMPTY_OWNERS.copy(), 'items': dict()}
    while len(nodes) > 0:
        node = nodes.popleft()
        for d in node['deps']:
            nodes.append(d)
            if d['node-type'] == "Property" and d['name'].startswith('OWNER='):
                assert node['name'].startswith('$S/')
                components = os.path.dirname(node['name'][3:]).split(os.path.sep)
                owners = d['name'][len('OWNER='):].split()
                current_item = owners_list
                for component in components:
                    component_item = current_item['items'].get(component)
                    if component_item is None:
                        component_item = {'owners': {'logins': [], "groups": []}, 'items': dict()}
                        current_item['items'][component] = component_item
                    current_item = component_item
                logins, groups = make_logins_and_groups(owners)
                current_item['owners']['logins'] = logins
                current_item['owners']['groups'] = groups

    return owners_list


def parse_ymake_owners(plain_text):
    def extract_path(full_path):
        return os.path.split(full_path)[0]

    result = _gen_node({}, [], [])

    for x in plain_text.splitlines():
        m = YMAKE_OWNER_LINE_RE.match(x)
        if m:
            path = extract_path(m.group('path'))
            owners = re.split('[ ,]+', m.group('owners'))
            groups = [x[3:] for x in owners if x.startswith('rb:')] + [x[2:] for x in owners if x.startswith('g:')]
            logins = [x for x in owners if not x.startswith('rb:') and not x.startswith('g:')]
            add_owner(result, path, logins, groups)

    return result


def _gen_node(items, logins, groups):
    return {
        'items': items,
        'owners': {
            'logins': logins,
            'groups': groups
        }
    }


def add_owner(owners, path, logins, groups):
    if not logins and not groups:
        return

    def inject_owner(node, parts):
        if len(parts) == 0:
            node['owners'] = {
                'logins': logins,
                'groups': groups
            }
            return

        head, tail = parts[0], parts[1:]
        if 'items' not in node:
            node['items'] = {}

        if head not in node['items']:
            node['items'][head] = {'items': {}, 'owners': EMPTY_OWNERS.copy()}

        inject_owner(node['items'][head], tail)

    parts = tuple(filter(len, exts.path2.path_explode(path)))

    inject_owner(owners, parts)


def find_path_owners(owners_list, source_path):
    """
    Gets source path owners
    """

    if not owners_list:
        return EMPTY_OWNERS.copy()

    owners_list_item = owners_list
    # Iterate through components of the path.
    for component in source_path.split(os.path.sep):
        items = owners_list_item.get("items")
        # Try to search for the current component in the owners tree.
        if items and component in items:
            owners_list_item = items[component]
        else:
            # Owners is not specified for the subpath.
            break

    return owners_list_item.get("owners", EMPTY_OWNERS.copy())
