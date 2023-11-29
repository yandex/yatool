# cython: profile=True

import logging

import collections
import os

import yalibrary.platform_matcher as cp

logger = logging.getLogger(__name__)

PATH_PREFIXES = ['$(BUILD_ROOT)/', '$(SOURCE_ROOT)/']


class BuildPlan(object):
    def __init__(self, graph):
        self._graph = graph
        self._store = set(self._graph['result'])
        self._prepare()

    def _prepare(self):
        self._projects_by_uids = {}
        self._uids_by_projects = collections.defaultdict(list)
        self._uids_by_outputs = collections.defaultdict(list)
        self._nodes_by_uids = {}
        self._uids_by_bundles = {}
        for item in self._graph['graph']:
            self._update_mappings(item)

    @staticmethod
    def _cut_path_type(path):
        for pfx in PATH_PREFIXES:
            if path.startswith(pfx):
                return path[len(pfx):]
        return path

    @staticmethod
    def node_name(item):
        try:
            node_name = BuildPlan._get_module_dir(item)
            if not node_name:
                node_name = os.path.dirname(item['outputs'][0])
            name = BuildPlan._cut_path_type(os.path.normpath(node_name))
        except IndexError:
            name = item['uid']
        return name

    @staticmethod
    def node_platform(item):
        return '-'.join(item.get('tags', [])) or cp.my_platform()

    def get_project_uids(self, project):
        project = os.path.normpath(project)
        uids = self._uids_by_projects.get(project, [])
        bundle_uid = self._uids_by_bundles.get(project)
        if bundle_uid:
            uids.append(bundle_uid)
        return uids

    def _get_target_binary_lang(self, uid):
        return self._nodes_by_uids.get(uid).get('target_properties', {}).get('module_lang')

    def is_target_python3(self, uid):
        return self._get_target_binary_lang(uid) == 'py3'

    def is_target_python2(self, uid):
        return self._get_target_binary_lang(uid) == 'py2'

    def get_target_outputs_by_type(self, uid, module_types, unroll_bundle=False, kvp=None):
        # TODO remove kvp argument after https://st.yandex-team.ru/DEVTOOLS-4660
        kvp = kvp or set()
        seen = set()
        q = collections.deque([uid])
        while q:
            uid = q.popleft()
            if uid in seen:
                continue
            seen.add(uid)

            node = self._nodes_by_uids.get(uid)
            module_type = self._get_module_type(node)
            p = node.get("kv", {}).get("p")
            if module_type in module_types or p in kvp:
                yield uid, node['outputs']
            # unroll bundle deps to find binaries
            elif unroll_bundle and module_type == 'bundle':
                q.extend(node['deps'])

    def get_uids_by_outputs(self, output):
        if output.startswith('$('):
            return self._uids_by_outputs.get(output, [])
        else:
            for pfx in PATH_PREFIXES:
                uids = self._uids_by_outputs.get(pfx + output)
                if uids is not None:
                    return uids
            return []

    def get_projects_by_uids(self, uid):
        return self._projects_by_uids.get(uid)

    def get_conf(self):
        if 'conf' not in self._graph:
            self._graph['conf'] = {}
        return self._graph['conf']

    def get_node_by_uid(self, uid):
        return self._nodes_by_uids.get(uid)

    def _update_mappings(self, item):
        uid = item['uid']
        is_target_platform = not item.get('host_platform', False)

        self._nodes_by_uids[uid] = item

        if is_target_platform:
            for o in item['outputs']:
                self._uids_by_outputs[o].append(uid)

        project = BuildPlan.get_project(item)
        if project:
            self._projects_by_uids[uid] = project
            if is_target_platform:
                name = project[0]
                self._uids_by_projects[name].append(item['uid'])
                bundle_name = item.get('kv', {}).get('bundle_name')
                if bundle_name:
                    bundle_file_name = os.path.join(name, bundle_name)
                    self._uids_by_bundles[bundle_file_name] = uid

    def append_node(self, node, add_to_result=True):
        if add_to_result:
            self._graph['result'].append(node['uid'])
            self._store.add(node['uid'])
        self._graph['graph'].append(node)
        self._update_mappings(node)

    def get_graph(self):
        return self._graph

    @staticmethod
    def _get_module_type(node):
        tp = node.get('target_properties')
        return tp.get('module_type') if tp else None

    @staticmethod
    def _get_module_dir(node):
        return node.get('target_properties', {}).get('module_dir')

    @staticmethod
    def get_module_tag(node):
        return node.get('target_properties', {}).get('module_tag')

    @staticmethod
    def get_project(node):
        module_type = BuildPlan._get_module_type(node)
        if module_type:
            name = BuildPlan.node_name(node)
            module_tag = BuildPlan.get_module_tag(node)
            return (name, BuildPlan.node_platform(node), module_type, module_tag, node.get('tags'))

    def get_context(self):
        conf = self.get_conf()
        if 'context' not in conf:
            conf['context'] = {}
        return conf['context']


class DistbuildGraph(object):
    def __init__(self, graph):
        self._graph = graph
        self._store = set(self._graph['result'])
        self._projects_by_uids = {}  # TODO
        self._not_add_to_report = set()

        for node in self._graph['graph']:
            uid = node['uid']
            project = BuildPlan.get_project(node)
            if project:
                self._projects_by_uids[uid] = project

            kv = node.get('kv')
            if kv and kv.get('add_to_report') is False:
                self._not_add_to_report.add(uid)

    def get_graph(self):
        return self._graph

    def get_targets(self):
        return self._projects_by_uids

    def add_to_report(self, uid):
        return uid not in self._not_add_to_report
