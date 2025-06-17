import datetime
import itertools

import logging

import os

import exts.hashing
import exts.fs
import exts.archive
import exts.yjson

from devtools.ya.build.build_opts import SandboxAuthOptions

from devtools.ya.core.yarg import (
    Options,
    SingleFreeArgConsumer,
    SetValueHook,
    ArgConsumer,
    SetConstValueHook,
    ArgsValidatingException,
    UsageExample,
)
from devtools.ya.core.yarg import OptsHandler
from devtools.ya.core.common_opts import DumpDebugCommonOptions, EventLogFileOptions, ShowHelpOptions
import devtools.ya.core.config
import devtools.ya.core.logger
import devtools.ya.core.yarg.help_level
import devtools.ya.handlers.dump.reproducer as reproducer

import yalibrary.evlog as evlog_lib

from pathlib import Path

try:
    from . import dump_upload
except ImportError:
    dump_upload = None

from yalibrary.debug_store.processor import DumpProcessor

import devtools.ya.app


class DumpDebugProcessingOptions(Options):
    def __init__(self):
        self.dump_debug_choose = None
        self.dry_run = False
        self.upload = True
        self.resource_owner = None

    @staticmethod
    def consumer():
        return [
            SingleFreeArgConsumer(
                hook=SetValueHook('dump_debug_choose'),
                help='Choose item for processing',
                required=False,
            ),
            ArgConsumer(
                ['--no-upload'],
                help="Do not upload to sandbox",
                hook=SetConstValueHook('upload', False),
                visible=devtools.ya.core.yarg.help_level.HelpLevel.ADVANCED,
            ),
            ArgConsumer(
                ['--dry-run'],
                help="Dry run",
                hook=SetConstValueHook('dry_run', True),
                visible=devtools.ya.core.yarg.help_level.HelpLevel.BASIC,
            ),
            ArgConsumer(
                ['--resource-owner'],
                help="Sandbox resource owner",
                hook=SetValueHook('resource_owner'),
                visible=devtools.ya.core.yarg.help_level.HelpLevel.ADVANCED,
            ),
        ]

    def postprocess(self):
        if not self.dump_debug_choose:
            return

        if self.dump_debug_choose == "last":
            self.dump_debug_choose = 1

        try:
            self.dump_debug_choose = int(self.dump_debug_choose)
            assert self.dump_debug_choose >= 0
        except BaseException:
            raise ArgsValidatingException(
                "Item must be non-positive number or `last`, not {}".format(self.dump_debug_choose)
            )


def do_dump_debug(params):
    import app_ctx

    evlog = getattr(app_ctx, "evlog", None)

    if evlog is None:
        app_ctx.display.emit_message("[[bad]]Event log not found in app context[[rst]]")
        return -1

    proc = DumpProcessor(path=params.dump_debug_path, evlog=evlog)

    if not proc:
        app_ctx.display.emit_message("[[bad]]Could not find bundles[[rst]]")
        return -1

    if params.dump_debug_choose is None:
        for i, item in enumerate(proc):
            index = len(proc) - i
            try:
                app_ctx.display.emit_message("{}: {}".format(index, item))
            except Exception as e:
                app_ctx.display.emit_message("[[bad]]Error while print item #{}: {}[[rst]]".format(index, e))
        return

    try:
        item = proc[params.dump_debug_choose]
    except IndexError:
        app_ctx.display.emit_message("[[bad]]Wrong bundle number[[rst]]")
        return -1

    if params.dump_debug_choose != 1:
        logging.warning("You have chosen not the last item, some files may have been changed")

    app_ctx.display.emit_message("Will be processed: [[imp]]{}[[rst]]".format(item))

    app_ctx.display.emit_status("Processing...")
    # Здесь я неявно полагаюсь на то, что ya сохраняет logs и evlogs в чанки по дням:
    # https://a.yandex-team.ru/arc_vcs/devtools/ya/yalibrary/file_log/__init__.py?rev=e0fc01eaad7f9b59cfd26f78727ac2ba6b500265
    # https://a.yandex-team.ru/arc_vcs/devtools/ya/yalibrary/evlog/__init__.py?rev=d7174b2ea0b2b98cdaa5badb2b5e051df0718bda#L15
    # Если это поведение изменится — нужно править и здесь.
    # TODO: В идеале нужно вынести работу с чанками в отдельную библиотеку
    _LOG_DIR_NAME_FMT = '%Y-%m-%d'

    misc_root = Path(devtools.ya.core.config.misc_root())
    today = datetime.datetime.now()
    yesterday = today - datetime.timedelta(days=1)

    additional_paths = [
        (
            "{}_from_{}".format(folder, chunk.strftime(_LOG_DIR_NAME_FMT)),
            misc_root / folder / chunk.strftime(_LOG_DIR_NAME_FMT),
        )
        for folder, chunk in itertools.product(('logs', 'evlogs'), (today, yesterday))
    ]

    item.load()

    _decompress_evlog_if_needed(item)

    # Store all tools_cache logs

    tools_cache_root = item.debug_bundle_data.get('tools_cache_root', None)
    if tools_cache_root and os.path.exists(tools_cache_root):
        additional_paths.extend(_discovery_folder(tools_cache_root, "fallback.log", "tools_cache_log"))

    # Store all build_cache logs

    build_cache_root = item.debug_bundle_data.get('build_cache_root', None)
    if build_cache_root and os.path.exists(build_cache_root):
        additional_paths.extend(_discovery_folder(build_cache_root, "fallback.log", "build_cache_log"))
        additional_paths.extend(_discovery_folder(build_cache_root, "blobs.log", "build_cache_blobs_log"))

    changelist_store = item.debug_bundle_data.get('changelist_store', None)
    if changelist_store and Path(changelist_store).exists():
        additional_paths.append(("changelist_store", Path(changelist_store)))

    try:
        repro_manager = reproducer.Reproducer(item)
        fully_restored_repo, path, makefile_path = repro_manager.prepare_reproducer()
        additional_paths.append(("reproducer", path))
        move_paths = [(makefile_path, Path("/Makefile"))]
    except Exception as exc:
        logging.debug("Failed to gather reproducer due to exception", exc_info=exc)
        path = None
        fully_restored_repo = False
        move_paths = None

    item.process(
        additional_paths,
        is_last=params.dump_debug_choose == 1,
        move_paths=move_paths,
        path_to_repro=path,
        fully_restored_repo=fully_restored_repo,
    )

    # In OPENSOURCE version we do not upload anything and just create archive file
    if dump_upload is None or not params.upload:
        working_dir = str(item.workdir)
        try:
            archive_filename = 'dump_debug.tar.zst'
            archive_path = os.path.join(os.getcwd(), archive_filename)
            folder_to_archive = working_dir
            logging.debug('Archiving folder %s', folder_to_archive)
            exts.archive.create_tar([folder_to_archive], archive_path, compression_filter=exts.archive.ZSTD)
            app_ctx.display.emit_message('Archive created: [[imp]]{}[[rst]]'.format(archive_filename))
            logging.debug("Remove workdir %s", item.workdir)
            exts.fs.ensure_removed(working_dir)
        finally:
            if not params.dry_run:
                logging.debug("Remove %s", item.workdir)
                exts.fs.ensure_removed(working_dir)
        return 0

    dump_upload.upload(item, params, app_ctx)
    return 0


def _decompress_evlog_if_needed(item):
    path = item.path

    if not path:
        return

    path_as_str = str(path)
    if not evlog_lib.is_compressed(path_as_str):
        return

    decompressed_evlog_filepath = os.path.splitext(path_as_str)[0]
    if os.path.exists(decompressed_evlog_filepath):
        return

    evlog_reader = evlog_lib.EvlogReader(path_as_str)
    with open(decompressed_evlog_filepath, "a+") as fout:
        for record in evlog_reader:
            fout.write(exts.yjson.dumps(record) + '\n')


def _discovery_folder(tools_cache_root, base_name, item_key):
    for item_ in os.listdir(tools_cache_root):  # type: str
        if item_.startswith(base_name):
            index = item_[len(base_name) :] or "0"
            yield ("{}_{}".format(item_key, index), Path(tools_cache_root) / item_)


debug_handler = OptsHandler(
    action=devtools.ya.app.execute(action=do_dump_debug),
    description="Utils for work with debug information stored by last ya runs",
    opts=[
        DumpDebugCommonOptions(),
        DumpDebugProcessingOptions(),
        EventLogFileOptions(),
        SandboxAuthOptions(),
        ShowHelpOptions(),
    ],
    visible=True,
    examples=[
        UsageExample('{prefix} last', 'Upload last debug item'),
        UsageExample('{prefix} 3', 'Upload third from the end debug bundle'),
        UsageExample('{prefix}', 'Show all items'),
        UsageExample('{prefix} last --dry-run', 'Collect, but not upload last debug bundle'),
    ],
)
