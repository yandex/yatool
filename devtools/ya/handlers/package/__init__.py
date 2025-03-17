import logging

import devtools.ya.app
import app_config
import devtools.ya.core.yarg
import devtools.ya.core.common_opts
import devtools.ya.build.build_opts as build_opts
import devtools.ya.test.opts as test_opts

import package.docker
import package.packager
import devtools.ya.handlers.package.opts as package_opts
from devtools.ya.core.yarg.help_level import HelpLevel

logger = logging.getLogger(__name__)


class PackageYaHandler(devtools.ya.core.yarg.OptsHandler):
    description = "Build package using json package description in the release build type by default."
    in_house_docs = "For more info see https://docs.yandex-team.ru/ya-make/usage/ya_package"

    def __init__(self):
        super().__init__(
            action=devtools.ya.app.execute(package.packager.do_package, respawn=devtools.ya.app.RespawnType.OPTIONAL),
            description=self.description + ("\n" + self.in_house_docs if app_config.in_house else ""),
            examples=[
                devtools.ya.core.yarg.UsageExample(
                    cmd='{prefix} <path to json description>',
                    description='Create tarball package from json description',
                )
            ],
            opts=[
                package_opts.PackageOperationalOptions(),
                package_opts.PackageCustomizableOptions(),
                package_opts.InterimOptions(),
                package_opts.Dist2RepoCustomizableOptions(),
                devtools.ya.core.common_opts.LogFileOptions(),
                devtools.ya.core.common_opts.EventLogFileOptions(),
                build_opts.BuildTypeOptions('release'),
                build_opts.BuildThreadsOptions(build_threads=None),
                devtools.ya.core.common_opts.CrossCompilationOptions(),
                build_opts.ArcPrefetchOptions(),
                build_opts.ContentUidsOptions(),
                build_opts.KeepTempsOptions(),
                build_opts.RebuildOptions(),
                build_opts.StrictInputsOptions(),
                build_opts.DumpReportOptions(),
                build_opts.OutputOptions(),
                build_opts.AuthOptions(),
                build_opts.YMakeDumpGraphOptions(),
                build_opts.YMakeDebugOptions(),
                build_opts.YMakeBinOptions(),
                build_opts.YMakeRetryOptions(),
                build_opts.YMakeModeOptions(),
                build_opts.ExecutorOptions(),
                build_opts.ForceDependsOptions(),
                build_opts.IgnoreRecursesOptions(),
                build_opts.GraphFilterOutputResultOptions(),
                build_opts.GraphOperateResultsOptions(),
                devtools.ya.core.common_opts.CustomSourceRootOptions(),
                devtools.ya.core.common_opts.CustomBuildRootOptions(),
                devtools.ya.core.common_opts.ShowHelpOptions(),
                devtools.ya.core.common_opts.BeVerboseOptions(),
                devtools.ya.core.common_opts.HtmlDisplayOptions(),
                devtools.ya.core.common_opts.CommonUploadOptions(),
                build_opts.SandboxUploadOptions(ssh_key_option_name="--ssh-key", visible=HelpLevel.BASIC),
                build_opts.MDSUploadOptions(),
                devtools.ya.core.common_opts.TransportOptions(),
                build_opts.CustomFetcherOptions(),
                build_opts.DistCacheOptions(),
                build_opts.FlagsOptions(),
                build_opts.PGOOptions(),
                test_opts.RunTestOptions(),
                test_opts.DebuggingOptions(),
                # strip_idle_build_results must be False to avoid removal of build nodes which are
                # reachable due RECURSE and used in package, but not required for tests
                test_opts.DepsOptions(strip_idle_build_results=False),
                test_opts.FileReportsOptions(),
                test_opts.FilteringOptions(test_size_filters=None),
                test_opts.PytestOptions(),
                test_opts.JUnitOptions(),
                test_opts.RuntimeEnvironOptions(),
                test_opts.TestToolOptions(),
                test_opts.UidCalculationOptions(cache_tests=False),
                test_opts.LintersOptions(),
                devtools.ya.core.common_opts.YaBin3Options(),
                devtools.ya.core.common_opts.OutputStyleOptions(),
                devtools.ya.core.common_opts.PrintStatisticsOptions(),
            ]
            + build_opts.distbs_options()
            + build_opts.checkout_options()
            + build_opts.svn_checkout_options(),
        )
