import os
import logging

from collections import defaultdict

from . import mk_common
from . import mk_parser
from . import mk_builder
from . import macro_definitions

logger = logging.getLogger(__name__)


MAKELIST_NAME = 'ya.make'


def from_file(path, keep_on=False):
    try:
        builder = mk_builder.Builder()
        parser = mk_parser.Parser(builder)
        parser.parse_file(path)
    except mk_common.MkLibException:
        logger.error('Fail build tree from makefile - {}'.format(path))
        if keep_on:
            import traceback

            logger.error(traceback.format_exc())
        else:
            raise
    return builder.root


def from_str(raw_data, keep_on=False):
    try:
        builder = mk_builder.Builder()
        parser = mk_parser.Parser(builder)
        parser.parse(raw_data)
    except mk_common.MkLibException:
        logger.error('Fail build tree from makefile content\n{}'.format(raw_data))
        if keep_on:
            import traceback

            logger.error(traceback.format_exc())
        else:
            raise
    return builder.root


class Arcadia(object):
    def __init__(self, arc_root, makelist_name=MAKELIST_NAME):
        self.arc_root = arc_root
        self.makelist_name = makelist_name

    def __getitem__(self, path):
        # type: (str) -> ArcProject
        full_path = os.path.normpath(os.path.join(self.arc_root, path))
        if not os.path.exists(full_path):
            raise KeyError('error: project - {path} does not exist in arcadia'.format(path=path))

        return ArcProject(self.arc_root, path, self.makelist_name)

    def exists(self, path):
        return os.path.exists(os.path.join(self.arc_root, path, self.makelist_name))

    def walk_projects(self, func, visit_peerdirs=True, visit_tests=True, paths=None):
        proj_walk = self._ProjectsWalker(self, visit_peerdirs=visit_peerdirs, visit_tests=visit_tests)
        for path in paths or ['']:
            proj_walk.walk(path, func)

    class _ProjectsWalker(object):
        def __init__(self, arcadia, visit_peerdirs, visit_tests):
            self.arcadia = arcadia
            self.passed = set()
            self.visit_peerdirs = visit_peerdirs
            self.visit_tests = visit_tests

        def walk(self, path, func):
            if path in self.passed:
                return
            else:
                self.passed.add(path)
            if not self.arcadia.exists(path) or not self.arcadia[path].makelistpath():
                return

            makelist = self.arcadia[path].makelist()

            if makelist.has_project():
                func(path)

            makelist_sets = sets_values(makelist, self.arcadia.arc_root)
            recurses = get_paths(makelist, 'RECURSE', makelist_sets)
            rel_recurses = get_paths(makelist, 'RECURSE_ROOT_RELATIVE', makelist_sets)

            if self.visit_peerdirs:
                peerdirs = get_paths(makelist, 'PEERDIR', makelist_sets)
                for peer in peerdirs:
                    self.walk(peer, func)

            if self.visit_tests:
                test_recurses = get_paths(makelist, 'RECURSE_FOR_TESTS', makelist_sets)
                for recurse in test_recurses:
                    self.walk(os.path.join(path, recurse), func)

            for recurse in rel_recurses:
                self.walk(recurse, func)

            for recurse in recurses:
                self.walk(os.path.join(path, recurse), func)


class ArcProject(object):
    def __init__(self, arc_root, path, makelist_name=MAKELIST_NAME):
        self.arc_root = arc_root
        self.path = path
        self.makelist_name = makelist_name

    def project_path(self):
        project_path = os.path.join(self.arc_root, self.path)
        if os.path.exists(project_path):
            return project_path
        return None

    def makelistpath(self):
        mklist = os.path.join(self.arc_root, self.path, self.makelist_name)
        if os.path.exists(mklist):
            return mklist
        else:
            return None

    def makelist(self):
        mklist = self.makelistpath()
        if mklist:
            return from_file(mklist)
        else:
            return macro_definitions.MakeList('root')

    def write_makelist(self, makelist, name=None):
        # type: (macro_definitions.MakeList, str | None) -> None
        makelist_name = name or self.makelist_name
        mklist = os.path.join(self.arc_root, self.path, makelist_name)
        logger.debug("Path: %s", mklist)
        makelist.write(mklist)


def get_paths(makelist, macro_name, sets):
    nodes = makelist.find_siblings(macro_name)

    paths = []
    for node in nodes:
        current_values = node.get_values()
        for value in current_values:
            string = value.name
            chunks = string.split()
            if len(chunks) > 1 and chunks[0] == 'ADDINCL':
                string = chunks[1]
            values = substitute_set_vars(string, sets)
            paths.extend(values)

    return paths


def sets_values(makelist, arc_root):
    nodes = makelist.find_siblings('SET')
    set_dict = defaultdict(list, {'ARCADIA_ROOT': [arc_root]})

    for set_ in nodes:
        values = set_.get_values()
        if values:
            list_vals = []
            for value in values:
                chunks = value.name.split()
                list_vals.extend(chunks)
                if list_vals[1:]:
                    set_dict[list_vals[0]] = list_vals[1:]

    return set_dict


def get_patterns(string):
    patterns = []
    beg = string.find('${')
    while beg != -1:
        end = string.find('}', beg)
        if end == -1:
            raise mk_common.MkLibException('Unexpected end of pattern at {string}'.format(string=string))
        patterns.append(string[beg : end + 1])
        beg = string.find('${', end)

    return patterns


def substitute_set_vars(string, sets):
    ret_values = []

    while string.count('${') > 0:
        patterns = get_patterns(string)
        for pattern in patterns:
            set_name = pattern[2:-1]
            replacement_values = sets.get(set_name)
            if not replacement_values:
                # logger.warning('Unknown set value - {val}'.format(val=pattern))
                return []

            if len(replacement_values) > 1 and string != pattern:
                logger.error('Bad set value - {val} at {string}'.format(val=pattern, string=string))
                return []

            replacement_string = ' '.join(replacement_values)
            string = string.replace(pattern, replacement_string)

    ret_values.extend(string.split())

    return ret_values
