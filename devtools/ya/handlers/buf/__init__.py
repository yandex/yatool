import logging
import os
from copy import deepcopy

import yaml
from library.python import resource


import exts.yjson as json
from exts.process import run_process, execve
import yalibrary.tools
from devtools.ya.build.build_facade import gen_json_graph
from devtools.ya.build.build_opts import ShowHelpOptions, BuildTargetsOptions, SandboxAuthOptions
from devtools.ya.core.yarg import CompositeHandler, OptsHandler
from devtools.ya.core.yarg import Options, ArgConsumer, SetValueHook, ArgsValidatingException, SetConstValueHook


import devtools.ya.app


TARGET_CONFIG_NAME = 'buf.yaml'
RESOURCE_CONFIG_NAME = 'buf.yaml'

logger = logging.getLogger(__name__)


class BufYaHandler(CompositeHandler):
    def __init__(self):
        CompositeHandler.__init__(
            self,
            description='Protobuf linter and breaking change detector',
        )
        common_opts = [
            ShowHelpOptions(),
            BuildTargetsOptions(with_free=True),
            CommonOptions(),
            SandboxAuthOptions(),
        ]

        self['lint'] = OptsHandler(
            action=devtools.ya.app.execute(action=do_lint_protos), description='Lint .proto files', opts=common_opts
        )

        self['build'] = OptsHandler(
            action=devtools.ya.app.execute(action=do_build_image),
            description='Build all .proto files from the target and output an Image',
            opts=common_opts + [OutputImageOptions()],
        )

        self['check'] = OptsHandler(
            action=devtools.ya.app.execute(action=do_breaking),
            description="Check that the target has no breaking changes compared to the input image",
            opts=common_opts + [InputImageOptions()],
        )


class CommonOptions(Options):
    def __init__(self):
        self.buf_bin_path = None
        self.custom_conf_dir = None
        self.use_build_graph = True

    @staticmethod
    def consumer():
        return [
            ArgConsumer(['--buf-bin'], help='The location of custom buf binary', hook=SetValueHook('buf_bin_path')),
            ArgConsumer(
                ['--custom-conf-dir'], help='Custom directory for conf files', hook=SetValueHook('custom_conf_dir')
            ),
            ArgConsumer(
                ['--do-not-use-build-graph'],
                help='Use simple find insted of build graph',
                hook=SetConstValueHook('use_build_graph', False),
            ),
        ]

    def postprocess(self):
        if self.custom_conf_dir:
            self.custom_conf_dir = os.path.abspath(self.custom_conf_dir)

        if self.buf_bin_path is None:
            pass
        elif not os.path.exists(self.buf_bin_path):
            raise ArgsValidatingException("buf binary path %s does not exists" % self.buf_bin_path)
        else:
            self.buf_bin_path = os.path.abspath(self.buf_bin_path)


class OutputImageOptions(Options):
    def __init__(self):
        self.output_image_path = None

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['-o', '--output'],
                help='Required. The location to write the image. Must be one of format [bin, json]',
                hook=SetValueHook('output_image_path'),
            )
        ]

    def postprocess(self):
        if self.output_image_path is None:
            raise ArgsValidatingException('--output option is required')

        self.output_image_path = os.path.abspath(self.output_image_path)


class InputImageOptions(Options):
    def __init__(self):
        self.input_image_path = None

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['-a', '--against-input'],
                help='Required. The source or image to check against. Must be one of format [bin, json]',
                hook=SetValueHook('input_image_path'),
            )
        ]

    def postprocess(self):
        if self.input_image_path is None:
            raise ArgsValidatingException('--against-input option is required')

        if os.path.splitext(self.input_image_path)[1] not in ('.bin', '.json'):
            raise ArgsValidatingException('Input image format must be .bin or .json, found %s' % self.input_image_path)

        self.input_image_path = os.path.abspath(self.input_image_path)


def gen_graph(params):
    res = gen_json_graph(
        build_root=None,
        build_type='release',
        build_targets=params.abs_targets,
        debug_options=[],
        flags={'TRAVERSE_DEPENDS': 'yes', 'TRAVERSE_RECURSE_FOR_TESTS': 'yes'},
        custom_conf_dir=params.custom_conf_dir,
    )
    try:
        return json.loads(res.stdout)
    except ValueError:
        logger.error('Failed to build graph')
        raise


def find_protos_simple(params, ignore_prefixes):
    res = set()
    suffix = '.proto'
    for target in params.abs_targets:
        for dir_, _, files in os.walk(target):
            for filename in files:
                # add alice/megamind/protos/a.proto
                file_path = os.path.relpath(os.path.join(dir_, filename), params.arc_root)

                if not file_path.endswith(suffix):
                    continue
                elif any(file_path.startswith(prefix) for prefix in ignore_prefixes):
                    continue
                else:
                    res.add(file_path)

    return res


def find_protos(data, params, conf):
    ignore_prefixes = {'contrib/libs/protobuf'}
    ignore_prefixes.update(conf['build'].get('excludes', []))

    if data is None:
        return find_protos_simple(params, ignore_prefixes)

    suffix = '.proto'
    proto_files = set()

    def walk(nodes_list):
        for node in nodes_list:
            if any(prefix in node['name'] for prefix in ignore_prefixes):
                continue

            if node['name'].endswith(suffix) and node['name'].startswith('$S/'):
                # "name": "$S/alice/megamind/protos/a.proto"
                name = node['name'].split('/', 1)[1]
                proto_files.add(name)

            if 'deps' in node:
                walk(node['deps'])

    walk(data['graph'])
    return proto_files


def get_ignored_protos(protos, targets):
    res = []
    for file_ in protos:
        for trgt in targets:
            if file_.startswith(trgt):
                break
        else:
            res.append(file_)

    return sorted(res)


def get_default_config():
    data = resource.find(RESOURCE_CONFIG_NAME)
    return yaml.safe_load(data)


def split_path(path):
    while path not in ('/', ''):
        yield path
        path = os.path.split(path)[0]

    yield path


def get_target_conf(root, target_path):
    for path in split_path(target_path):
        filename = os.path.join(root, path, TARGET_CONFIG_NAME)
        if os.path.exists(filename):
            return yaml.safe_load(open(filename))

    return None


def update_conf(default_conf, target_conf, ignored_protos):
    conf = deepcopy(default_conf)

    if target_conf is not None:
        if 'build' in target_conf:
            conf['build'] = target_conf['build']
            # roots can't be changed
            conf['build']['roots'] = ['.']

        if 'lint' in target_conf:
            conf['lint'] = target_conf['lint']

        if 'breaking' in target_conf:
            conf['breaking'] = target_conf['breaking']

    if 'ignore' in conf['lint'] and isinstance(conf['lint']['ignore'], list):
        conf['lint']['ignore'].extend(ignored_protos)
    else:
        conf['lint']['ignore'] = list(ignored_protos)

    return conf


def gen_file_args(files):
    for file_ in files:
        yield '--file'
        yield file_


def prepare_conf_and_files(params, use_build_graph=None):
    default_conf = get_default_config()
    # use only first target to search target config
    target_conf = get_target_conf(params.arc_root, params.rel_targets[0])
    conf = update_conf(default_conf, target_conf, [])

    if use_build_graph is None:
        use_build_graph = params.use_build_graph

    if use_build_graph:
        graph = gen_graph(params)
    else:
        graph = None

    protos = find_protos(graph, params, conf)
    ignored_protos = get_ignored_protos(protos, params.rel_targets)

    conf = update_conf(conf, None, ignored_protos)

    return conf, protos


def _get_buf_binary(params):
    if params.buf_bin_path is None:
        buf_bin_path = yalibrary.tools.tool('buf')
    else:
        buf_bin_path = params.buf_bin_path
    return os.path.abspath(buf_bin_path)


def call_buf(params, args, subprocess=False):
    buf_abs = _get_buf_binary(params)
    logger.debug('Buf calling args: %s', args)

    if subprocess:
        return run_process(buf_abs, args, cwd=params.arc_root, check=True)
    else:
        execve(buf_abs, args, cwd=params.arc_root)


# handlers


def do_lint_protos(params, use_build_graph=None, subprocess=False):
    conf, protos = prepare_conf_and_files(params, use_build_graph)
    files_args = list(gen_file_args(protos))

    args = ['check', 'lint', '--input-config', json.dumps(conf)] + files_args
    call_buf(params, args, subprocess)


def do_build_image(params, use_build_graph=None, subprocess=False):
    conf, protos = prepare_conf_and_files(params, use_build_graph)
    files_args = list(gen_file_args(protos))

    args = ['image', 'build', '--source-config', json.dumps(conf), '-o', params.output_image_path] + files_args

    call_buf(params, args, subprocess)
    return params.output_image_path


def do_breaking(params, use_build_graph=None, subprocess=False, source_image=None):
    conf, protos = prepare_conf_and_files(params, use_build_graph)

    if source_image is not None:
        add_args = ['--exclude-imports', '--input', source_image]
    else:
        add_args = list(gen_file_args(protos))

    args = [
        'check',
        'breaking',
        '--against-input',
        params.input_image_path,
        '--input-config',
        json.dumps(conf),
    ] + add_args

    call_buf(params, args, subprocess)
