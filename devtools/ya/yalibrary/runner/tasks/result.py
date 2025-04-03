import os
import sys
import logging

from devtools.ya.build.graph_description import GraphNodeUid
from exts.fs import get_file_size
from exts.windows import on_win, RETRIABLE_FILE_ERRORS
import exts.archive

import yalibrary.worker_threads as worker_threads
import yalibrary.runner.fs

from yalibrary.runner.tasks.enums import WorkerPoolType


logger = logging.getLogger(__name__)


type ResultArtifacts = dict[GraphNodeUid, list[dict]]


TARED_NONE = 0
TARED_DIR = 1
TARED_NODIR = 2

TARED_KIND_STR_MAP = {
    'dir': TARED_DIR,
    'nodir': TARED_NODIR,
}


def str_to_tared_kind(tared_kind_str):
    return TARED_KIND_STR_MAP.get(tared_kind_str, TARED_NONE)


def _untared_path(path):
    exts = ['.tar', '.tar.zstd']
    for ext in exts:
        if path.endswith(ext):
            new_path = path[: -len(ext)]
            if new_path:
                return new_path
    return path + '_unpacked'


def process_output(
    self, output, pat, tared_kind=TARED_NONE, legacy_install=False, legacy_bin_dir=False, legacy_lib_dir=False
):
    if not output.startswith('$(B'):
        return False

    built = pat.fill(output)

    if not os.path.isfile(built):
        logger.debug('Cannot build target %s', output)
        # self.err[output].append([])
        return False

    result = {}

    if tared_kind == TARED_DIR:
        output = _untared_path(output)

    path = pat.fill(output.replace('$(BUILD_ROOT)', ''))

    def wrap_os_error(func, built, output, **kwargs):
        try:
            return func(built, output, **kwargs)
        except Exception as exc:
            import errno

            if isinstance(exc, OSError) and exc.errno in (
                errno.EEXIST,
                errno.ENOENT,
            ):
                logger.debug('Concurrent processing of result %s from %s', output, built)
            elif on_win() and isinstance(exc, WindowsError) and exc.winerror in RETRIABLE_FILE_ERRORS:
                logger.debug('Concurrent processing of result %s from %s', output, built)
            else:
                raise

    def make_output(built, output, tared):
        if tared:
            exts.archive.extract_from_tar(built, output, apply_mtime=True)
        else:
            if os.path.exists(output) and os.path.isdir(output):
                logger.warning("Not creating hardlink to %s: %s exists and is a directory", built, output)
                return

            yalibrary.runner.fs.make_hardlink(built, output, retries=1, prepare=True)
            built_file_size = get_file_size(built)
            output_file_size = get_file_size(output)
            if built_file_size != output_file_size:
                logger.warning('File %s size mismatch: expected %d got %d', output, built_file_size, output_file_size)

    def suppress_output():
        default_suppress_outputs = self._suppress_outputs_conf['default_suppress_outputs']
        add_result = self._suppress_outputs_conf['add_result']
        suppress_outputs = self._suppress_outputs_conf['suppress_outputs']
        return (
            any([path.endswith(x) for x in default_suppress_outputs])
            and not any([path.endswith(x) for x in add_result])
        ) or any([path.endswith(x) for x in suppress_outputs])

    if self._need_test_trace_file and built.endswith('ytest.report.trace'):
        result['trace_file'] = built

    tared = tared_kind != TARED_NONE
    tared_nodir = tared_kind == TARED_NODIR

    if not suppress_output():
        # Persistent output on demand
        if self._need_output:
            result['artifact'] = wrap_os_error(
                self._output_result.put,
                built,
                path,
                action=make_output,
                forced_existing_dir_removal=tared,
                tared=tared,
                tared_nodir=tared_nodir,
            )

        # Output for symlinks
        if self._need_symlinks:
            result['symlink'] = wrap_os_error(
                self._symlink_result.put,
                built,
                path,
                target_action=make_output,
                forced_existing_dir_removal=tared,
                tared=tared,
                tared_nodir=tared_nodir,
            )

        if legacy_install:
            wrap_os_error(
                self._install_result.put,
                built,
                path,
                action=make_output,
                forced_existing_dir_removal=tared,
            )

        if legacy_bin_dir:
            wrap_os_error(
                self._bin_result.put,
                built,
                path,
                action=make_output,
                forced_existing_dir_removal=tared,
            )

        if legacy_lib_dir:
            wrap_os_error(
                self._lib_result.put,
                built,
                path,
                action=make_output,
                forced_existing_dir_removal=tared,
            )

    self._res[self._node.uid].append(result)

    return True


class ResultNodeTask(object):
    node_type = 'ResultNode'
    worker_pool_type = WorkerPoolType.BASE

    def __init__(
        self,
        node,
        ctx,
        callback,
        need_output,
        output_result,
        need_symlinks,
        symlink_result,
        suppress_outputs_conf,
        install_result,
        bin_result,
        lib_result,
        res,
        need_test_trace_file,
        provider=None,
    ):
        self._ok = True
        self._node = node
        self._ctx = ctx
        self._callback = callback
        self._build_root = None
        self._need_output = need_output
        self._output_result = output_result
        self._need_symlinks = need_symlinks
        self._suppress_outputs_conf = suppress_outputs_conf
        self._symlink_result = symlink_result
        self._install_result = install_result
        self._bin_result = bin_result
        self._lib_result = lib_result
        self._res = res
        self._need_test_trace_file = need_test_trace_file

        self._provider = provider
        logger.debug("ResultsNodeTask for %s created (provider=%s)", self._node.uid, self._provider)

    def __call__(self, deps):
        for x in [self._provider] if self._provider else deps:
            if hasattr(x, 'build_root') and x.build_root.ok:
                pat = self._ctx.patterns.sub()
                pat['BUILD_ROOT'] = x.build_root.path
                self._build_root = x.build_root.path

                bin_target = self._node.target_properties.get('module_type') == 'bin'
                so_target = self._node.target_properties.get('module_type') == 'so'
                union_target = self._node.target_properties.get('module_type') == 'bundle'
                legacy_install = (bin_target or so_target or union_target) and self._ctx.opts.install_dir
                legacy_bin_dir = bin_target and self._ctx.opts.generate_bin_dir
                legacy_lib_dir = so_target and self._ctx.opts.generate_lib_dir

                for o in x.build_root.result_output:
                    if o not in self._node.tared_outputs:
                        self._ok &= process_output(
                            self,
                            o,
                            pat,
                            legacy_install=legacy_install,
                            legacy_bin_dir=legacy_bin_dir,
                            legacy_lib_dir=legacy_lib_dir,
                        )
                    else:
                        self._ok &= process_output(
                            self, o, pat, str_to_tared_kind(self._node.kv.get('tared_kind', 'dir'))
                        )

                x.build_root.dec()

                break
        else:
            self._ok = False
        self._callback(
            {
                'uid': self._node.uid,
                'status': 0 if self._ok else 1,
                'build_root': self._build_root,
                'result_node': True,
            }
        )
        logger.debug("Result node %s processed successfully: %s", self._node.uid, self._ok)

    def __str__(self):
        return 'Result({})'.format(str(self._node))

    def res(self):
        return worker_threads.ResInfo(io=1)

    def status(self):
        tags = ['[[alt1]]RESULT[[rst]]']
        if not self._ok:
            tags.append('[[bad]]FAILED[[rst]]')

        return self._node.fmt(tags)

    def prio(self):
        return sys.maxsize

    def short_name(self):
        return 'result[{}]'.format(self._node.kv.get('p', '??'))
