import devtools.ya.build.build_opts as build_opts

import devtools.ya.core.yarg
import devtools.ya.core.common_opts as common_opts

import devtools.ya.test.opts as test_opts


class JavaBuildOptions(devtools.ya.core.yarg.Options):
    def __init__(self, use_distbuild=False):
        self.get_deps = None
        self.dump_graph = False
        self.run_tests = False
        self.javac_flags = {}
        self.dump_sources = False
        self.validate = False
        self.use_distbuild = use_distbuild

    @staticmethod
    def consumer():
        return [
            devtools.ya.core.yarg.ArgConsumer(
                ['-D', '--get-deps'],
                help='Compile and collect all dependencies in specified directory',
                hook=devtools.ya.core.yarg.SetValueHook('get_deps'),
                group=devtools.ya.core.yarg.BULLET_PROOF_OPT_GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['-G', '--dump-json-graph'],
                help='Dump build graph json',
                hook=devtools.ya.core.yarg.SetConstValueHook('dump_graph', True),
                group=devtools.ya.core.yarg.BULLET_PROOF_OPT_GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--javac-core.yarg', '-J'],
                help='Set common javac flags',
                hook=devtools.ya.core.yarg.DictPutHook('javac_flags', None),
                group=devtools.ya.core.yarg.ADVANCED_OPT_GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['-s', '--sources'],
                help='Make sources jar also',
                hook=devtools.ya.core.yarg.SetConstValueHook('dump_sources', True),
                group=devtools.ya.core.yarg.BULLET_PROOF_OPT_GROUP,
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['--validate'],
                help='Validate dependencies',
                hook=devtools.ya.core.yarg.SetConstValueHook('validate', True),
                group=devtools.ya.core.yarg.BULLET_PROOF_OPT_GROUP,
            ),
        ]


class IdeaProjectOptions(devtools.ya.core.yarg.Options):
    def __init__(self):
        self.idea_project_root = None
        self.local = False

    @staticmethod
    def consumer():
        return [
            devtools.ya.core.yarg.ArgConsumer(
                ['--idea'], help='Idea project path', hook=devtools.ya.core.yarg.SetValueHook('idea_project_root')
            ),
            devtools.ya.core.yarg.ArgConsumer(
                ['-l', '--local'],
                help='Only recurse reachable projects are idea modules',
                hook=devtools.ya.core.yarg.SetConstValueHook('local', True),
            ),
        ]

    def postprocess(self):
        if self.idea_project_root:
            import os

            dirname, basename = os.path.split(self.idea_project_root)

            if not basename:
                self.idea_project_root = dirname


def jbuild_opts(use_distbuild=False):
    return (
        [
            build_opts.BuildThreadsOptions(build_threads=8),
            build_opts.BuildTargetsOptions(with_free=True),
            JavaBuildOptions(use_distbuild=use_distbuild),
            build_opts.OutputOptions(),
            build_opts.CreateSymlinksOptions(),
        ]
        + test_opts.test_options()
        + [
            common_opts.OutputStyleOptions(),
            common_opts.ShowHelpOptions(),
            common_opts.BeVerboseOptions(),
            common_opts.KeepTempsOptions(),
            IdeaProjectOptions(),
            build_opts.RebuildOptions(),
            build_opts.ContinueOnFailOptions(),
        ]
    )
