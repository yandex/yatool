from __future__ import absolute_import
import copy
import os
import itertools
import xml.etree.ElementTree as et

import devtools.ya.app
import build.build_handler as bh
import build.build_opts
import core.yarg
from exts import fs
from exts import path2
from yalibrary.tools import toolchain_root
import devtools.ya.ide.ide_common
from .clion2016 import create_plugin_config, get_vcs
from six.moves import map
from six.moves import filterfalse


CODEGEN_EXTS = [".go", ".gosrc"]
SUPPRESS_CODEGEN_EXTS = [".cgo1.go", "_cgo_gotypes.go", "_cgo_import.go"]

EXLUDES = ['contrib/', 'library/go/', 'vendor/', 'tools/go_test_miner/']


TARGETS = [
    'library/go',
    'vendor',
]


ACCEPTABLE_EXTENSIONS = [
    '.go',
    '.proto',
]


GO_MODULES_WORKSPACE = """
<?xml version="1.0" encoding="UTF-8"?>
<project version="4">
    <component name="GOROOT" url="{goroot}" />
    <component name="GoLibraries">
        <option name="indexEntireGoPath" value="false" />
        <option name="useGoPathFromSystemEnvironment" value="false" />
    </component>
    <component name="VgoProject">
        <integration-enabled>true</integration-enabled>
    </component>
    <component name="RunManager">
        <configuration default="true" type="GoApplicationRunConfiguration" factoryName="Go Application">
          <envs>
            <env name="CGO_ENABLED" value="0" />
          </envs>
        </configuration>
        <configuration default="true" type="GoTestRunConfiguration" factoryName="Go Test">
          <envs>
            <env name="CGO_ENABLED" value="0" />
          </envs>
        </configuration>
    </component>
</project>
"""

LEGACY_WORKSPACE = """
<?xml version="1.0" encoding="UTF-8"?>
<project version="4">
    <component name="GOROOT" url="{goroot}" />
    <component name="GoLibraries">
        <option name="indexEntireGoPath" value="false" />
    </component>
    <component name="PropertiesComponent">
        <property name="go.tried.to.enable.integration.vgo.integrator" value="true" />
    </component>
    <component name="RunManager">
        <configuration default="true" type="GoApplicationRunConfiguration" factoryName="Go Application">
          <envs>
            <env name="CGO_ENABLED" value="0" />
          </envs>
        </configuration>
        <configuration default="true" type="GoTestRunConfiguration" factoryName="Go Test">
          <envs>
            <env name="CGO_ENABLED" value="0" />
          </envs>
        </configuration>
    </component>
</project>
"""

WATCHERS = """
<?xml version="1.0" encoding="UTF-8"?>
<project version="4">
  <component name="ProjectTasksOptions">
    <TaskOptions isEnabled="true">
      <option name="arguments" value="tool yoimports -w $FilePath$" />
      <option name="checkSyntaxErrors" value="true" />
      <option name="description" />
      <option name="exitCodeBehavior" value="ERROR" />
      <option name="fileExtension" value="go" />
      <option name="immediateSync" value="false" />
      <option name="name" value="yoimports" />
      <option name="output" value="$FilePath$" />
      <option name="outputFilters">
        <array />
      </option>
      <option name="outputFromStdout" value="false" />
      <option name="program" value="{ya_tool}" />
      <option name="runOnExternalChanges" value="false" />
      <option name="scopeName" value="Project Files" />
      <option name="trackOnlyRoot" value="true" />
      <option name="workingDir" value="$ProjectFileDir$" />
      <envs>
        <env name="GOROOT" value="$GOROOT$" />
        <env name="GOPATH" value="$GOPATH$" />
        <env name="PATH" value="$GoBinDirs$" />
      </envs>
    </TaskOptions>
  </component>
</project>
"""


class GolandOptions(core.yarg.Options):
    GROUP = core.yarg.Group('Goland options', 0)

    def __init__(self):
        self.go_modules = True
        self.yoimports = False
        self.codegen_enabled = True

    @classmethod
    def consumer(cls):
        consumers = [
            core.yarg.ArgConsumer(
                ['--with-go-modules'],
                help='Enable go modules support',
                hook=core.yarg.SetConstValueHook('go_modules', True),
                group=cls.GROUP,
                visible=False,
            ),
            core.yarg.ArgConsumer(
                ['--without-go-modules'],
                help='Disable go modules support',
                hook=core.yarg.SetConstValueHook('go_modules', False),
                group=cls.GROUP,
                visible=True,
            ),
            core.yarg.ArgConsumer(
                ['--with-yoimports'],
                help='Enable yoimports watcher',
                hook=core.yarg.SetConstValueHook('yoimports', True),
                group=cls.GROUP,
                visible=True,
            ),
            core.yarg.ArgConsumer(
                ['--without-yoimports'],
                help='Disable yoimports watcher',
                hook=core.yarg.SetConstValueHook('yoimports', False),
                group=cls.GROUP,
                visible=False,
            ),
            core.yarg.ArgConsumer(
                ['--no-codegen'],
                help="Do not run codegeneration",
                hook=core.yarg.SetConstValueHook('codegen_enabled', False),
                group=cls.GROUP,
            ),
        ]
        return consumers


def find_excludes(main_root, targets):
    exclude_roots = set()

    include_tree = dict()
    for target in targets:
        cur = None
        for part in target.split('/'):
            if cur is None:
                if part not in include_tree:
                    include_tree[part] = {}
                cur = include_tree[part]
                continue

            if part not in cur:
                cur[part] = {}
            cur = cur[part]

    def visit(root, includes):
        if not includes:
            return

        for x in os.listdir(root):
            cur = os.path.join(root, x)
            if os.path.isdir(cur):
                if x in includes:
                    visit(cur, includes[x])
                else:
                    exclude_roots.add(cur)

    visit(main_root, include_tree)

    return exclude_roots


def goroot_url():
    return 'file://{}'.format(toolchain_root('go', None, None))


def patch_workspace(filename):
    root = et.parse(filename)
    goroot_entry = root.find(path='component[@name="GOROOT"]')
    if goroot_entry is None:
        return

    goroot_entry.attrib['url'] = goroot_url()
    root.write(filename, encoding='UTF-8', xml_declaration=True)


def gen_idea_prj(project_info, app_ctx, targets, params):
    idea_dir = os.path.join(project_info.output_path, '.idea')
    fs.create_dirs(idea_dir)
    excludes = find_excludes(project_info.params.arc_root, targets)

    iml_name = project_info.title + '.iml'
    with open(os.path.join(idea_dir, 'modules.xml'), 'w') as modules_out:
        modules_out.write(
            """<?xml version="1.0" encoding="UTF-8"?>
<project version="4">
  <component name="ProjectModuleManager">
    <modules>
      <module fileurl="file://$PROJECT_DIR$/.idea/{iml}" filepath="$PROJECT_DIR$/.idea/{iml}" />
    </modules>
  </component>
</project>""".format(
                iml=iml_name
            )
        )

    with open(os.path.join(idea_dir, iml_name), 'w') as go_iml:
        go_iml.write(
            """<?xml version="1.0" encoding="UTF-8"?>
<module type="WEB_MODULE" version="4">
  <component name="Go" enabled="true">
    <buildTags>
      <option name="cgo" value="NO" />
    </buildTags>
  </component>
  <component name="NewModuleRootManager">
    <content url="file://{}">""".format(
                project_info.params.arc_root
            )
        )

        for x in excludes:
            go_iml.write('<excludeFolder url="file://{}" />\n'.format(x))

        go_iml.write(
            """
    </content>
    <orderEntry type="sourceFolder" forTests="false" />
  </component>
</module>"""
        )

    vcs_file = os.path.join(idea_dir, 'vcs.xml')
    if not os.path.exists(vcs_file):
        with open(vcs_file, 'w') as vcs_out:
            vcs_out.write(
                """<?xml version="1.0" encoding="UTF-8"?>
<project version="4">
  <component name="VcsDirectoryMappings">
    <mapping directory="{root}" vcs="{vcs}" />
  </component>
</project>""".format(
                    root=project_info.params.arc_root, vcs=get_vcs(project_info.params.arc_root)
                )
            )

    workspace_file = os.path.join(idea_dir, 'workspace.xml')
    if os.path.exists(workspace_file):
        patch_workspace(workspace_file)
    else:
        with open(workspace_file, 'w') as workspace_out:
            content = (GO_MODULES_WORKSPACE if params.go_modules else LEGACY_WORKSPACE).format(goroot=goroot_url())
            workspace_out.write(content.strip())

    watchers_file = os.path.join(idea_dir, 'watcherTasks.xml')
    if params.yoimports and not os.path.exists(watchers_file):
        with open(watchers_file, 'w') as watchers_out:
            content = WATCHERS.format(ya_tool=os.path.join(project_info.params.arc_root, 'ya'))
            watchers_out.write(content.strip())


def is_excluded_source(file):
    if os.path.splitext(file)[1] not in ACCEPTABLE_EXTENSIONS:
        return True

    for exclude in EXLUDES:
        if file.startswith(exclude):
            return True

    return False


def norm_graph_path(data):
    data = path2.normpath(data)
    pos = data.index('/')
    return data[pos + 1 :]


def do_codegen(params):
    devtools.ya.ide.ide_common.emit_message("Running codegen")
    build_params = copy.deepcopy(params)
    build_params.add_result = list(CODEGEN_EXTS)
    build_params.suppress_outputs = list(SUPPRESS_CODEGEN_EXTS)
    build_params.replace_result = True
    build_params.force_build_depends = True
    build_params.continue_on_fail = True
    devtools.ya.app.execute(action=bh.do_ya_make, respawn=devtools.ya.app.RespawnType.NONE)(build_params)


def do_goland(params):
    params.ya_make_extra.append('-DBUILD_LANGUAGES=GO')
    ya_make_opts = core.yarg.merge_opts(build.build_opts.ya_make_options(free_build_targets=True))
    params = core.yarg.merge_params(ya_make_opts.initialize(params.ya_make_extra), params)
    import app_ctx  # XXX

    stub_info = devtools.ya.ide.ide_common.IdeProjectInfo(params, app_ctx, default_output_here=True)

    ide_graph = devtools.ya.ide.ide_common.IdeGraph(params)
    src_dirs = sorted(
        set(
            itertools.chain(
                list(
                    map(
                        os.path.dirname,
                        itertools.chain(
                            filterfalse(
                                is_excluded_source,
                                list(
                                    map(
                                        norm_graph_path,
                                        ide_graph.iter_source_files(),
                                    )
                                ),
                            ),
                            filterfalse(
                                is_excluded_source,
                                list(
                                    map(
                                        norm_graph_path,
                                        ide_graph.iter_build_files(),
                                    )
                                ),
                            ),
                        ),
                    )
                ),
                params.rel_targets,
            ),
        )
    )

    targets = list(TARGETS)
    last_dir = None
    for src_dir in src_dirs:
        if last_dir and src_dir.startswith(last_dir):
            continue
        last_dir = src_dir + '/'
        targets.append(src_dir)

    if params.codegen_enabled:
        do_codegen(params)

    gen_idea_prj(stub_info, app_ctx, targets, params)
    create_plugin_config(stub_info)
