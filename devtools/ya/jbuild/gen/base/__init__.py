import os
import logging
import collections

import exts.path2 as path2
import devtools.ya.jbuild.gen.consts as consts
from devtools.ya.jbuild.gen import configure

logger = logging.getLogger(__name__)


class Context(object):
    def __init__(
        self,
        opts,
        arc_root,
        by_path,
        global_resources,
    ):
        self.rclosure = set()
        self.opts = opts
        self.arc_root = arc_root
        self.by_path = by_path
        self.errs = collections.defaultdict(configure.PathConfigureError)
        self.global_resources = global_resources


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


def resolve_jacoco_agent(global_resources, opts=None):
    if opts and consts.LOCAL_JACOCO_AGENT_FLAG in opts.flags:
        return opts.flags[consts.LOCAL_JACOCO_AGENT_FLAG]
    return global_resources.get(consts.RESOURCE_JACOCO_AGENT, '$' + consts.RESOURCE_JACOCO_AGENT)


def resolve_kotlin_compiler(global_resources, opts=None):
    if opts and consts.LOCAL_KOTLIN_COMPILER_FLAG in opts.flags:
        return opts.flags[consts.LOCAL_KOTLIN_COMPILER_FLAG]
    return global_resources.get(consts.RESOURCE_KOTLIN_COMPILER, '$' + consts.RESOURCE_KOTLIN_COMPILER)


def resolve_jstyle_lib(global_resources, opts=None):
    if opts and consts.LOCAL_JSTYLE_LIB_FLAG in opts.flags:
        return opts.flags[consts.LOCAL_JSTYLE_LIB_FLAG]
    return global_resources.get(consts.RESOURCE_JSTYLE_LIB, '$' + consts.RESOURCE_JSTYLE_LIB)
