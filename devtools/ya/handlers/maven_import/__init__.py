from __future__ import absolute_import
import devtools.ya.core.yarg
import devtools.ya.jbuild.maven.maven_import as mi
import devtools.ya.core.common_opts as common_opts
import build.build_opts as build_opts
import devtools.ya.app
import app_config

from devtools.ya.core.yarg.help_level import HelpLevel


class MavenImportYaHandler(devtools.ya.core.yarg.OptsHandler):
    description = 'Import specified artifact from remote maven repository to arcadia/contrib/java.'
    visible = app_config.in_house

    def __init__(self):
        super(MavenImportYaHandler, self).__init__(
            action=devtools.ya.app.execute(mi.do_import),
            opts=[
                build_opts.MavenImportOptions(visible=HelpLevel.BASIC),
                build_opts.BuildThreadsOptions(build_threads=None),
                common_opts.OutputStyleOptions(),
                common_opts.TransportOptions(),
                common_opts.ShowHelpOptions(),
                build_opts.YMakeBinOptions(),
                build_opts.CustomFetcherOptions(),
                build_opts.ToolsOptions(),
            ]
            + build_opts.checkout_options(),
            description=self.description,
        )
