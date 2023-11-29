import os
import logging
import collections

import exts.path2 as path2
import jbuild.gen.consts as consts
import yalibrary.graph.base as base
from jbuild.gen import configure

logger = logging.getLogger(__name__)


def dirname_unix_path(unix_path):
    return '/'.join(unix_path.split('/')[:-1])


def basename_unix_path(unix_path):
    return unix_path.split('/')[-1]


def is_contrib(path, ctx):
    return any(path.startswith(root) for root in ctx.contrib_roots)


def is_proxy_library(path, ctx):
    return is_contrib(path, ctx) and any(dirname_unix_path(dep.path) == path for dep in ctx.by_path[path].deps)


def extract_excludes(paths, ctx):
    excludes = []

    for path in paths:
        target = ctx.by_path[path]

        if target.is_dart_target():
            excludes.extend(sum(target.plain.get(consts.EXCLUDE, []), []))

    return sorted(set(excludes))


def is_excluded(path, exclude):
    return (path.rstrip('/') + '/').startswith(exclude.rstrip('/') + '/')


def strip_root(s):
    return s[3:]


class Context(object):
    def __init__(
        self,
        opts,
        arc_root,
        contrib_roots,
        paths,
        rclosure,
        by_path,
        resolved_sources,
        maven_export_version,
        sonar_paths,
        target_platform,
        global_resources,
    ):
        self.paths = paths
        self.rclosure = rclosure
        self.opts = opts
        self.arc_root = arc_root
        self.contrib_roots = contrib_roots
        self.by_path = by_path

        self.maven_export_version = maven_export_version
        self.sonar_paths = sonar_paths

        self.resolved_sources = resolved_sources
        self.errs = collections.defaultdict(configure.PathConfigureError)
        self.external_maven_import = set()

        self.maven_export_modules_list = set()
        self.maven_test_map = collections.defaultdict(set)

        self._maven_export_result = None
        self.target_platform = target_platform

        self.global_resources = global_resources

    def maven_export_result(self):
        if self._maven_export_result is None:
            self._maven_export_result = set()
            for p in self.rclosure:
                self._maven_export_result |= set(self.classpath(p))

        return self._maven_export_result

    def choose_in_classpath(self, path, accept_target, extract_artifact, direct=False):
        chosen = []
        checked = set()

        def collect(target):
            if not is_proxy_library(target.path, self) and accept_target(target):
                chosen.append(extract_artifact(target))

        def collect_non_java_deps(target):
            for dep in target.deps:
                if dep.provides_jar() or dep in checked:
                    continue
                checked.add(dep)
                collect(dep)

        checked.add(path)
        target = self.by_path[path]
        collect(target)
        if not target.is_dart_target():
            return chosen

        # collect direct non java deps
        collect_non_java_deps(target)

        # collect managed java deps
        classpath = target.plain[consts.MANAGED_PEERS][0] if direct else target.plain[consts.MANAGED_PEERS_CLOSURE][0]
        for classpath_dir in classpath:
            classpath_target = self.by_path[strip_root(classpath_dir)]
            if classpath_target.path in checked:
                continue
            checked.add(classpath_target.path)

            collect(classpath_target)
            if not direct:
                collect_non_java_deps(classpath_target)

        return chosen

    def _filter_classpath(self, classpath, accept_target, extract_artifact):
        chosen = []
        checked = set()

        def collect(target):
            if accept_target(target):
                chosen.append(extract_artifact(target))

        def collect_non_java_deps(target):
            for dep in target.deps:
                if dep.provides_jar() or dep in checked:
                    continue
                checked.add(dep)
                collect(dep)

        for classpath_dir in classpath:
            classpath_target = self.by_path[strip_root(classpath_dir)]
            if classpath_target.path in checked:
                continue
            checked.add(classpath_target.path)

            collect(classpath_target)
            collect_non_java_deps(classpath_target)

        return chosen

    def classpath(self, path, type=consts.CLS, direct=False):
        return self.choose_in_classpath(
            path,
            accept_target=lambda t: t.provides_jar_of_type(type),
            extract_artifact=lambda t: t.output_jar_of_type_path(type),
            direct=direct,
        )

    def filtered_classpath(self, classpath, type=consts.CLS):
        return self._filter_classpath(
            classpath,
            accept_target=lambda t: t.provides_jar_of_type(type),
            extract_artifact=lambda t: t.output_jar_of_type_path(type),
        )

    def dlls(self, path):
        return self.choose_in_classpath(
            path,
            accept_target=lambda t: t.provides_dll(),
            extract_artifact=lambda t: t.output_dll_path(),
        )

    def classpath_dlls(self, classpath):
        return self._filter_classpath(
            classpath,
            accept_target=lambda t: t.provides_dll(),
            extract_artifact=lambda t: t.output_dll_path(),
        )

    def wars(self, path):
        return self.choose_in_classpath(
            path, accept_target=lambda t: t.provides_war(), extract_artifact=lambda t: t.output_war_path()
        )


class GraphMergeException(Exception):
    pass


def log_or_throw(msg, keepon, exception=None):
    if keepon:
        logger.error(msg)

    else:
        if not exception:
            exception = GraphMergeException

        raise exception(msg)


def uniq_last_case(lst, key=None):
    return list(base.uniq_first_case(lst[::-1], key=key))[::-1]


def remove_prefixes(paths):
    import yalibrary.checkout as checkout

    correct_paths = []

    def _pre_action(path, is_native):
        if is_native:
            correct_paths.append(path)

    checkout.PathsTree(paths).traverse(pre_action=_pre_action, skip_non_leaves=True)

    correct_paths = frozenset([base.hacked_normpath(p) for p in correct_paths])

    return [p for p in paths if p in correct_paths]


def group_by(iterable, by):
    g = collections.defaultdict(list)

    for el in iterable:
        g[by(el)].append(el)

    return g


def relativize(path, root=(consts.BUILD_ROOT, consts.SOURCE_ROOT)):
    for r in root:
        if path2.path_startswith(path, r):
            return os.path.relpath(path, r)

    return path


def path_provides_external_jar(path, ctx):
    import jbuild.gen.actions.externals as ext

    if path in ctx.by_path and consts.EXTERNAL_JAR in ctx.by_path[path].plain:
        _candidates, _, _, _ = ext.extract_ej(ctx.by_path[path].plain)
        candidates = []
        for candidate in _candidates:
            if os.path.exists(os.path.join(ctx.arc_root, path, candidate)):
                candidates.append(candidate)
            if os.path.exists(os.path.join(ctx.arc_root, candidate)):
                candidates.append(
                    base.hacked_path_join(
                        *(['..'] * (base.hacked_normpath(path).count('/') + int(bool(path))) + [candidate])
                    )
                )
        return bool(candidates), candidates or None
    return False, None


def path_provides_run_java_program(path, ctx):
    import jbuild.gen.actions.run_programs as rp

    programs = []
    if path in ctx.by_path and consts.RUN_MANAGED in ctx.by_path[path].plain:
        for program in ctx.by_path[path].plain[consts.RUN_MANAGED]:
            _, _, _, current = rp.parse_words(program)
            current = dict(current)
            if None in current.keys():
                current['CMD'] = current[None]
                del current[None]
            programs.append(current)
        return programs, programs or None
    return False, None


def resolve_java_srcs(
    srcdir, include_patterns, exclude_patterns=None, all_resources=False, resolve_kotlin=False, resolve_groovy=False
):
    import jbuild.resolve_java_srcs as resolver

    return resolver.resolve_java_srcs(
        srcdir, include_patterns, exclude_patterns or [], all_resources, resolve_kotlin, resolve_groovy
    )


def resolve_possible_srcdirs(arc_root, targets):
    from jbuild.gen import node
    import jbuild.gen.actions.compile as comp

    srcs = collections.defaultdict(lambda: collections.defaultdict(lambda: ([], [], [], [])))

    for t in targets:
        if not t.is_dart_target():
            continue

        resolve_kotlin = consts.WITH_KOTLIN in t.plain
        resolve_groovy = consts.WITH_GROOVY in t.plain

        for words in t.plain.get(consts.JAVA_SRCS, []):
            is_resource, srcdir, pp, ex, ws, exc = comp.parse_words(words)

            if ex:
                continue

            if not srcdir:
                srcdir = base.hacked_path_join(consts.SOURCE_ROOT, t.path)

            res = node.try_resolve_inp(arc_root, t.path, srcdir)

            if base.in_source(res):
                s, r, k, g = resolve_java_srcs(
                    res.replace(consts.SOURCE_ROOT, arc_root),
                    ws,
                    exclude_patterns=exc,
                    all_resources=is_resource,
                    resolve_kotlin=resolve_kotlin,
                    resolve_groovy=resolve_groovy,
                )

                srcs[t.path][(res, pp)][0].extend(s)
                srcs[t.path][(res, pp)][1].extend(r)
                srcs[t.path][(res, pp)][2].extend(k)
                srcs[t.path][(res, pp)][3].extend(g)

    return srcs


def resolve_jdk(
    global_resources,
    prefix='JDK_DEFAULT',
    prefix_for_tests='_NO_JDK_FOR_TEST_',
    opts=None,
    for_test=False,
    jdk_version=None,
):
    if for_test and opts and consts.LOCAL_JDK_FOR_TESTS_FLAG in opts.flags:
        return opts.flags[consts.LOCAL_JDK_FOR_TESTS_FLAG]
    if opts and consts.LOCAL_JDK_FLAG in opts.flags:
        return opts.flags[consts.LOCAL_JDK_FLAG]

    if jdk_version:
        prefix = '{}{}'.format(consts.JDK_VERSION_PREFIX, jdk_version)
        jdks = [x for x in global_resources if x.startswith(prefix) and x.endswith(consts.JDK_RESOURCE_SUFFIX)]
        if len(jdks) == 1:
            return global_resources[jdks[0]]
        elif len(jdks) > 1:
            raise AssertionError('Found several jdk resources of the same version: {}'.format(jdks))
        else:
            raise AssertionError('Failed to resolve JDK{}. Global resources: {}'.format(jdk_version, global_resources))

    jdk_for_tests_resource = prefix_for_tests + consts.JDK_RESOURCE_SUFFIX
    if for_test and jdk_for_tests_resource in global_resources:
        return global_resources[jdk_for_tests_resource]

    jdk_resource = prefix + consts.JDK_RESOURCE_SUFFIX
    if jdk_resource in global_resources:
        return global_resources[jdk_resource]

    raise AssertionError('Failed to resolve jdk: {}'.format(global_resources))


def resolve_uberjar(global_resources, opts=None):
    if opts and consts.LOCAL_UBERJAR_FLAG in opts.flags:
        return opts.flags[consts.LOCAL_UBERJAR_FLAG]
    return global_resources.get(consts.RESOURCE_UBERJAR, '$' + consts.RESOURCE_UBERJAR)


def resolve_error_prone(global_resources, opts=None):
    if opts and consts.LOCAL_ERROR_PRONE_FLAG in opts.flags:
        return opts.flags[consts.LOCAL_ERROR_PRONE_FLAG]
    return global_resources.get(consts.RESOURCE_ERROR_PRONE, '$' + consts.RESOURCE_ERROR_PRONE)


def resolve_jacoco_agent(global_resources, opts=None):
    if opts and consts.LOCAL_JACOCO_AGENT_FLAG in opts.flags:
        return opts.flags[consts.LOCAL_JACOCO_AGENT_FLAG]
    return global_resources.get(consts.RESOURCE_JACOCO_AGENT, '$' + consts.RESOURCE_JACOCO_AGENT)


def resolve_kotlin_compiler(global_resources, opts=None):
    if opts and consts.LOCAL_KOTLIN_COMPILER_FLAG in opts.flags:
        return opts.flags[consts.LOCAL_KOTLIN_COMPILER_FLAG]
    return global_resources.get(consts.RESOURCE_KOTLIN_COMPILER, '$' + consts.RESOURCE_KOTLIN_COMPILER)


def resolve_groovy_compiler(global_resources, opts=None):
    if opts and consts.LOCAL_GROOVY_COMPILER_FLAG in opts.flags:
        return opts.flags[consts.LOCAL_GROOVY_COMPILER_FLAG]
    return global_resources.get(consts.RESOURCE_GROOVY_COMPILER, '$' + consts.RESOURCE_GROOVY_COMPILER)


def resolve_jstyle_lib(global_resources, opts=None):
    if opts and consts.LOCAL_JSTYLE_LIB_FLAG in opts.flags:
        return opts.flags[consts.LOCAL_JSTYLE_LIB_FLAG]
    return global_resources.get(consts.RESOURCE_JSTYLE_LIB, '$' + consts.RESOURCE_JSTYLE_LIB)
