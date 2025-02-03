from devtools.ya.jbuild.gen import consts

import yalibrary.graph.base as graph_base
import yalibrary.graph.const as graph_consts


class Target(object):
    def __init__(self, path, deps):
        self.path = path
        self.deps = deps

    # Jar
    def provides_jar(self):
        return True

    def output_jar_name(self):
        return None

    def output_jar_path(self):
        if self.provides_jar():
            return graph_base.hacked_path_join(graph_consts.BUILD_ROOT, self.path, self.output_jar_name())

        return None

    # Sources jar
    def provides_sources_jar(self):
        return True

    def output_sources_jar_name(self):
        return None

    def output_sources_jar_path(self):
        if self.provides_sources_jar():
            return graph_base.hacked_path_join(graph_consts.BUILD_ROOT, self.path, self.output_sources_jar_name())

        return None

    # Any java sources
    def provides_any_java_sources(self):
        return False

    def provides_any_kotlin_sources(self):
        return False

    def java_sources_paths(self):
        return []

    def kotlin_sources_paths(self):
        return []

    # War
    def provides_war(self):
        return False

    def output_war_name(self):
        return None

    def output_war_path(self):
        if self.provides_war():
            return graph_base.hacked_path_join(graph_consts.BUILD_ROOT, self.path, self.output_war_name())

        return None

    # Aar
    def provides_aar(self):
        return False

    def output_aar_name(self):
        return None

    def output_aar_path(self):
        if self.provides_aar():
            return graph_base.hacked_path_join(graph_consts.BUILD_ROOT, self.path, self.output_aar_name())

        return None

    # Dll
    def provides_dll(self):
        return False

    def output_dll_name(self):
        return None

    def output_dll_path(self):
        if self.provides_dll():
            return graph_base.hacked_path_join(graph_consts.BUILD_ROOT, self.path, self.output_dll_name())

        return None

    # Common
    def is_idea_target(self):
        return False

    def provides_jar_of_type(self, type):
        if type == consts.CLS:
            return self.provides_jar()

        elif type == consts.SRC:
            return self.provides_sources_jar()

        elif type == consts.WAR:
            return self.provides_war()

        elif type == consts.AAR:
            return self.provides_aar()

        else:
            raise Exception()

    def output_jar_of_type_name(self, type):
        if type == consts.CLS:
            return self.output_jar_name()

        elif type == consts.SRC:
            return self.output_sources_jar_name()

        elif type == consts.WAR:
            return self.output_war_name()

        elif type == consts.AAR:
            return self.output_aar_name()

        else:
            raise Exception()

    def output_jar_of_type_path(self, type):
        if type == consts.CLS:
            return self.output_jar_path()

        elif type == consts.SRC:
            return self.output_sources_jar_path()

        elif type == consts.WAR:
            return self.output_war_path()

        elif type == consts.AAR:
            return self.output_aar_path()

        else:
            raise Exception()


class YmakeGraphTarget(Target):  # TODO: memoize
    def __init__(self, path, deps, node, graph, plain):
        super(YmakeGraphTarget, self).__init__(path, deps)
        self.node = node
        self.graph = graph
        self.plain = plain

        self.ext_to_output = None

    def output_jar_name(self):
        for out in self.node['outputs']:
            if (
                out.endswith('.jar')
                and not out.endswith('-sources.jar')
                and '/'.join(graph_base.hacked_normpath(out).split('/')[1:-1]) == self.path
            ):
                return graph_base.hacked_normpath(out).split('/')[-1]

        return None

    def provides_jar(self):
        return self.output_jar_name() is not None

    def provides_any_java_sources(self):
        return len(self.java_sources_paths()) > 0

    def provides_any_kotlin_sources(self):
        return len(self.kotlin_sources_paths()) > 0

    def _provides_extensions(self, exts):
        srcs = []

        for out in self.node['outputs']:
            for e in exts:
                if out.endswith(e):
                    srcs.append('/'.join(graph_base.hacked_normpath(out).split('/')[1:]))
                    break

        return srcs

    def java_sources_paths(self):
        return self._provides_extensions(('-sources.jar', '.java', '.jsrc', '.kt'))

    def kotlin_sources_paths(self):
        return self._provides_extensions(('.kt',))

    def output_sources_jar_name(self):
        for out in self.node['outputs']:
            if out.endswith('-sources.jar') and '/'.join(graph_base.hacked_normpath(out).split('/')[1:-1]) == self.path:
                return graph_base.hacked_normpath(out).split('/')[-1]

        return None

    def provides_sources_jar(self):
        return self.output_sources_jar_name() is not None

    def output_dll_name(self):
        names = self.output_dll_names()

        if names:
            return names[0]

        return None

    def output_dll_names(self):
        res = []

        for out in self.node['outputs']:
            for ext in ('.so', '.dylib', '.dll'):
                if (
                    out.endswith(ext)
                    or (ext + '.') in graph_base.hacked_normpath(out).split('/')[-1]
                    and '/'.join(graph_base.hacked_normpath(out).split('/')[1:-1]) == self.path
                ):
                    res.append(graph_base.hacked_normpath(out).split('/')[-1])

        return res

    def output_dll_paths(self):
        return [graph_base.hacked_path_join(graph_consts.BUILD_ROOT, self.path, x) for x in self.output_dll_names()]

    def provides_dll(self):
        return self.output_dll_name() is not None

    def output_war_name(self):
        for out in self.node['outputs']:
            if out.endswith('.war') and '/'.join(graph_base.hacked_normpath(out).split('/')[1:-1]) == self.path:
                return graph_base.hacked_normpath(out).split('/')[-1]

        return None

    def provides_war(self):
        return self.output_war_name() is not None

    def output_aar_name(self):
        for out in self.node['outputs']:
            if out.endswith('.aar') and '/'.join(graph_base.hacked_normpath(out).split('/')[1:-1]) == self.path:
                return graph_base.hacked_normpath(out).split('/')[-1]

        return None

    def provides_aar(self):
        return self.output_aar_name() is not None

    def is_idea_target(self):
        return self.plain is not None
