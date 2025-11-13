import os
import logging

import yalibrary.tools as tools

import exts.asyncthread as core_async
from devtools.ya.build.sem_graph import SemException

from devtools.ya.ide.gradle.build import _Builder
from devtools.ya.ide.gradle.common import tracer, YaIdeGradleException
from devtools.ya.ide.gradle.config import _JavaSemConfig
from devtools.ya.ide.gradle.export import _Exporter
from devtools.ya.ide.gradle.graph import _JavaSemGraph
from devtools.ya.ide.gradle.remove import _Remover
from devtools.ya.ide.gradle.stat import _print_stat
from devtools.ya.ide.gradle.symlinks import (
    _ExistsSymlinkCollector,
    _RemoveSymlinkCollector,
    _NewSymlinkCollector,
    _collect_symlinks,
)
from devtools.ya.ide.gradle.ya_settings import _YaSettings


def _do_ya_settings(config: _JavaSemConfig) -> None:
    _ya_settings = _YaSettings(config)
    _ya_settings.save()


def _async_ya_settings(config: _JavaSemConfig):
    return core_async.future(lambda: _do_ya_settings(config), daemon=False)


def _do_export(
    config: _JavaSemConfig,
    sem_graph: _JavaSemGraph,
    exists_symlinks: _ExistsSymlinkCollector,
    remove_symlinks: _RemoveSymlinkCollector,
) -> None:
    with tracer.scope('async>||export'):
        exporter = _Exporter(config, sem_graph)
        exporter.export()

        with tracer.scope('async>||export>make symlinks'):
            new_symlinks = _NewSymlinkCollector(exists_symlinks, remove_symlinks)
            new_symlinks.collect()

            remove_symlinks.remove()
            new_symlinks.create()
            exists_symlinks.save()

            if new_symlinks.has_errors:
                raise YaIdeGradleException('Some errors during creating symlinks, read the logs for more information')


def _async_export(
    config: _JavaSemConfig,
    sem_graph: _JavaSemGraph,
    exists_symlinks: _ExistsSymlinkCollector,
    remove_symlinks: _RemoveSymlinkCollector,
):
    return core_async.future(lambda: _do_export(config, sem_graph, exists_symlinks, remove_symlinks), daemon=False)


def _do_build(config: _JavaSemConfig, sem_graph: _JavaSemGraph) -> None:
    with tracer.scope('async>||build'):
        builder = _Builder(config, sem_graph)
        builder.build()


def _async_build(config: _JavaSemConfig, sem_graph: _JavaSemGraph):
    return core_async.future(lambda: _do_build(config, sem_graph), daemon=False)


def do_gradle(params) -> int:
    """Real handler of `ya ide gradle`"""
    success = True
    with tracer.scope('summary'):
        try:
            config = _JavaSemConfig(params)

            with tracer.scope('collect symlinks'):
                exists_symlinks, remove_symlinks = _collect_symlinks(config)

            if config.params.remove or config.params.reexport:
                with tracer.scope('remove'):
                    remover = _Remover(config, remove_symlinks)
                    remover.remove()
                if not config.params.reexport:
                    return
                config.sign()
                with tracer.scope('recollect symlinks'):
                    exists_symlinks, remove_symlinks = _collect_symlinks(config, 'recollect symlinks')

            with tracer.scope('sem-graph'):
                sem_graph = _JavaSemGraph(config)
                sem_graph.make()

            with tracer.scope('async'):
                wait_ya_settings = _async_ya_settings(config)
                wait_export = _async_export(config, sem_graph, exists_symlinks, remove_symlinks)
                wait_build = _async_build(config, sem_graph)

                wait_ya_settings()
                wait_export()
                wait_build()

        except (SemException, YaIdeGradleException) as e:
            logging.error("%s", str(e))
            success = False

    if success:
        _print_stat()

        logging.info(
            'Codestyle config: %s. You can import this file with "File -> Manage IDE Settings -> Import settings..." command. '
            'After this choose "yandex-arcadia" in code style settings (Preferences -> Editor -> Code Style).',
            os.path.join(tools.tool('idea_style_config'), 'intellij-codestyle.jar'),
        )

    return 0 if success else -1
