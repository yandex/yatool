import os
import logging

import yalibrary.tools as tools

from devtools.ya.build.sem_graph import SemException

from devtools.ya.ide.gradle.build import _Builder
from devtools.ya.ide.gradle.common import tracer, YaIdeGradleException
from devtools.ya.ide.gradle.config import _JavaSemConfig
from devtools.ya.ide.gradle.export import _Exporter
from devtools.ya.ide.gradle.graph import _JavaSemGraph
from devtools.ya.ide.gradle.remove import _Remover
from devtools.ya.ide.gradle.stat import _print_stat
from devtools.ya.ide.gradle.symlinks import _NewSymlinkCollector, _collect_symlinks
from devtools.ya.ide.gradle.ya_settings import _YaSettings
from devtools.ya.ide.gradle.wrapper_patcher import _WrapperPatcher


def do_gradle(params):
    """Real handler of `ya ide gradle`"""
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
                with tracer.scope('recollect symlinks'):
                    exists_symlinks, remove_symlinks = _collect_symlinks(config, 'recollect symlinks')

            with tracer.scope('sem-graph'):
                sem_graph = _JavaSemGraph(config)
                sem_graph.make()

            with tracer.scope('export'):
                exporter = _Exporter(config, sem_graph)
                exporter.export()

            _ya_settings = _YaSettings(config)
            _ya_settings.save()

            if config.params.jdk11_compatibility_mode:
                with tracer.scope("patch wrapper"):
                    _wrapper_patcher = _WrapperPatcher(config)
                    _wrapper_patcher.patch_wrapper()

            with tracer.scope('make symlinks'):
                new_symlinks = _NewSymlinkCollector(exists_symlinks, remove_symlinks)
                new_symlinks.collect(sem_graph.generated_symlinks)

                remove_symlinks.remove()
                new_symlinks.create()
                exists_symlinks.save()

                if new_symlinks.has_errors:
                    raise YaIdeGradleException(
                        'Some errors during creating symlinks, read the logs for more information'
                    )

            with tracer.scope('build'):
                builder = _Builder(config, sem_graph)
                builder.build()

        except SemException as e:
            logging.error("%s", str(e))

    _print_stat()

    logging.info(
        'Codestyle config: %s. You can import this file with "File -> Manage IDE Settings -> Import settings..." command. '
        'After this choose "yandex-arcadia" in code style settings (Preferences -> Editor -> Code Style).',
        os.path.join(tools.tool('idea_style_config'), 'intellij-codestyle.jar'),
    )
