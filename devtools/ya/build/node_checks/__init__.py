def is_module(n):
    return 'target_properties' in n and 'module_type' in n['target_properties']


def is_binary(n):
    return is_module(n) and n['target_properties']['module_type'] == 'bin'


def is_package(elem_props):
    # TODO: mark PACKAGE and UNION explicitly
    return elem_props.get('kv', {}).get('p', '') in ('PK', 'UN')


def is_host_platform(node):
    return bool(node.get('host_platform'))


def is_tools_tc(target_tc):
    return target_tc.get('flags') and target_tc.get('flags').get('TOOL_BUILD_MODE') == 'yes'


def is_maven_deploy(cmd):
    maven_bin = False
    deploy_file = False
    for arg in cmd['cmd_args']:
        if arg.endswith('bin/mvn'):
            maven_bin = True
        elif arg == 'deploy:deploy-file':
            deploy_file = True
    return maven_bin and deploy_file


def is_empty_graph(g):
    if not g:
        return True

    if not g['graph']:
        return True

    return False
