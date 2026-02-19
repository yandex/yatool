import collections
import string

import six

import devtools.ya.jbuild.gen.consts as consts
import devtools.ya.jbuild.gen.java_target2 as jt

import yalibrary.graph.base as graph_base


def strip_root(s):
    return s[3:]


def default_vars(path):
    return {
        'CURDIR': graph_base.hacked_path_join(consts.SOURCE_ROOT, path),
        'ARCADIA_ROOT': consts.SOURCE_ROOT,
        'BINDIR': graph_base.hacked_path_join(consts.BUILD_ROOT, path),
        'ARCADIA_BUILD_ROOT': consts.BUILD_ROOT,
    }


def split(s, replace_escaped_whitespaces=True):
    escaped = False
    quote = None
    cur_string = ''
    result = []

    for c in s:
        if escaped:
            if replace_escaped_whitespaces:
                if c == 'n':
                    cur_string += '\n'

                elif c == 't':
                    cur_string += '\t'

                elif c == 'r':
                    cur_string += '\r'

                else:
                    cur_string += c

            else:
                cur_string += c

            escaped = False

        elif c == '\\':
            escaped = True

        elif c in ['\'', '"']:
            if quote:
                if c == quote:
                    quote = None

                else:
                    cur_string += c

            else:
                quote = c

        elif c in string.whitespace:
            if quote:
                cur_string += c

            elif cur_string:
                result.append(cur_string)
                cur_string = ''

        else:
            cur_string += c

    if cur_string:
        result.append(cur_string)

    return result


class ParseError(Exception):
    mute = True


def is_jtest(plain):
    return consts.JAVA_TEST in plain


def is_junit5(plain):
    return consts.JUNIT5 in plain


def is_junit6(plain):
    return consts.JUNIT6 in plain


def is_jtest_for(plain):
    return consts.JAVA_TEST_FOR in plain


def is_java(plain):
    return (
        consts.JAVA_LIBRARY in plain
        or consts.JAVA_PROGRAM in plain
        or is_jtest(plain)
        or is_jtest_for(plain)
        or is_junit5(plain)
        or is_junit6(plain)
    )


def replace_vars(arg, vars):
    for k, v in six.iteritems(vars):
        arg = arg.replace('${' + k + '}', v)

    return arg


def obtain_targets_graph2(dart, cpp_graph):
    from devtools.ya.jbuild.gen.actions import parse

    by_path = {}
    extra_idea_paths = {}

    def strip_root(s):
        return s[3:]

    def is_module(node):
        props = node.get('target_properties', {})
        return 'module_type' in props or node.get('is_module', False)

    all_java_peerdirs = set()
    all_java_external_srcs = set()

    # Add targets from dart
    for entry in dart:
        path = graph_base.hacked_normpath(strip_root(entry['PATH']))
        module_type = entry['MODULE_TYPE']
        module_args = entry['MODULE_ARGS'].split() if 'MODULE_ARGS' in entry else []
        managed_peers = entry[consts.MANAGED_PEERS].split() if consts.MANAGED_PEERS in entry else []
        managed_peers_closure = (
            entry[consts.MANAGED_PEERS_CLOSURE].split() if consts.MANAGED_PEERS_CLOSURE in entry else []
        )
        non_manageable_peers = (
            entry[consts.NON_NAMAGEABLE_PEERS].split() if consts.NON_NAMAGEABLE_PEERS in entry else []
        )
        jdk_version = entry[consts.JDK_RESOURCE_PREFIX] if consts.JDK_RESOURCE_PREFIX in entry else None
        if jdk_version and not jdk_version.endswith('_DEFAULT'):
            try:
                jdk_version = int(jdk_version[len('JDK') :])
            except Exception:
                jdk_version = None

        if module_type == 'JTEST_FOR' and 'UNITTEST_DIR' in entry:
            module_args = [entry['UNITTEST_DIR']] + module_args

        sbr_resources = []
        arcadia_resources = []
        if 'TEST_DATA' in entry:
            for td in sum(entry['TEST_DATA'], []):
                if td.startswith('sbr://'):
                    sbr_resources.append(td[len('sbr://') :])
                if td.startswith('arcadia/'):
                    arcadia_resources.append(td[len('arcadia/') :])

        all_java_peerdirs |= set(map(strip_root, non_manageable_peers)) | set(map(strip_root, managed_peers_closure))

        for words in entry.get(consts.JAVA_SRCS, []):
            try:
                external, _ = parse.extract_word(words, consts.J_EXTERNAL)
            except ParseError:
                external = None

            if external and external not in consts.JAVA_SRCS_WORDS:
                all_java_external_srcs.add(external)

        plain = collections.defaultdict(list)
        plain.update(entry)
        plain[consts.MANAGED_PEERS] = [managed_peers]
        plain[consts.MANAGED_PEERS_CLOSURE] = [managed_peers_closure]
        plain[consts.NON_NAMAGEABLE_PEERS] = [non_manageable_peers]
        plain[module_type] = [module_args]
        if sbr_resources:
            plain[consts.TEST_DATA_SANDBOX] = [sbr_resources]
        if arcadia_resources:
            plain[consts.TEST_DATA_ARCADIA] = [arcadia_resources]

        plain.pop('BUNDLE_NAME', None)
        plain.pop('PATH', None)
        plain.pop('IDEA_ONLY', None)
        plain.pop('MODULE_TYPE', None)
        plain.pop('MODULE_ARGS', None)
        plain.pop('UNITTEST_DIR', None)
        plain.pop('TEST_DATA', None)
        plain.pop(consts.JDK_RESOURCE_PREFIX, None)

        # Replace ${ARCADIA_ROOT} --> $(SOURCE_ROOT), etc.
        vars = default_vars(path)
        for macro, calls in six.iteritems(plain):
            for args in calls:
                for i in range(len(args)):
                    args[i] = replace_vars(args[i], vars)

        plain['JDK_VERSION_INT'] = jdk_version
        extra_idea_paths[path] = plain

    cpp_node_by_uid = {n['uid']: n for n in cpp_graph['graph']}

    # Add targets from ymake graph
    for node in cpp_graph['graph']:
        if not is_module(node):
            continue

        path = '/'.join(graph_base.hacked_normpath(node['outputs'][0]).split('/')[1:-1])

        if (
            path not in all_java_peerdirs and path not in all_java_external_srcs and path not in extra_idea_paths
        ):  # It's not connected to java graph
            continue

        target = jt.YmakeGraphTarget(path, [], node, cpp_graph, extra_idea_paths.get(path))

        if not (
            target.provides_jar()
            or target.provides_dll()
            or target.provides_aar()
            or target.provides_war()
            or target.provides_any_java_sources()
        ):
            # peerdir from java-module to non-java, non-dll, non-aar, non-war module, ignore
            continue

        by_path[path] = target

        if by_path[path].provides_jar():  # add all it's deps to by_path for correct classpath calculation
            for dep_uid in node['deps']:
                dep_node = cpp_node_by_uid[dep_uid]

                if not is_module(dep_node):
                    continue

                dep_path = '/'.join(graph_base.hacked_normpath(dep_node['outputs'][0]).split('/')[1:-1])

                target = jt.YmakeGraphTarget(dep_path, [], dep_node, cpp_graph, extra_idea_paths.get(dep_path))

                if target.provides_jar():
                    by_path[dep_path] = target

    # Make dependencies
    for path, target in six.iteritems(by_path):
        if target.provides_jar():
            for dep_uid in target.node['deps']:
                dep_node = cpp_node_by_uid[dep_uid]

                if not is_module(dep_node):
                    continue

                dep_path = '/'.join(graph_base.hacked_normpath(dep_node['outputs'][0]).split('/')[1:-1])

                if dep_path in by_path:
                    target.deps.append(by_path[dep_path])

    return by_path
