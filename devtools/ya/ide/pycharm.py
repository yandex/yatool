import logging
import xml.etree.ElementTree as et
import os
import platform
import re
import shutil
import sys

import devtools.ya.build.build_opts as bo
import devtools.ya.build.graph as bg
import devtools.ya.build.ya_make as ya_make
import devtools.ya.core.event_handling
import devtools.ya.core.yarg
import devtools.ya.core.resource as resource
import exts.fs


JDK_TABLE_XML = 'jdk.table.xml'
JDK_TABLE_XML_BKP = JDK_TABLE_XML + '.bkp'
JDK_TABLE_XML_ORIG = JDK_TABLE_XML + '.orig'
LINUX_JB_CONFIG_PATH = '~/.config/JetBrains'
DARWIN_JB_CONFIG_PATH = '~/Library/Application Support/JetBrains'
IDE_PREFIXES = ('PyCharm', 'PyCharmCE', 'IdeaIC', 'IntelliJIdea', 'CLion')
PYCHARM_VERSION_REGEX = re.compile(r"PyCharm(CE)?([\d.]+)")

MODULE_HELPER_SCRIPT = '''#!/bin/sh
export Y_PYTHON_SOURCE_ROOT={}
export Y_PYTHON_ENTRY_POINT=":main"
{} "$@"
'''
logger = logging.getLogger(__name__)


def do_pycharm(params):
    os_name = platform.system()
    if os_name == 'Windows':
        logger.error("PyCharm helper doesn't work on Windows")
        return
    arcadia_root = params.arc_root
    if params.list_ide:
        logger.info("Available IDE: {}".format(" ".join(find_available_ide())))
        return

    targets = generate_wrappers(params, arcadia_root)
    if len(targets) == 0:
        logger.error("Python executable targets not found. Stop generating project.")
        return

    if params.only_generate_wrapper:
        return

    params.jdk_table_path = find_jdk_table(params)

    update_pycharm_sdks(arcadia_root, targets, params)
    project_dir = os.getcwd()
    generate_project(arcadia_root, targets, project_dir)


def generate_wrappers(params, arcadia_root):
    params.ya_make_extra.extend(['-DBUILD_LANGUAGES=PY3', '-r'])
    ya_make_opts = devtools.ya.core.yarg.merge_opts(bo.ya_make_options(free_build_targets=True))
    params = devtools.ya.core.yarg.merge_params(ya_make_opts.initialize(params.ya_make_extra), params)
    if params.do_codegen:
        to_add = {'.py', '.pyi', '.fbs.pysrc'}
        params.add_result.extend(to_add - set(params.add_result))

    params.create_symlinks = True
    params.force_build_depends = True
    import app_ctx  # XXX

    subscribers = [
        ya_make.DisplayMessageSubscriber(params, app_ctx.display),
        devtools.ya.core.event_handling.EventToLogSubscriber(),
    ]
    if getattr(app_ctx, 'evlog', None):
        subscribers.append(ya_make.YmakeEvlogSubscriber(app_ctx.evlog.get_writer('ymake')))

    with app_ctx.event_queue.subscription_scope(*subscribers):
        task, _, _, _, _ = bg.build_graph_and_tests(params, check=True, display=app_ctx.display)

    results = task['result']
    targets = []
    for graph_node in task['graph']:
        if (
            graph_node['uid'] in results
            and graph_node.get('target_properties', {}).get('module_type') == 'bin'
            and graph_node.get('target_properties', {}).get('module_lang') == 'py3'
        ):
            target = {
                'binary': graph_node['outputs'][0],
                'module_dir': graph_node.get('target_properties', {}).get('module_dir'),
            }
            targets.append(target)

    # Skip building if Python executables not found
    if len(targets) == 0:
        return targets

    builder = ya_make.YaMake(params, app_ctx, graph=task, tests=[])
    builder.go()
    rc = builder.exit_code
    if rc != 0:
        sys.exit(rc)

    for target in targets:
        generate_module_helper(arcadia_root, target, params)

    return targets


def generate_module_helper(arcadia_root, target, params):
    module_dir = target['module_dir']
    python_bin = os.path.join(arcadia_root, target['binary'].replace('$(BUILD_ROOT)/', ''))
    helper_path = os.path.join(arcadia_root, module_dir, params.wrapper_name)

    with open(helper_path, mode='w') as helper:
        helper.write(MODULE_HELPER_SCRIPT.format(arcadia_root, python_bin))
        os.chmod(helper_path, 0o755)


def generate_imls(arcadia_root, targets):
    for target in targets:
        binary_path = target['binary']
        target_dir = os.path.join(arcadia_root, os.path.dirname(binary_path).replace('$(BUILD_ROOT)' + os.path.sep, ''))
        iml_name = os.path.basename(binary_path)
        iml_path = os.path.join(target_dir, iml_name + '.iml')
        target['iml_path'] = iml_path
        target['module_name'] = iml_name
        write_iml(iml_path, jdk_name=target['jdk_name'])


def write_iml(iml_path, jdk_name):
    root = pycharm_template('ide/templates/pycharm/iml.xml')
    jdk_entry = root.find(path='component[@name="NewModuleRootManager"]/orderEntry[@type="jdk"]')
    jdk_entry.attrib['jdkName'] = jdk_name
    root.tail = '\n'
    reorder_attributes(root)
    et.ElementTree(root).write(iml_path, encoding='UTF-8', xml_declaration=True)


def generate_project(arcadia_root, targets, project_dir):
    generate_imls(arcadia_root, targets)
    settings_dir = os.path.join(project_dir, '.idea')
    exts.fs.ensure_dir(settings_dir)
    iml_name = os.path.basename(project_dir)
    iml_path = os.path.join(settings_dir, iml_name + '.iml')
    jdk_name = targets[0]['jdk_name']  # use random python binary, later use system python
    write_iml(iml_path, jdk_name=jdk_name)
    modules = pycharm_template('jbuild/idea_templates/modules.xml')
    modules_list = modules.find('component[@name="ProjectModuleManager"]/modules')
    add_module(modules_list, iml_path, project_dir)  # Root module
    for target in targets:
        add_module(modules_list, target['iml_path'], project_dir)
    indent(modules_list, level=2)

    root = et.ElementTree(modules)
    reorder_attributes(root)

    modules_path = os.path.join(settings_dir, 'modules.xml')
    root.write(modules_path, encoding='UTF-8', xml_declaration=True)


def add_module(module_list, iml_path, project_dir):
    module_iml_path = '$PROJECT_DIR$/' + os.path.relpath(iml_path, project_dir)
    module_list.append(et.Element('module', {'fileurl': 'file://' + module_iml_path, 'filepath': module_iml_path}))


def update_pycharm_sdks(arcadia_root, targets, params):
    jdk_table_path = params.jdk_table_path
    backup_jdk_table_settings(jdk_table_path)
    update_sdk(arcadia_root, targets, jdk_table_path, params.wrapper_name)


def find_available_ide():
    base_ide_config_dir = find_base_config_dir()
    if not os.path.exists(base_ide_config_dir):
        return []
    dirs = sorted(os.listdir(base_ide_config_dir))
    candidate = []
    for dir_name in dirs:
        if any(dir_name.startswith(prefix) for prefix in IDE_PREFIXES) and not dir_name.endswith('backup'):
            candidate.append(dir_name)
        elif dir_name.startswith('RemoteDev-'):
            workspace_dirs = sorted(os.listdir(os.path.join(base_ide_config_dir, dir_name)))
            for workspace_dir in workspace_dirs:
                candidate.append(os.path.join(dir_name, workspace_dir))

    return candidate


def find_base_config_dir():
    os_name = platform.system()
    if os_name.startswith('Darwin'):
        return os.path.expanduser(DARWIN_JB_CONFIG_PATH)
    elif os_name.startswith('Linux'):
        return os.path.expanduser(LINUX_JB_CONFIG_PATH)
    return ""


def find_latest_pycharm(dirs):
    candidates = []
    for dir_name in dirs:
        result = PYCHARM_VERSION_REGEX.match(dir_name)
        if result:
            candidates.append((tuple(result.group(2).split(".") + [result.group(1) is None]), dir_name))
    if len(candidates) == 0:
        raise Exception("Can't find PyCharm settings dir")
    return max(candidates, key=lambda x: x[0])[1]


def find_jdk_table(params):
    base_ide_config_dir = find_base_config_dir()
    dirs = find_available_ide()
    if not params.ide_version:
        params.ide_version = find_latest_pycharm(dirs)

    if not os.path.exists(os.path.join(base_ide_config_dir, params.ide_version)):
        logger.error(f"Can't find IDE settings dir for {params.ide_version}")
        logger.info("Available IDE: {}".format(" ".join(dirs)))
        raise Exception(f"Can't find IDE settings dir for {params.ide_version}")

    logger.info('%s was selected for updating SDK table', params.ide_version)
    jdk_table_file = os.path.join(base_ide_config_dir, params.ide_version, 'options', JDK_TABLE_XML)
    if not os.path.exists(jdk_table_file):
        raise Exception(f"Can't find jdk.table.xml for {params.ide_version}")
    return jdk_table_file


def backup_jdk_table_settings(jdk_table_path):
    options_dir = os.path.dirname(jdk_table_path)
    file_list = [file_name for file_name in (os.listdir(options_dir)) if file_name.startswith(JDK_TABLE_XML)]
    if JDK_TABLE_XML_ORIG not in file_list:
        shutil.copyfile(jdk_table_path, os.path.join(options_dir, JDK_TABLE_XML_ORIG))
    else:
        shutil.copyfile(jdk_table_path, os.path.join(options_dir, JDK_TABLE_XML_BKP))


def format_home_path(arcadia_root, binary_path, user_home, wrapper_name):
    return os.path.join(
        '$USER_HOME$',
        os.path.relpath(arcadia_root, user_home),
        os.path.dirname(binary_path).replace('$(BUILD_ROOT)' + os.path.sep, ''),
        wrapper_name,
    )


def compute_wrapper_classpath(home_path, target):
    binary_path = target['binary']
    return os.path.join('file://', os.path.dirname(home_path), os.path.basename(binary_path))


def update_sdk(arcadia_root, targets, jdk_table_path, wrapper_name):
    xml_tree = et.parse(jdk_table_path)

    jdk_table = xml_tree.find(path='component[@name="ProjectJdkTable"]')

    jdk_dict = {}

    for jdk in jdk_table:
        jdk_name = jdk.find('name').attrib['value']
        jdk_dict[jdk_name] = jdk

    for target in targets:
        jdk = et.Element('jdk', {'version': '2'})
        binary_path = target['binary']
        jdk_name = binary_path.replace('$(BUILD_ROOT)' + os.path.sep, '').replace(os.path.sep, '_')
        jdk.append(et.Element('name', {'value': jdk_name}))
        jdk.append(et.Element('type', {'value': 'Python SDK'}))
        user_home = os.path.expanduser('~')
        home_path = format_home_path(arcadia_root, binary_path, user_home, wrapper_name)
        jdk.append(et.Element('homePath', {'value': home_path}))
        roots_elem = et.Element('roots')
        classpath_elem = et.Element('classPath')
        composite_root = et.Element('root', {'type': 'composite'})
        wrapper_classpath = et.Element('root', {'type': 'simple', 'url': compute_wrapper_classpath(home_path, target)})
        composite_root.append(wrapper_classpath)
        classpath_elem.append(composite_root)
        sourcepath_elem = et.Element('sourcePath')
        sourcepath_elem.append(et.Element('root', {'type': 'composite'}))
        roots_elem.append(classpath_elem)
        roots_elem.append(sourcepath_elem)
        jdk.append(roots_elem)
        jdk.append(et.Element('additional'))

        if jdk_name in jdk_dict:
            jdk_table.remove(jdk_dict[jdk_name])
        jdk_table.append(jdk)
        target['jdk_name'] = jdk_name

    indent(xml_tree.getroot())
    reorder_attributes(xml_tree)
    xml_tree.write(jdk_table_path)


def reorder_attributes(root):
    """Reorder XML element attributes to stable order for test purposes"""
    for el in root.iter():
        attrib = el.attrib
        if len(attrib) > 1:
            attribs = sorted(attrib.items())
            attrib.clear()
            attrib.update(attribs)


def indent(elem, level=0, more_sibs=False):
    """Indent xml elements properly for readable diffs in unit tests and ocular debugging"""
    i = "\n"
    if level:
        i += (level - 1) * '  '
    num_kids = len(elem)
    if num_kids:
        if not elem.text or not elem.text.strip():
            elem.text = i + "  "
            if level:
                elem.text += '  '
        count = 0
        for kid in elem:
            indent(kid, level + 1, count < num_kids - 1)
            count += 1
        if not elem.tail or not elem.tail.strip():
            elem.tail = i
            if more_sibs:
                elem.tail += '  '
    else:
        if level and (not elem.tail or not elem.tail.strip()):
            elem.tail = i
            if more_sibs:
                elem.tail += '  '


def pycharm_template(name):
    s = resource.try_get_resource(name)

    if s is None:
        raise Exception(f"Can't find resource {name}")

    return et.fromstring(s)
