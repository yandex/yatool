import copy
import os
import logging
import collections
import io
import itertools
import json
import shutil
import os.path as op
import xml.etree.ElementTree as et
import xml.sax.saxutils as saxutils

import six
import devtools.ya.jbuild.gen.base as base
import devtools.ya.jbuild.gen.consts as consts
import devtools.ya.jbuild.gen.node as node
import devtools.ya.jbuild.gen.makelist_parser2 as mp2
import yalibrary.graph.base as graph_base
import exts.archive as archive
import exts.fs as fs
import exts.path2 as path2
from . import compile
from . import funcs
import devtools.ya.core.resource as resource
import sys
import re
import yalibrary.tools as tools
import devtools.ya.build.targets

logger = logging.getLogger(__name__)


class ModulesStructureException(Exception):
    mute = True


MODULE_DIR = '$MODULE_DIR$'
PROJECT_DIR = '$PROJECT_DIR$'

UNIQUE_JVM_ARGS_RE = [
    re.compile(i)
    for i in (
        '-D[^=]+=[^=]+',
        '-verbose:[class|module|gc|jni]',
        '-ea.+',
        '-enableassertions.+',
        '-da.+',
        '-disableassertions.+',
        '-esa',
        '-enablesystemassertions',
        '-dsa',
        '-disablesystemassertions',
        '-agentlib:.+',
        '-agentpath:.+',
        '-javaagent:.+',
        '--enable-preview',
    )
]


class OrderedXMLTreeBuilder(et.XMLParser):
    def _start_list(self, tag, attrib_in):
        fixname = self._fixname
        tag = fixname(tag)
        attrib = collections.OrderedDict()
        if attrib_in:
            for i in range(0, len(attrib_in), 2):
                attrib[fixname(attrib_in[i])] = self._fixtext(attrib_in[i + 1])
        return self._target.start(tag, attrib)


class Module(object):
    def __init__(
        self,
        name,
        path,
        contents,
        dep_paths,
        dep_scopes,
        processors,
        javac_flags,
        test_data=None,
        is_content_root=False,
        jvm_args=None,
        kotlinc_args=None,
        jdk_version=None,
        with_preview=False,
    ):
        self.name = name
        self.path = path
        self.contents = contents
        self.dep_paths = dep_paths
        self.dep_scopes = dep_scopes
        self.processors = processors
        self.javac_flags = javac_flags
        self.is_content_root = is_content_root
        self.jvm_args = jvm_args or []
        self.kotlinc_args = kotlinc_args or []
        self.test_data = {
            (
                graph_base.hacked_path_join(path, 'sbr_test_data', i[0]) if i[1] == consts.TEST_DATA_SANDBOX else i[0],
                i[1],
            )
            for i in (test_data or set())
        }
        self.jdk_version = jdk_version
        self.with_preview = with_preview


class Lib(object):
    def __init__(self, name, long_name, classes_path, sources_path):
        self.name = name
        self.long_name = long_name
        self.classes_path = classes_path
        self.sources_path = sources_path


class Root(object):
    def __init__(self, path, prefix, is_test, ignored, generated, is_resource):
        self.path = path
        self.prefix = prefix
        self.is_test = is_test
        self.ignored = ignored
        self.generated = generated
        self.is_resource = is_resource


class Cotent(object):
    def __init__(self, path, roots):
        self.path = path
        self.roots = roots


def strip_root(s):
    return s[3:]


def forced_lib(path, ctx):
    return ctx.opts.local and (path not in ctx.rclosure or path in ['devtools/junit-runner', 'devtools/junit5-runner'])


def is_test(plain):
    return mp2.is_jtest_for(plain) or mp2.is_jtest(plain) or mp2.is_junit5(plain)


def idea_results(ctx, nodes):
    res = set()
    paths = set()

    node_by_output = {}
    for n in nodes:
        for o in n.outs:
            node_by_output[o] = n

    def before(n):
        assert n.path in ctx.by_path
        target = ctx.by_path[n.path]

        paths.add(n.path)

        # Traverse only jar-providing nodes
        assert target.provides_jar()
        assert (target.output_jar_path(), node.FILE) in n.outs

    def provides_lib(target):
        assert target.provides_jar()

        if not target.is_idea_target():
            return True

        elif consts.JAVA_SRCS not in target.plain and not is_test(
            target.plain
        ):  # Jar from EXTERNAL_JAR comes as Library
            return True

        elif forced_lib(target.path, ctx):  # Only rclosure paths are Modules in local mode
            return True

        return False

    def ideps(n):
        target = ctx.by_path[n.path]

        if target.plain is not None and consts.NON_NAMAGEABLE_PEERS in target.plain:
            for peer in target.plain[consts.NON_NAMAGEABLE_PEERS][0]:
                peer = ctx.by_path.get(strip_root(peer))
                if peer and peer.provides_dll():
                    res.add(node_by_output[(peer.output_dll_path(), node.FILE)])

        if provides_lib(target):
            res.add(n)

            if target.provides_sources_jar() and ctx.opts.dump_sources:
                assert (target.output_sources_jar_path(), node.FILE) in node_by_output
                res.add(node_by_output[(target.output_sources_jar_path(), node.FILE)])

            return

        if target.is_idea_target():
            for peer in target.plain[consts.MANAGED_PEERS_CLOSURE][0]:
                peer = ctx.by_path[strip_root(peer)]
                if peer.provides_jar():
                    yield node_by_output[(peer.output_jar_path(), node.FILE)]
            return

        for dep_node in n.deps:
            dep_path = dep_node.path
            dep_target = ctx.by_path[dep_path]

            if not dep_target.provides_jar():  # peerdir to dll/war/other, ignore TODO: add to result for rebuilds?
                # Works only if jar nodes depend on dll nodes!
                if dep_target.provides_dll():
                    res.add(dep_node)

                continue

            if (dep_target.output_jar_path(), node.FILE) not in dep_node.outs:  # Module src inputs
                res.add(dep_node)
                continue

            else:  # Module classpath
                yield dep_node

    initials = []
    for path in ctx.rclosure:
        target = ctx.by_path[path]

        if not target.provides_jar() or provides_lib(target):
            continue

        assert (target.output_jar_path(), node.FILE) in node_by_output
        initials.append(node_by_output[(target.output_jar_path(), node.FILE)])

    graph_base.traverse(initials, before=before, ideps=ideps)

    return res, paths


def idea_template(name):
    s = resource.try_get_resource(name)

    if s is None:
        p = op.join(op.dirname(__file__), '..', '..', 'idea_templates', name + '.xml')

        if not os.path.exists(p) or os.path.isdir(p):
            raise Exception('Can\'t find {} template.'.format(name))

        with open(p) as f:
            s = f.read()

    return et.fromstring(s)


def empty_iml():
    return idea_template('jbuild/idea_templates/iml.xml')


def empty_ipr(minimal=False):
    return idea_template('jbuild/idea_templates/' + ('ipr-min.xml' if minimal else 'ipr.xml'))


def empty_iws():
    return idea_template('jbuild/idea_templates/iws.xml')


def empty_directory_based_project():
    return {
        '.idea/compiler.xml': idea_template('jbuild/idea_templates/compiler.xml'),
        '.idea/encodings.xml': idea_template('jbuild/idea_templates/encodings.xml'),
        '.idea/misc.xml': idea_template('jbuild/idea_templates/misc.xml'),
        '.idea/modules.xml': idea_template('jbuild/idea_templates/modules.xml'),
        '.idea/workspace.xml': idea_template('jbuild/idea_templates/workspace.xml'),
        '.idea/kotlinc.xml': idea_template('jbuild/idea_templates/kotlinc.xml'),
        '.idea/vcs.xml': idea_template('jbuild/idea_templates/vcs.xml'),
    }


def fix_windows(pth):
    return pth.replace('\\', '/')


def get_backup_name(origin_name):
    for i in range(100):
        candidate = origin_name + '_{:03d}'.format(i)
        if not os.path.exists(candidate):
            return candidate
    return None


def print_pretty(root, path):
    class StringOrderEntry(str):
        def __init__(self, *args, **kwargs):
            self.priority_map = {}
            super(StringOrderEntry, self).__init__(*args, **kwargs)

        def __lt__(self, other):
            if str(self) in self.priority_map and other in self.priority_map:
                return self.priority_map[str(self)] - self.priority_map[other] < 0
            if str(self) in self.priority_map:
                return True
            if other in self.priority_map:
                return False
            return super(StringOrderEntry, self).__lt__(other)

        def __gt__(self, other):
            return not self.__lt__(other)

    class HackedDict(collections.OrderedDict):
        def items(self):
            priority_map = {attr: id for id, attr in enumerate(self.keys())}
            result = []
            for k, v in super(HackedDict, self).items():
                if isinstance(k, str):
                    k = StringOrderEntry(k)
                    k.priority_map = priority_map
                result.append((k, v))
            return result

    def indent(elem, level=0):
        elem.attrib = HackedDict(elem.attrib)
        i = "\n" + level * "  "
        if len(elem):
            if not elem.text or not elem.text.strip():
                elem.text = i + "  "
            if not elem.tail or not elem.tail.strip():
                elem.tail = i
            for elem in elem:
                indent(elem, level + 1)
            if not elem.tail or not elem.tail.strip():
                elem.tail = i
        else:
            if level and (not elem.tail or not elem.tail.strip()):
                elem.tail = i

    indent(root)
    temp_out = io.BytesIO()
    et.ElementTree(root).write(temp_out, xml_declaration=True, encoding="UTF-8")
    content = (
        temp_out.getvalue()
        .replace(b"<?xml version='1.0' encoding='UTF-8'?>", b'<?xml version="1.0" encoding="UTF-8"?>')
        .strip()
    )
    try:
        cont = six.text_type(content, "utf-8")
    except UnicodeEncodeError:
        msg = '''ya ide idea: can't encode characters for {}'''.format(path)
        backup_path = path + '_backup'
        if os.path.exists(backup_path):
            os.remove(backup_path)
        if os.path.exists(path):
            shutil.move(path, backup_path)
            msg += '''\n           original file was moved to {}'''.format(backup_path)
        msg += '''\n           contact devtools@ for details'''
        raise Exception(msg)
    try:
        backup_name = get_backup_name(path)
        if backup_name and os.path.exists(path):
            shutil.copy2(path, backup_name)
        with io.open(path, 'w', newline='\n', encoding='utf-8') as out:
            out.write(cont)
        if backup_name and os.path.exists(backup_name):
            os.remove(backup_name)
    except UnicodeEncodeError:
        msg = '''ya ide idea: UnicodeEncodeError, can't save file {}'''.format(path)
        msg += '''\n    origin file was copied to {}'''.format(backup_name)
        encoded_content = cont.encode('utf-8')
        try:
            with io.open(path, 'wb') as out:
                out.write(encoded_content)
        except Exception as e:
            msg += '''\nWrite as binary: error\n{}'''.format(e)
        msg += '''\ncontact devtools@ for details'''
        raise Exception(msg)


def create_iml(path, by_path, project_root, ctx):
    m = by_path[path]

    iml_path = op.join(project_root if ctx.opts.iml_in_project_root else ctx.opts.arc_root, m.path, m.name) + '.iml'
    fs.ensure_dir(os.path.dirname(iml_path))

    try:
        with open(iml_path) as f:
            iml = et.parse(f, OrderedXMLTreeBuilder()).getroot()

    except Exception:
        iml = empty_iml()

    c = iml.find('./component[@name="NewModuleRootManager"]')

    language_level = None
    if m.jdk_version:
        language_level = 'JDK_' + str(m.jdk_version)
        if m.with_preview:
            language_level += '_PREVIEW'

    if c is None:
        attrib = collections.OrderedDict()
        attrib['name'] = 'NewModuleRootManager'
        if language_level:
            attrib['LANGUAGE_LEVEL'] = language_level
        attrib['inherit-compiler-output'] = 'true'
        c = et.SubElement(iml, 'component', attrib=attrib)
    elif language_level:
        new_attrib = collections.OrderedDict()
        old_attrib = copy.deepcopy(c.attrib)
        new_attrib['name'] = 'NewModuleRootManager'
        new_attrib['LANGUAGE_LEVEL'] = language_level
        for k, v in old_attrib.items():
            if k not in ('name', 'LANGUAGE_LEVEL'):
                new_attrib[k] = v
        c.attrib = new_attrib

    for cont in c.findall('content'):
        c.remove(cont)

    exclude_dirs = (
        sum(ctx.by_path[path].plain.get(consts.IDEA_EXCLUDE_DIRS, []), [])
        if not m.is_content_root and ctx.by_path.get(path, None)
        else []
    )
    resource_dirs = (
        sum(ctx.by_path[path].plain.get(consts.IDEA_RESOURCE_DIRS, []), [])
        if not m.is_content_root and ctx.by_path.get(path, None)
        else []
    )

    if ctx.opts.exclude_dirs:
        exclude_dirs += [i for i in ctx.opts.exclude_dirs if i not in exclude_dirs]

    if ctx.opts.auto_exclude_symlinks:
        module_root = os.path.join(ctx.opts.arc_root, path)
        candidates = set(exclude_dirs)
        for root, dirs, _ in os.walk(module_root):
            for d in dirs:
                abs_path = os.path.join(module_root, root, d)
                candidate = os.path.normpath(os.path.join(root, d))
                if os.path.islink(abs_path):
                    if not any(i for i in candidates if candidate.startswith(i)):
                        candidates.add(candidate)
        exclude_dirs += list(sorted(candidates))

    contains_test = False
    for cr in sorted(m.contents, key=lambda x: x.path.replace(PROJECT_DIR, ctx.opts.arc_root).replace('\\', '/')):
        if path2.path_startswith(cr.path, PROJECT_DIR):
            if ctx.opts.iml_in_project_root:
                cr.path = op.join(MODULE_DIR, op.relpath(cr.path, op.join(PROJECT_DIR, path)))
            else:
                cr.path = cr.path.replace(PROJECT_DIR, project_root)
        elif ctx.opts.iml_in_project_root and ctx.opts.iml_keep_relative_paths:
            cr.path = op.join(MODULE_DIR, op.relpath(cr.path, os.path.dirname(os.path.abspath(iml_path))))

        cont = et.SubElement(c, 'content', attrib={'url': 'file://' + fix_windows(cr.path)})

        for exc in exclude_dirs:
            if ctx.opts.iml_in_project_root:
                exc_path = op.normpath(op.join(cr.path, exc))
            else:
                exc_path = os.path.join(cr.path, exc).replace(PROJECT_DIR, project_root)
            et.SubElement(cont, 'excludeFolder').attrib = {'url': 'file://' + fix_windows(exc_path)}

        for res in resource_dirs:
            if ctx.opts.iml_in_project_root and ctx.opts.iml_keep_relative_paths:
                res_path = op.join(cr.path, res)
            elif ctx.opts.iml_in_project_root:
                res_path = op.join(MODULE_DIR, op.relpath(cr.path, os.path.join(project_root, path)), res)
            else:
                res_path = os.path.join(cr.path, res).replace(PROJECT_DIR, project_root)
            attrib = collections.OrderedDict()
            attrib['url'] = 'file://' + fix_windows(res_path)
            attrib['type'] = 'java-resource'
            et.SubElement(cont, 'sourceFolder').attrib = attrib

        for root in sorted(
            cr.roots,
            key=lambda x: (x.path.replace('\\', '/'), x.ignored, x.is_resource, x.is_test, x.prefix, x.generated),
        ):
            if root.is_test:
                contains_test = True

            if path2.path_startswith(root.path, PROJECT_DIR):
                if ctx.opts.iml_in_project_root and ctx.opts.iml_keep_relative_paths and root.generated:
                    relpath_temp = os.path.relpath(root.path, PROJECT_DIR)
                    root.path = op.join(
                        MODULE_DIR, op.relpath('.', os.path.join('.', os.path.dirname(relpath_temp))), relpath_temp
                    )
                    cont.attrib['url'] = 'file://' + fix_windows(root.path)
                elif ctx.opts.iml_in_project_root:
                    root.path = op.join(MODULE_DIR, op.relpath(root.path, op.join(PROJECT_DIR, path)))
                else:
                    root.path = root.path.replace(PROJECT_DIR, project_root)
            elif ctx.opts.iml_in_project_root and ctx.opts.iml_keep_relative_paths:
                root.path = op.join(MODULE_DIR, op.relpath(root.path, os.path.dirname(os.path.abspath(iml_path))))

            if root.ignored:
                et.SubElement(cont, 'excludeFolder').attrib = {'url': 'file://' + fix_windows(root.path)}

            else:
                rp = root.path

                attr = collections.OrderedDict()
                attr['url'] = 'file://' + fix_windows(rp)

                if root.is_resource:
                    if root.is_test:
                        attr['type'] = 'java-test-resource'

                    else:
                        attr['type'] = 'java-resource'

                else:
                    attr['isTestSource'] = 'true' if root.is_test else 'false'

                if root.prefix:
                    attr['packagePrefix'] = root.prefix

                if root.generated:
                    attr['generated'] = 'true'

                et.SubElement(cont, 'sourceFolder').attrib = attr
        if m.test_data:
            if ctx.opts.iml_in_project_root and ctx.opts.iml_keep_relative_paths:
                test_data_path = op.join(MODULE_DIR, op.relpath('.', path), '..', path, 'test_data')
            else:
                test_data_path = op.join(os.path.abspath(project_root), path, 'test_data')
            et.SubElement(cont, 'excludeFolder').attrib = {'url': 'file://' + fix_windows(test_data_path)}

    application_level_entries = []

    for d in c.findall('orderEntry'):
        c.remove(d)
        if d.get('type') not in ['module', 'library']:
            c.append(d)
        if d.get('level') == 'application':
            application_level_entries.append(d)

    for p, s in zip(m.dep_paths, m.dep_scopes):
        if isinstance(by_path[p], Module):
            attrib = collections.OrderedDict()
            attrib['type'] = 'module'
            attrib['module-name'] = by_path[p].name
            if s != 'COMPILE':
                attrib['scope'] = s

            et.SubElement(c, 'orderEntry', attrib=attrib)

        else:
            assert isinstance(by_path[p], Lib)

            attrib = collections.OrderedDict()
            attrib['type'] = 'library'
            if s != 'COMPILE':
                attrib['scope'] = s
            attrib['name'] = by_path[p].long_name
            attrib['level'] = 'project'

            et.SubElement(c, 'orderEntry', attrib=attrib)

    for entry in application_level_entries:
        c.append(entry)

    fs.create_dirs(op.join(project_root, path))
    print_pretty(iml, iml_path)
    if contains_test and ctx.opts.generate_tests_run:
        create_run_configuration(path, m.name, project_root, ctx, m.jvm_args)


def get_proj_dll_path(opts, project_root):
    projrelpath = '' if not opts.idea_files_root else os.path.relpath(project_root, opts.idea_files_root)
    return graph_base.hacked_normpath(graph_base.hacked_path_join('$PROJECT_DIR$', projrelpath, 'dlls'))


def create_run_configuration(path, module_name, project_root, ctx, jvm_args):
    conf_dir = os.path.join(project_root, '.idea', 'runConfigurations')
    fs.create_dirs(conf_dir)
    conf_name = '-'.join(
        ['autogenerated'] + ([i for i in graph_base.hacked_normpath(path).split('/') if i and i.strip('.')])
    )
    conf_filename = os.path.join(conf_dir, conf_name + '.xml')
    dll_path = get_proj_dll_path(ctx.opts, project_root)
    vm_options = [i.replace('$(BUILD_ROOT)', '$PROJECT_DIR$') for i in jvm_args]
    if '-ea' not in vm_options:
        vm_options += ['-ea']
    for k in (
        '-Djava.library.path',
        '-Djna.library.path',
    ):
        if k not in vm_options:
            vm_options += [k + '=' + dll_path]
    with open(conf_filename, 'w') as cf:
        cf.write(
            '''<component name="ProjectRunConfigurationManager">
  <configuration default="false" name="{}" type="JUnit" factoryName="JUnit">
    <module name="{}" />
    <option name="MAIN_CLASS_NAME" value="" />
    <option name="METHOD_NAME" value="" />
    <option name="TEST_OBJECT" value="pattern" />
    <option name="VM_PARAMETERS" value="{}" />
    <option name="PARAMETERS" value="" />
    <patterns>
      <pattern testClass=".*" />
    </patterns>
    <method v="2">
      <option name="Make" enabled="true" />
    </method>
  </configuration>
</component>'''.format(
                conf_name, module_name, ' '.join([saxutils.escape(i).replace('"', '&quot;') for i in vm_options])
            )
        )


def create_plugin_config(project_root, ctx):
    config_path = op.join(ctx.opts.idea_files_root or project_root, 'ya-settings.xml')
    root = et.Element('root')
    cmd = et.SubElement(root, 'cmd')
    for c in sys.argv:
        et.SubElement(cmd, 'part').text = c
    abs_cwd = os.getcwd()
    et.SubElement(root, 'cwd').text = abs_cwd
    if ctx.opts.regenerate_with_project_update or ctx.opts.project_update_kind or ctx.opts.project_update_targets:
        project_update = et.SubElement(root, 'project_update')
        et.SubElement(project_update, 'cwd').text = abs_cwd
        if ctx.opts.project_update_kind:
            et.SubElement(project_update, 'kind').text = ctx.opts.project_update_kind
        dirs = et.SubElement(project_update, 'dirs')
        if ctx.opts.project_update_targets:
            for target in ctx.opts.project_update_targets:
                et.SubElement(dirs, 'dir').text = target
        else:
            for target in ctx.opts.abs_targets:
                et.SubElement(dirs, 'dir').text = op.relpath(target, abs_cwd)

    print_pretty(root, config_path)


def detect_jdk(ctx):
    def find_jdk_version(ctx):
        versions = list()
        for target in ctx.by_path.values():
            if target.is_idea_target() and consts.JDK_VERSION in target.plain:
                versions.append(target.plain.get(consts.JDK_VERSION))
        if not versions:
            return ''
        return str(max(versions))

    jdk_version_found = find_jdk_version(ctx)
    if ctx.opts.idea_jdk_version:
        jdk_version = ctx.opts.idea_jdk_version
    elif jdk_version_found:
        jdk_version = jdk_version_found
    else:
        jdk_version = '11'

    language_level = ctx.opts.flags.get('JDK_LANGUAGE_LEVEL', '')
    sdk_default_language_level = 'false' if language_level else 'true'
    if language_level and not language_level.startswith('JDK_'):
        language_level = 'JDK_' + language_level

    if jdk_version == '8':
        language_level = language_level or 'JDK_1_8'
        jdk_name = '1.8'
    else:
        language_level = language_level or ('JDK_' + jdk_version)
        jdk_name = jdk_version

    kotlin_target = jdk_version
    if int(kotlin_target) >= 25:
        kotlin_target = '24'  # remove when kotlin starts supporting jdk24 bytecode

    return language_level, sdk_default_language_level, jdk_name, kotlin_target


def get_vcs(arc_root):
    from yalibrary.vcs import detect

    vcs_type, _, _ = detect([arc_root])

    if not vcs_type:
        return 'svn'

    if vcs_type[0] == 'arc':
        return 'Arc'  # IntelliJ plugin uses VCS name "Arc", not "arc"

    return vcs_type[0]


def has_parent_module(module, all_roots):
    for cr in module.contents:
        if any(r.generated for r in cr.roots):
            return False
        parts = cr.path.split(op.sep)
        ok = False
        while parts:
            parts = parts[:-1]
            parent_path = op.sep.join(parts)
            if parent_path in all_roots:
                ok = True
                break
        if not ok:
            return False
    return True


def get_modules_and_libs(by_path, project_root, ctx):
    modules, libs = {}, set()

    all_roots = {
        cr.path
        for v in by_path.values()
        # Ignore content-root modules to preserve current grouping behavior: content roots are always
        # separate modules in the project root that duplicate all sources in the tree.
        if isinstance(v, Module) and not v.is_content_root
        for cr in v.contents
    }

    for _, item in sorted(by_path.items()):
        if isinstance(item, Module):
            if not ctx.opts.iml_in_project_root:
                modulesroot = ctx.opts.arc_root
            elif ctx.opts.idea_files_root:
                modulesroot = op.normpath(op.join(PROJECT_DIR, op.relpath(project_root, ctx.opts.idea_files_root)))
            else:
                modulesroot = PROJECT_DIR
            iml_p = fix_windows(op.join(modulesroot, item.path, item.name) + '.iml')

            attrib = collections.OrderedDict()
            attrib['fileurl'] = 'file://' + iml_p
            attrib['filepath'] = iml_p

            if ctx.opts.group_modules:
                parts = [x for x in item.path.split(op.sep) if x]
                sep = '/' if ctx.opts.group_modules == 'tree' else '.'

                group_name = None
                if item.is_content_root:
                    # Content roots should always be in root.
                    pass
                elif has_parent_module(item, all_roots):
                    # Do not generate a group if the module is fully contained in other modules.
                    pass
                elif len(item.contents) > 1:
                    # Always generate a separate group for the module if it contains more than one content root.
                    #
                    # This is needed because if a module has multiple content roots it will have an additional
                    # node in the source tree that is named after the module.
                    #
                    # Example source tree for module direct/api5 without this group may look like this:
                    # - direct:         <-- project root
                    #   - direct-api5:  <-- additional tree node that contains both content roots
                    #     - api5        <-- first content root that corresponds to source path "direct/api5"
                    #     - generated   <-- second content root for generated sources
                    #
                    # Here the additional node is named "direct-api5" (this is the name of the IDEA module), which can
                    # be very long for deeply nested modules and does not correspond to module path in source tree.
                    #
                    # With a separate group the same tree looks like this:
                    # - direct:           <-- project root
                    #   - api5:           <-- (!) new separate group
                    #     - direct-api5   <-- additional tree node that contains both content roots
                    #       - api5        <-- first content root that corresponds to source path "direct/api5"
                    #       - generated   <-- second content root for generated sources
                    #
                    # This tree is more deeply nested, but now items in project root will have reasonable names
                    # ("api5" instead of "direct-api5") and will be sorted in the right order.
                    group_name = sep.join(parts)
                else:
                    # Generate a parent group for the module if there is no parent module.
                    parts = parts[:-1]
                    group_name = sep.join(parts)

                if group_name:
                    attrib['group'] = group_name

            modules[iml_p] = attrib

        else:
            assert isinstance(item, Lib)

            libs.add(item)
    return modules, libs


def get_annotation_processors(by_path):
    def group_by(m):
        return tuple(m.processors)

    def proc_name(procs):
        parts = []

        for x in procs:
            try:
                parts.append(x.split('.')[-1])

            except Exception:
                parts.append(str(x))

        return '-'.join(parts)

    for procs, modules in base.group_by(
        [x for x in six.itervalues(by_path) if isinstance(x, Module)], group_by
    ).items():
        if procs:
            yield proc_name(procs), procs, modules


def dump_annotation_processors(compile_configuration_root, by_path, outdir_in_content_root=False):
    ap = compile_configuration_root.find('annotationProcessing')

    if ap is None:
        ap = et.SubElement(compile_configuration_root, 'annotationProcessing')

    for p in ap.findall('profile'):
        ap.remove(p)

    attrib = collections.OrderedDict()
    attrib['name'] = 'Default'
    attrib['enabled'] = 'true'
    attrib['default'] = 'true'
    prof = et.SubElement(ap, 'profile', attrib=attrib)
    et.SubElement(prof, 'processorPath', attrib={'useClasspath': 'true'})

    for name, procs, ms in get_annotation_processors(by_path):
        attrib = collections.OrderedDict()
        attrib['default'] = 'false'
        attrib['name'] = name
        attrib['enabled'] = 'true'
        prof = et.SubElement(ap, 'profile', attrib=attrib)

        et.SubElement(prof, 'processorPath').attrib = {'useClasspath': 'true'}
        if outdir_in_content_root:
            et.SubElement(prof, 'outputRelativeToContentRoot').attrib = {'value': 'true'}
            et.SubElement(prof, 'sourceOutputDir').attrib = {'name': 'ya_generated'}
            et.SubElement(prof, 'sourceTestOutputDir').attrib = {'name': 'ya_generated_test'}

        for p in procs:
            et.SubElement(prof, 'processor').attrib = {'name': p}

        for m in ms:
            et.SubElement(prof, 'module').attrib = {'name': m.name}


def get_modules_javac_flags(by_path):
    return {item.name: item.javac_flags for item in by_path.values() if isinstance(item, Module)}


def dump_modules_javac_flags(javac_settings_root, by_path):
    oo = javac_settings_root.find('./option[@name="ADDITIONAL_OPTIONS_OVERRIDE"]')
    if oo is None:
        oo = et.SubElement(javac_settings_root, 'option', attrib={'name': 'ADDITIONAL_OPTIONS_OVERRIDE'})

    for m in oo.findall('module'):
        oo.remove(m)

    for module_name, flags in sorted(get_modules_javac_flags(by_path).items()):
        attrib = collections.OrderedDict()
        attrib['name'] = module_name
        attrib['options'] = ' '.join(flags)
        et.SubElement(oo, 'module', attrib=attrib)


def collect_common_jvm_args(by_path):
    jvm_args = None
    for m in by_path.values():
        if isinstance(m, Module):
            for cr in m.contents:
                for r in cr.roots:
                    if r.is_test and m.jvm_args:
                        break
                else:
                    continue
                if jvm_args is None:
                    jvm_args = set(m.jvm_args)
                else:
                    jvm_args = jvm_args & set(m.jvm_args)
    return {i.replace('$(BUILD_ROOT)', '$PROJECT_DIR$') for i in (jvm_args or set())}


def collect_kotlic_args(by_path, resource_id, plugin_root):
    plugins, kotlinc_args = set(), set()
    opts = set()
    for m in by_path.values():
        if isinstance(m, Module):
            i = 0
            while i < len(m.kotlinc_args or []):
                opt = m.kotlinc_args[i]
                i += 1
                if opt.startswith('-Xplugin='):
                    plugin_bin = opt[len('-Xplugin=') :]
                    plugins.add(plugin_bin)
                    if resource_id:
                        kotlinc_args.add(
                            '-Xplugin=$PROJECT_DIR$/{}/{}/plugins/{}'.format(
                                plugin_root, resource_id, os.path.basename(plugin_bin)
                            )
                        )
                    continue
                if opt.startswith('-') and i < len(m.kotlinc_args or []):
                    next_opt = m.kotlinc_args[i]
                    if not next_opt.startswith('-'):
                        i += 1
                        if next_opt not in opts:
                            opts.add(next_opt)
                        kotlinc_args.add((opt, next_opt))
                        continue
                kotlinc_args.add(opt)

    kotlinc_args = [item for sublist in kotlinc_args for item in (sublist if isinstance(sublist, tuple) else [sublist])]
    return plugins, kotlinc_args


def create_directory_based(by_path, project_root, ctx):
    project_root = ctx.opts.idea_files_root or project_root
    if not os.path.exists(os.path.join(project_root, '.idea')):
        os.makedirs(os.path.join(project_root, '.idea'))
    language_level, sdk_default_language_level, jdk_name, kotlin_target = detect_jdk(ctx)
    structure = empty_directory_based_project()
    dump_keys = set()

    for k, v in structure.items():
        if k == '.idea/workspace.xml':
            if os.path.exists(os.path.join(project_root, k)):
                continue
            configuratons = v.findall('./*/configuration')
            test_jvm_args = (
                collect_common_jvm_args(by_path) if ctx.opts.with_common_jvm_args_in_junit_template else set()
            )

            dll_path = get_proj_dll_path(ctx.opts, project_root)
            for key in ('-Djava.library.path=', '-Djna.library.path='):
                already = any(i.startswith(key) for i in test_jvm_args)
                if not already:
                    test_jvm_args.add(key + dll_path)
            test_jvm_args = ' '.join([saxutils.escape(i).replace('"', '&quot;') for i in sorted(test_jvm_args)])
            jvm_args = '-Djava.library.path={} -Djna.library.path={}'.format(dll_path, dll_path)
            for c in configuratons:
                val = test_jvm_args if c.attrib.get('type').lower().startswith('junit') else jvm_args
                et.SubElement(c, 'option', attrib={'name': 'VM_PARAMETERS', 'value': val})

        if k == '.idea/vcs.xml':
            if os.path.exists(os.path.join(project_root, k)):
                continue
            vcs_map = v.find('./component[@name="VcsDirectoryMappings"]/mapping')
            vcs_map.set('directory', ctx.opts.arc_root)
            vcs_map.set('vcs', get_vcs(ctx.opts.arc_root))

        if k not in (
            '.idea/modules.xml',
            '.idea/workspace.xml',
        ):
            try:
                with open(os.path.join(project_root, k)) as f:
                    data = et.parse(f, OrderedXMLTreeBuilder()).getroot()
                structure[k] = data
                continue
            except Exception:
                pass
        dump_keys.add(k)

    if '.idea/kotlinc.xml' in structure:
        kotlin_opts = structure['.idea/kotlinc.xml']
        kotlin_resource = base.resolve_kotlin_compiler(ctx.global_resources, ctx.opts)
        m = re.match('\\$\\(KOTLIN_COMPILER-sbr:(\\d+)\\)', kotlin_resource)
        if m:
            kotlin_resource = m.group(1)
        else:
            kotlin_resource = None
        option = structure['.idea/kotlinc.xml'].find(
            './component[@name="Kotlin2JvmCompilerArguments"]/option[@name="jvmTarget"]'
        )
        if option is not None:
            option.attrib['value'] = kotlin_target

        plugins_root = '.plugins_root'
        plugins, opts = collect_kotlic_args(by_path, kotlin_resource, plugins_root)
        if plugins:
            if not kotlin_resource:
                logger.warning("Can't detect kotlin compiler resource, be ready to problems with compiler plugins")
            elif not os.path.exists(os.path.join(project_root, plugins_root, kotlin_resource)):
                try:
                    import devtools.ya.yalibrary.yandex.sandbox as sandbox

                    out_path = os.path.join(project_root, '.plugins_root', kotlin_resource)
                    temp_path = out_path + '_'
                    logger.info('Download kotlin compiler resource')
                    sandbox.SandboxClient(token=ctx.opts.sandbox_oauth_token).fetch_from_sandbox(
                        resource_id=kotlin_resource,
                        task_id=None,
                        resource_type=None,
                        untar=True,
                        output=temp_path,
                        overwrite=True,
                    )
                    shutil.move(temp_path, out_path)
                except Exception as e:
                    logger.info("Couldn't download kotlin compiler: %s", e)

        if '-version' not in opts:
            opts = ['-version'] + opts
        kcs = kotlin_opts.find('./component[@name="KotlinCompilerSettings"]')
        if kcs is None:
            kcs = et.SubElement(kotlin_opts, 'component', attrib={'name': 'KotlinCompilerSettings'})
        aargs = kcs.find('./option[@name="additionalArguments"]')
        if aargs is None:
            aargs = et.SubElement(kcs, 'option', attrib={'name': 'additionalArguments'})
        aargs.attrib['value'] = ' '.join(opts)

    modules, libs = get_modules_and_libs(by_path, project_root, ctx)
    modules_item = structure['.idea/modules.xml'].find('./*/modules')
    for module in [modules[i] for i in sorted(modules, key=lambda x: modules[x].get('fileurl', '').split('/'))]:
        et.SubElement(modules_item, 'module', attrib=module)
    libraries_dir = os.path.join(project_root, '.idea', 'libraries')
    if not libs and os.path.isdir(libraries_dir):
        fs.ensure_removed(libraries_dir)
    elif libs:
        if not os.path.exists(libraries_dir):
            os.makedirs(libraries_dir)
        assert os.path.isdir(libraries_dir)
        for root, _, files in os.walk(libraries_dir):
            for deathman in [_f for _f in files if _f.endswith('.xml')]:
                os.remove(os.path.join(root, deathman))
            break
        for lib in libs:
            template = idea_template('jbuild/idea_templates/library.xml')
            template.find('./library').attrib['name'] = lib.long_name
            filename = lib.name + '.xml'
            if lib.classes_path:
                et.SubElement(
                    template.find('./*/CLASSES'),
                    'root',
                    attrib={'url': 'jar://{}'.format(fix_windows(lib.classes_path))},
                )
            if lib.sources_path:
                et.SubElement(
                    template.find('./*/SOURCES'),
                    'root',
                    attrib={'url': 'jar://{}'.format(fix_windows(lib.sources_path))},
                )
            print_pretty(template, os.path.join(libraries_dir, filename))

    prmanager = structure['.idea/misc.xml'].find('./component[@name="ProjectRootManager"]')
    if ctx.opts.idea_jdk_version and prmanager is not None:
        if prmanager.attrib.get('languageLevel') == '':
            prmanager.attrib['languageLevel'] = language_level
            dump_keys.add('.idea/misc.xml')
        if prmanager.attrib.get('default') == '':
            prmanager.attrib['default'] = sdk_default_language_level
            dump_keys.add('.idea/misc.xml')
        if prmanager.attrib.get('project-jdk-name') == '':
            prmanager.attrib['project-jdk-name'] = jdk_name
            dump_keys.add('.idea/misc.xml')

    cc = structure['.idea/compiler.xml'].find('./component[@name="CompilerConfiguration"]')
    if cc is not None:
        dump_annotation_processors(cc, by_path, 'SAVE_JAVAC_GENERATED_SRCS' in ctx.opts.flags)
        dump_keys.add('.idea/compiler.xml')

    js = structure['.idea/compiler.xml'].find('./component[@name="JavacSettings"]')
    if js is None:
        js = et.SubElement(structure['.idea/compiler.xml'], 'component', attrib={'name': 'JavacSettings'})
    dump_modules_javac_flags(js, by_path)
    dump_keys.add('.idea/compiler.xml')
    dump_keys.add('.idea/kotlinc.xml')

    for k in dump_keys:
        print_pretty(structure[k], os.path.join(project_root, k))


def modify_run_manager(etree, path, project_root, ctx):
    run_manager = etree.find('./component[@name="RunManager"]')

    if run_manager is None:
        # Normally never happens
        logger.warning('No RunManager section in %s file. Can\'t not modify VM_PARAMETERS.', path)
        return False

    # Add -Djava.library.path -Djna.library.path to all default run configuration
    for default_config in run_manager.findall('./configuration[@default="true"]'):
        for vm_optios in default_config.findall('./option[@name="VM_PARAMETERS"]'):
            val = vm_optios.get('value')
            new_val = val if val is not None else ''

            if 'java.library.path' not in new_val:
                new_val += ' -Djava.library.path={}'.format(get_proj_dll_path(ctx.opts, project_root))

            if 'jna.library.path' not in new_val:
                new_val += ' -Djna.library.path={}'.format(get_proj_dll_path(ctx.opts, project_root))
            logger.debug('Modifying VM_PARAMETERS for default configuration %s.', default_config.get('type'))

            if new_val != val:
                logger.debug('Change "%s" into "%s"', val, new_val)

            vm_optios.set('value', new_val)
    return True


def get_module_name(path, ctx):
    for custom in sum(ctx.by_path[path].plain.get(consts.IDEA_MODULE_NAME, []), []):
        return custom

    for m in consts.JAVA_LIBRARY, consts.JAVA_PROGRAM:
        for occ in ctx.by_path[path].plain.get(m, []):
            for val in occ:
                return val

    def split_parts(path):
        while path:
            splited = os.path.split(path)
            if not splited[1]:
                return
            yield splited[1]
            path = splited[0]

    return '-'.join(reversed(list(split_parts(path))))


def create_library(ctx, target, path, classes_path, sources_path):
    name = target.output_jar_name()[:-4]
    long_name = path if ctx.opts.with_long_library_names else name
    return Lib(name, long_name, classes_path, sources_path)


def cached_relativize(path, cache):
    result = cache.get(path)
    if result is not None:
        return result
    result = base.relativize(path)
    cache[path] = result
    return result


def process_path(path, ctx, results_root, project_root, relativize_cache, dry_run):
    target = ctx.by_path[path]

    if not target.is_idea_target():
        funcz = []

        def look_for_jar(p):
            if not p:
                return None

            src = p.replace(consts.BUILD_ROOT, results_root)
            dest = p.replace(consts.BUILD_ROOT, project_root)

            if os.path.exists(src):
                if os.path.exists(dest) or os.path.islink(dest):
                    funcz.append(funcs.rm(dest))
                funcz.extend([funcs.mkdirp(os.path.dirname(dest)), funcs.mv(src, dest)])

                return dest.replace(project_root, PROJECT_DIR, 1) + '!/'

            return None

        assert target.provides_jar()

        cls = look_for_jar(target.output_jar_path())

        if cls:
            return create_library(ctx, target, path, cls, look_for_jar(target.output_sources_jar_path())), funcz

        elif dry_run:
            return create_library(ctx, target, path, None, None), funcz

        else:
            return None, []

    funcz = []
    srcdirs = []
    prefixes = []
    gens = []

    have_java_srcs = False

    for i, words in enumerate(target.plain.get(consts.JAVA_SRCS, [])):
        have_java_srcs = True
        if forced_lib(path, ctx):
            continue

        generated = False
        in_source_root = False
        in_results_root = False

        is_resource, srcdir, pp, ex, _, _ = compile.parse_words(words)

        if ex:
            srcdir = graph_base.hacked_path_join(consts.BUILD_ROOT, path, str(i) + '-sources')

        if not srcdir:
            srcdir = op.join(consts.SOURCE_ROOT, path)

        if graph_base.in_source(srcdir):
            in_source_root = True

        elif graph_base.in_build(srcdir):
            in_results_root = True

        else:
            source = node.try_resolve_inp(ctx.arc_root, path, srcdir)
            build = node.try_resolve_inp(results_root, path, srcdir).replace(consts.SOURCE_ROOT, consts.BUILD_ROOT)

            if graph_base.in_source(source):
                srcdir = source
                in_source_root = True

            elif graph_base.in_build(build):
                srcdir = build
                in_results_root = True

        if in_source_root:
            srcdir = srcdir.replace(consts.SOURCE_ROOT, ctx.arc_root)

        elif in_results_root and op.isdir(srcdir.replace(consts.BUILD_ROOT, results_root)):
            generated = True

            proj = srcdir.replace(consts.BUILD_ROOT, project_root)

            funcz.append(funcs.rm(proj))
            funcz.append(funcs.cp(srcdir.replace(consts.BUILD_ROOT, results_root), proj))

            srcdir = srcdir.replace(consts.BUILD_ROOT, PROJECT_DIR, 1)

        else:
            logger.warning('Can\'t resolve srcdir %s in %s', srcdir, path)
            srcdir = None

        srcdirs.append((srcdir, is_resource))
        prefixes.append(pp)
        gens.append(generated)

    if target.plain.get(consts.ADD_DLLS_FROM_DEPENDS) and not forced_lib(path, ctx):
        proj = graph_base.hacked_path_join(project_root, path, 'dlls_to_pack')
        srcdir = proj.replace(project_root, PROJECT_DIR, 1)

        funcz.append(funcs.rm(proj))
        funcz.append(funcs.mkdirp(proj))

        dll_deps = set()
        for peer in target.plain[consts.NON_NAMAGEABLE_PEERS][0]:
            peer = ctx.by_path.get(strip_root(peer))
            if peer and peer.provides_dll():
                for dll in peer.output_dll_paths():
                    dll_deps.add(dll)
        dll_deps = sorted(dll_deps)

        for dll in dll_deps:
            src = dll.replace(consts.BUILD_ROOT, results_root)
            dst = os.path.join(proj, os.path.basename(dll))

            if os.path.exists(src):
                funcz.append(funcs.cp(src, dst))

        srcdirs.append((srcdir, True))
        prefixes.append(None)
        gens.append(True)
    javac_generated_srcs_tar = target.plain.get(consts.SAVE_JAVAC_GENERATED_SRCS_TAR)

    if javac_generated_srcs_tar and javac_generated_srcs_tar[0] and not forced_lib(path, ctx):
        javac_generated_srcs_tar = javac_generated_srcs_tar[0][0]
        srcdir = os.path.join(
            os.path.dirname(javac_generated_srcs_tar),
            os.path.splitext(os.path.basename(javac_generated_srcs_tar))[0]
            + ('_test' if is_test(target.plain) else ''),
        )
        tar_real_name = javac_generated_srcs_tar.replace(consts.BUILD_ROOT, results_root)

        if op.isfile(tar_real_name) and not archive.is_empty(tar_real_name):
            proj = os.path.join(
                srcdir.replace(consts.BUILD_ROOT, project_root),
                'ya_generated' + ('_test' if is_test(target.plain) else ''),
            )
            funcz.append(funcs.rm(proj))
            funcz.append(funcs.mkdirp(proj))
            funcz.append(funcs.untar_all(tar_real_name, proj))
            srcdir = srcdir.replace(consts.BUILD_ROOT, PROJECT_DIR, 1)
            srcdirs.append((srcdir, False))
            prefixes.append(None)
            gens.append(True)

    roots = [
        Root(
            s[0],
            p,
            mp2.is_jtest(target.plain) or mp2.is_jtest_for(target.plain) or mp2.is_junit5(target.plain),
            False,
            g,
            (s[1] or op.basename(s[0] or '') == 'resources') if s is not None else False,
        )
        for s, p, g in zip(srcdirs, prefixes, gens)
    ]

    is_module = bool(roots) or (is_test(target.plain) and not forced_lib(path, ctx))

    if is_module:
        in_roots, out_roots = [], []

        for r in roots:
            if r.path:
                if path2.path_startswith(r.path, op.join(ctx.arc_root, path)):
                    in_roots.append(r)

                else:
                    out_roots.append(r)

        conts = [Cotent(op.join(ctx.arc_root, path), in_roots)] + [Cotent(r.path, [r]) for r in out_roots]

        cp = [ctx.by_path[strip_root(p)].output_jar_path() for p in target.plain.get('MANAGED_PEERS_CLOSURE', [[]])[0]]
        dep_paths = [cached_relativize(op.dirname(x), relativize_cache) for x in cp if x != target.output_jar_path()]
        dep_paths = list(map(graph_base.hacked_normpath, list(graph_base.uniq_first_case(dep_paths))))
        dep_scopes = ['COMPILE' for __ in dep_paths]
        processors = list(compile.iter_processors(target.plain))
        javac_flags = []
        for k, v in compile.get_ya_make_flags(target.plain, consts.JAVAC_FLAGS).items():
            if k.startswith("Xep"):
                continue
            if k == 's' and v and v.startswith(consts.BUILD_ROOT):
                continue
            javac_flags.append('-' + k)
            if v is not None:
                javac_flags.append(v)
        jvm_args = sum(target.plain.get(consts.T_JVM_ARGS, []), [])
        kotlinc_args = sum(target.plain.get(consts.KOTLINC_OPTS, []), [])
        return (
            Module(
                get_module_name(path, ctx),
                path,
                conts,
                dep_paths,
                dep_scopes,
                processors,
                javac_flags,
                [(i, consts.TEST_DATA_SANDBOX) for i in sum(target.plain.get(consts.TEST_DATA_SANDBOX, []), [])]
                + [(i, consts.TEST_DATA_ARCADIA) for i in sum(target.plain.get(consts.TEST_DATA_ARCADIA, []), [])],
                jvm_args=jvm_args,
                kotlinc_args=kotlinc_args,
                jdk_version=target.plain.get('JDK_VERSION_INT', None),
                with_preview=target.plain.get('ENABLE_PREVIEW', False),
            ),
            funcz,
        )

    else:

        def look_for_jar(x):
            jar = target.output_jar_of_type_path(x)
            if jar is None:
                return None

            src = jar.replace(consts.BUILD_ROOT, results_root)
            dest = jar.replace(consts.BUILD_ROOT, project_root)

            if os.path.exists(src):
                removed = False
                if have_java_srcs and (os.path.exists(dest) or os.path.islink(dest)):
                    removed = True
                    funcz.append(funcs.rm(dest))
                if not (os.path.exists(dest) or os.path.islink(dest)) or removed:
                    funcz.extend([funcs.mkdirp(os.path.dirname(dest)), funcs.mv(src, dest)])

                if dest.startswith(project_root):
                    dest = PROJECT_DIR + dest[len(project_root) :]
                return dest + '!/'

        cls = look_for_jar(consts.CLS)

        if cls:
            return create_library(ctx, target, path, cls, look_for_jar(consts.SRC)), funcz

        elif dry_run:
            return create_library(ctx, target, path, None, None), funcz

        else:
            return None, []


def merge_jvm_args(left, *other):
    for right in other:
        for arg in right:
            for matcher in UNIQUE_JVM_ARGS_RE:
                if matcher.match(arg) and arg in left:
                    break
            else:
                left.append(arg)


def collapse_ut(by_path, is_jtest, is_jtest_for, jtest_for_wat, is_junit5):
    redirect = {}

    def is_java_module(p):
        return (
            p in by_path
            and not is_jtest(p)
            and not is_jtest_for(p)
            and not is_junit5(p)
            and isinstance(by_path[p], Module)
        )

    for p in six.iterkeys(by_path):
        if is_jtest(p) or is_junit5(p):
            d = op.dirname(p)

            while fix_windows(d) != fix_windows(os.path.dirname(d)):
                if is_java_module(d):
                    break
                d = os.path.dirname(d)

            if not is_java_module(d):
                continue

        elif is_jtest_for(p):
            try:
                d = jtest_for_wat(p)[0]

            except IndexError:
                continue

        else:
            continue

        if is_java_module(d) and isinstance(by_path[p], Module):
            redirect[p] = d

            module, ut = by_path[d], by_path[p]

            module.contents.extend(ut.contents)
            module.processors = list(collections.OrderedDict.fromkeys(module.processors + ut.processors))
            module.test_data |= ut.test_data or set()
            merge_jvm_args(module.jvm_args, ut.jvm_args)

            store = frozenset(module.dep_paths)

            for ut_dep in ut.dep_paths:
                if ut_dep not in store and ut_dep != d:
                    module.dep_paths.append(ut_dep)
                    module.dep_scopes.append('TEST')
            if not module.jdk_version or not ut.jdk_version:
                module.jdk_version = None
            else:
                module.jdk_version = min(module.jdk_version, ut.jdk_version)

            if module.with_preview or ut.with_preview:
                module.with_preview = True

    def fix(p):
        try:
            return redirect[p]

        except KeyError:
            return p

    for m in six.itervalues(by_path):
        if isinstance(m, Module):
            m.dep_paths = list(map(fix, m.dep_paths))

    for p in six.iterkeys(redirect):
        by_path.pop(p)


def collapse_content_roots(by_path):
    modules = [x for x in six.itervalues(by_path) if isinstance(x, Module)]
    warns = []

    def warn(s):
        return lambda: logger.warning(s)

    def collapse_roots(module, roots):
        assert all(root.path == roots[0].path for root in roots)
        assert all(root.generated == roots[0].generated for root in roots)
        # Explicit mark collapsed root as source if not all roots has some type
        if not all(root.is_resource == roots[0].is_resource for root in roots):
            collapsed_root_is_resource = False
        else:
            collapsed_root_is_resource = roots[0].is_resource

        if not all(root.prefix == roots[0].prefix for root in roots):
            warns.append(
                warn(
                    'Module {} have multiple source roots with same path {} and different package prefixes: {}.'
                    'Result is undefined.'.format(
                        module.name, roots[0].path, ' '.join(sorted(set([str(root.prefix) for root in roots])))
                    )
                )
            )
        return Root(
            roots[0].path,
            roots[0].prefix,
            all(root.is_test for root in roots),
            all(root.ignored for root in roots),
            roots[0].generated,
            collapsed_root_is_resource,
        )

    for m in modules:
        cr_by_path = collections.defaultdict(list)
        for cr in m.contents:
            cr_by_path[cr.path].append(cr)

        for p, crs in cr_by_path.items():
            cr_by_path[p] = Cotent(p, sum([cr.roots for cr in crs], []))

        sorted_paths = sorted(cr_by_path.keys(), key=lambda x: x.replace('\\', '/'))  # because windows

        attach = {}

        for i in six.moves.xrange(len(sorted_paths)):
            for j in six.moves.xrange(i):
                child = sorted_paths[i]
                par = sorted_paths[j]

                if path2.path_startswith(child, par) and child not in attach:
                    attach[child] = par

        for p in sorted(attach.keys()):
            cr_by_path[attach[p]].roots.extend(cr_by_path[p].roots)
            cr_by_path.pop(p)

        crs = []
        for p in sorted(cr_by_path.keys()):
            unique_roots = {}
            for r in [
                collapse_roots(m, roots)
                for roots in base.group_by(cr_by_path[p].roots, lambda root: root.path).values()
            ]:
                real_p = os.path.normpath(r.path)
                if real_p not in unique_roots:
                    unique_roots[real_p] = r
                else:
                    warns.append(
                        warn(
                            'Module {} have several content roots with equal paths {}. '
                            'Result is undefinded.'.format(m.name, r.path)
                        )
                    )
            cr_by_path[p].roots = unique_roots.values()
            crs.append(cr_by_path[p])

        m.contents = crs

    mods_by_roots = []

    for m in modules:
        for cr in m.contents:
            for r in cr.roots:
                mods_by_roots.append((r.path, m.path))
    mods_by_roots.sort(key=lambda x: x[0])

    def pairwise(iterable):
        a, b = itertools.tee(iterable)
        next(b, None)
        return zip(a, b)

    for (prev_root, prev_mod), (next_root, next_mod) in pairwise(mods_by_roots):
        if prev_root != next_root and path2.path_startswith(next_root, prev_root):
            warns.append(
                warn(
                    'Source root {} for module {} shadows source root {} for module {}'.format(
                        prev_root,
                        prev_mod,
                        next_root,
                        next_mod,
                    )
                )
            )

    return warns


def iter_names(path):
    d, b = os.path.split(path)
    name = b

    while b:
        yield name

        d, b = os.path.split(d)
        name = b + '-' + name


def warn_uniq_names(by_path):
    module_by_name = base.group_by([x for x in by_path.values() if isinstance(x, Module)], lambda m: m.name)
    warns = []

    def warn(s):
        return lambda: logger.warning(s)

    for name, modules in six.iteritems(module_by_name):
        if len(modules) > 1:
            warns.append(warn('Modules {} have same module name {}'.format(', '.join([m.path for m in modules]), name)))

    if warns:
        warns.append(warn('To change module name use JAVA_LIBRARY(<name>), JAVA_PROGRAM(<name>)'))

    return warns


def collect_dlls(ctx, result_nodes, results_root, project_root):
    all_dll_results = set()

    for n in result_nodes:
        assert n.path in ctx.by_path
        target = ctx.by_path[n.path]

        if target.provides_dll():
            for dll in target.output_dll_paths():
                all_dll_results.add(dll)

    dll_dest_dir = os.path.join(project_root, 'dlls')
    funcz = [funcs.mkdirp(dll_dest_dir)]

    if not all_dll_results:
        return funcz

    for dll_result in sorted(all_dll_results):
        src = dll_result.replace(consts.BUILD_ROOT, results_root)
        dst = os.path.join(dll_dest_dir, os.path.basename(dll_result))

        if os.path.exists(src):
            funcz.append(funcs.cp(src, dst))

    return funcz


def up_funcs(ctx, results_root, project_root, dry_run):
    res, paths = idea_results(ctx, ctx.nodes)

    by_path = {}  # by_path value is None | Module | Library
    copy = []
    relativize_cache = {}

    for p in paths:
        target = ctx.by_path[p]
        assert target.provides_jar()

        by_path[p], f = process_path(p, ctx, results_root, project_root, relativize_cache, dry_run)
        copy.extend(f)

    if not ctx.opts.separate_tests_modules:
        collapse_ut(
            by_path,
            is_jtest=lambda path: (
                mp2.is_jtest(ctx.by_path[path].plain) if ctx.by_path[path].is_idea_target() else False
            ),
            is_jtest_for=lambda path: (
                mp2.is_jtest_for(ctx.by_path[path].plain) if ctx.by_path[path].is_idea_target() else False
            ),
            jtest_for_wat=lambda path: (
                sum(ctx.by_path[path].plain.get(consts.JAVA_TEST_FOR, []), [])
                if ctx.by_path[path].is_idea_target()
                else False
            ),
            is_junit5=lambda path: (
                mp2.is_junit5(ctx.by_path[path].plain) if ctx.by_path[path].is_idea_target() else False
            ),
        )

    warns = warn_uniq_names(by_path)
    warns += collapse_content_roots(by_path)

    def iml(p):
        def f():
            create_iml(p, by_path, project_root, ctx)

        return f

    def d_based():
        def f():
            create_directory_based(by_path, project_root, ctx)

        return f

    def test_data(p):
        def f():
            if not by_path[p].test_data:
                return

            test_data_dir = os.path.join(project_root, p, 'test_data')
            fs.ensure_removed(test_data_dir)
            fs.create_dirs(os.path.join(project_root, p, 'test_data'))
            for td in by_path[p].test_data:
                if td[1] == consts.TEST_DATA_SANDBOX:
                    parts = td[0].split('=', 1)
                    res_id = parts[0]
                    res_path = parts[1] if len(parts) > 1 else ''
                    if not os.path.exists(
                        os.path.join(results_root, res_id, 'resource_info.json')
                    ) or not os.path.exists(os.path.join(results_root, res_id, 'resource')):
                        logger.warning('Something wrong with {} DATA({})'.format(p, td[0]))
                        continue
                    with open(os.path.join(results_root, res_id, 'resource_info.json')) as js:
                        info = json.loads(js.read())
                        origin_filename = info['file_name']
                        if not os.path.exists(os.path.join(test_data_dir, res_path)):
                            fs.create_dirs(os.path.join(test_data_dir, res_path))
                        for ext in ('.tar', '.tar.gz', '.tgz', '.tar.bz2', '.tbz'):
                            if origin_filename.endswith(ext):
                                archive_filename = os.path.join(results_root, res_id, 'resource')
                                try:
                                    archive.extract_from_tar(
                                        tar_file_path=archive_filename, output_dir=os.path.join(test_data_dir, res_path)
                                    )
                                except Exception:
                                    logging.exception(
                                        "Cant extract {} ({}) be ready to problems".format(
                                            archive_filename, os.path.dirname(td[0])
                                        )
                                    )
                                break
                        else:
                            dest = os.path.join(test_data_dir, res_path, origin_filename)
                            if os.path.exists(dest):
                                fs.ensure_removed(dest)
                            shutil.move(os.path.join(results_root, res_id, 'resource'), dest)
                elif td[1] == consts.TEST_DATA_ARCADIA:
                    src = os.path.join(ctx.opts.arc_root, td[0])
                    dst = os.path.join(test_data_dir, os.path.basename(td[0]))
                    try:
                        os.symlink(src, dst)
                    except Exception:
                        logging.warning("Cant create symlink for {} DATA ({} -> {})".format(p, src, dst))

        return f

    def report():
        logger.info('Successfully generated idea project: %s', project_root)
        # logger.info('Recommended JDK path: %s', op.dirname(op.dirname(tools.tool('java'))))
        logger.info(
            'Devtools IntelliJ plugin (Latest stable IDEA is required): https://a.yandex-team.ru/arc/trunk/arcadia/devtools/intellij/README.md'
        )
        logger.info(
            'Codestyle config: %s. You can import this file with "File -> Manage IDE Settings -> Import settings..." command. '
            'After this choose "yandex-arcadia" in code style settings (Preferences -> Editor -> Code Style).',
            op.join(tools.tool('idea_style_config'), 'intellij-codestyle.jar'),
        )

    def create_plugin():
        return create_plugin_config(project_root, ctx)

    parse = []

    external_content_roots = set()

    if ctx.opts.with_content_root_modules:
        for p in ctx.opts.rel_targets:
            external_content_roots.add(p)
    if ctx.opts.external_content_root_modules:
        info = devtools.ya.build.targets.resolve(
            ctx.arc_root, [os.path.join(os.getcwd(), x) for x in ctx.opts.external_content_root_modules]
        )
        for p in info.targets:
            external_content_roots.add(os.path.relpath(p, info.root))

    for p in external_content_roots:
        if p not in by_path or isinstance(by_path[p], Lib):
            m = Module(
                '-'.join(op.normpath(p).split(op.sep)) + '_content_root',
                p,
                [Cotent(op.join(ctx.arc_root, p), [])],
                [],
                [],
                [],
                [],
                is_content_root=True,
            )
            by_path[p + '_content_root'] = m

    for p, item in six.iteritems(by_path):
        if isinstance(item, Module):
            parse.append(iml(p))
            if not ctx.opts.omit_test_data:
                parse.append(test_data(p))

    parse.append(d_based())

    def add_native_settings():
        def f():
            template = idea_template('jbuild/idea_templates/dlls.xml')
            print_pretty(template, os.path.join(project_root, '.idea/dlls.xml'))

        return f

    dlls = collect_dlls(ctx, res, results_root, project_root)
    dlls.append(add_native_settings())

    return parse + copy + dlls + warns + [create_plugin, report]
