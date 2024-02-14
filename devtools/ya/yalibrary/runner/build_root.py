import errno
import json
import os
import logging
import subprocess
import sys
import threading
import time

import six

import exts.func

from exts import archive
from exts import uniq_id
from exts import filelock
from exts import fs
from exts import hashing

from yalibrary import formatter
from yalibrary.runner import fs as runner_fs
from yalibrary.runner import worker_threads

from devtools.libs.limits.python import limits

STAMP_FILE = 'STAMP'
CONTENT_HASH_FILE_NAME = '.content_hash.md5'
EMPTY_DIR_OUTPUTS_META = '.empty_dir_outputs.json'

logger = logging.getLogger(__name__)


class BuildRootError(Exception):
    pass


class BuildRootIntegrityError(BuildRootError):
    pass


class OutputsExceedLimitError(BuildRootError):
    pass


class BuildRoot(object):
    def __init__(
        self,
        path,
        keep,
        outputs,
        limit_output_size,
        refcount,
        dir_outputs=None,
        validate_content=False,
        compute_hash=True,
    ):
        self.path = path
        self._keep = keep
        self._original_outputs = list(outputs)
        self._outputs = [x.replace('$(BUILD_ROOT)', path) for x in outputs]
        self._initial_outputs = list(self._outputs)
        self._limit_output_size = limit_output_size
        self._refcount = refcount
        self._created = False
        self._lock = threading.Lock()
        self._ok = False
        self._dir_outputs = []
        self._validate_content = validate_content
        self._compute_hash = compute_hash
        if dir_outputs:
            self._dir_outputs = [x.replace('$(BUILD_ROOT)', path) for x in dir_outputs]

    @exts.func.lazy_property
    def dir_outputs_files(self):
        dir_output_files = []
        empty_dirs = []
        for dir_output in self._dir_outputs:
            if not os.path.exists(dir_output):
                continue
            for root, dirs, files in os.walk(dir_output):
                for f in files:
                    dir_output_files.append(os.path.join(root, f))
                if not dirs and not files:
                    empty_dirs.append(os.path.relpath(root, self.path))
        empty_meta_path = os.path.join(self.path, EMPTY_DIR_OUTPUTS_META)
        if os.path.exists(empty_meta_path):
            # case when we're restoring dir outputs from cache
            # in cache we have prepared EMPTY_DIR_OUTPUTS_META,
            # but there we don't have empty dirs
            dir_output_files.append(empty_meta_path)
        elif empty_dirs:
            with open(empty_meta_path, 'w') as afile:
                json.dump(empty_dirs, afile)
            dir_output_files.append(empty_meta_path)
        return dir_output_files

    @property
    def raw_output(self):
        return self._original_outputs

    @property
    def result_output(self):
        for x in self._original_outputs:
            if not x.endswith((CONTENT_HASH_FILE_NAME, EMPTY_DIR_OUTPUTS_META)):
                yield x

    @property
    def output(self):
        for x in self._outputs:
            yield x

    def add_output(self, new_output):
        self._outputs.append(new_output.replace('$(BUILD_ROOT)', self.path))
        self._original_outputs.append(new_output)

    def validate(self):
        for x in self.output:
            if not os.path.exists(x):
                raise BuildRootIntegrityError('Cannot find {} in build root'.format(x))

        if self._validate_content:
            if self._compute_hash and self._hash_file():
                cached_hash = self.read_hashes()
                immediate_hash = self.read_hashes(force_recalc=True)
                if cached_hash != immediate_hash:
                    raise BuildRootIntegrityError(
                        'Content hash mismatch in {}: {} != {}'.format(self.path, cached_hash, immediate_hash)
                    )

        if self._limit_output_size:
            self.validate_outputs_size()

        self._ok = True

    def validate_outputs_size(self):
        file_sizes = {x: os.path.getsize(x) for x in self.output}
        total_files_size = sum(file_sizes.values())
        max_size = self.output_node_limit

        if total_files_size > max_size:
            top_5 = [
                {file: formatter.format_size(size)}
                for file, size in sorted(file_sizes.items(), key=lambda item: item[1], reverse=True)[:5]
            ]
            raise OutputsExceedLimitError(
                'Task output size {total_size} exceeds limit {max_total_size}, largest outputs are\n{large_files}'.format(
                    total_size=total_files_size, max_total_size=max_size, large_files=top_5
                )
            )

    @property
    def output_node_limit(self):
        return limits.get_max_result_size()

    @property
    def ok(self):
        return self._ok

    def dec(self):
        if self._keep:
            return
        with self._lock:
            self._refcount -= 1
            if self._refcount < 0:
                raise Exception('Negative refcount for ' + self.path)
        if self._refcount == 0 and not self._keep:
            self.delete()
            # self._defer(worker_threads.Action(self.delete, worker_threads.ResInfo(io=1)))

    def inc(self):
        if self._keep:
            return
        with self._lock:
            if self._refcount == 0 and self._created:
                raise Exception('Cannot recreate build_root ' + self.path)
            self._refcount += 1

    def validate_dir_outputs(self):
        dir_outputs_archive_map = self.dir_outputs_archive_map
        for dir_output in self._dir_outputs:
            for root, dirs, files in os.walk(dir_output):
                for d in dirs:
                    joined_dir = os.path.join(os.path.join(root, d))
                    if not os.listdir(joined_dir):
                        logger.warning("{} directory is empty".format(joined_dir))
            if dir_outputs_archive_map[dir_output] is None:
                raise ValueError("No declared archive for {} dir_output, outputs: {}".format(dir_output, self._outputs))

    def extract_dir_outputs(self):
        dir_outputs_archive_map = self.dir_outputs_archive_map
        for dir_output, dir_output_archive_path in dir_outputs_archive_map.items():
            if (
                not os.path.exists(dir_output)
                and dir_output_archive_path is not None
                and exts.archive.is_archive_type(dir_output_archive_path)
            ):
                archive.extract_from_tar(dir_output_archive_path, dir_output)

    def propagate_dir_outputs(self):
        for real_dir_output in self.dir_outputs_files:
            self._outputs.append(real_dir_output)
            self._original_outputs.append(real_dir_output.replace(self.path, "$(BUILD_ROOT)"))

    @exts.func.lazy_property
    def dir_outputs_archive_map(self):
        dir_outputs_archive_map = {}
        for dir_out in self._dir_outputs:
            for out in self._outputs:
                if out.startswith(dir_out + "."):
                    dir_outputs_archive_map[dir_out] = out
                    break
            else:
                dir_outputs_archive_map[dir_out] = None
        return dir_outputs_archive_map

    def write_hashes(self, hashes):
        if not self._compute_hash:
            return
        raw_path = '$(BUILD_ROOT)/' + CONTENT_HASH_FILE_NAME
        path = os.path.join(self.path, CONTENT_HASH_FILE_NAME)
        self.add_output(raw_path)
        runner_fs.write_into_file(path, hashes)

    def _hash_file(self):
        path = os.path.join(self.path, CONTENT_HASH_FILE_NAME)
        if os.path.exists(path):
            return path
        return None

    def read_hashes(self, force_recalc=False):
        """
        Returns sum of outputs hashes
        """
        if not force_recalc:
            path = self._hash_file()
            if path:
                return runner_fs.read_from_file(path)

        if not self._compute_hash:
            return "rndhash-{}".format(time.time())

        hashes = []
        for output in self._outputs:
            if os.path.exists(output):
                hashes.append(hashing.git_like_hash(output))
            else:
                return None
        return hashing.sum_hashes(hashes)

    def create(self):
        self._created = True
        return fs.create_dirs(self.path)

    def delete(self):
        fs.remove_tree_with_perm_update(self.path)

    def create_empty_dirs(self, where, empty_meta_path):
        with open(empty_meta_path) as afile:
            empty_dirs = json.load(afile)
            for ed in empty_dirs:
                empty_path = os.path.join(where, ed)
                try:
                    fs.create_dirs(six.ensure_str(empty_path, "utf-8", errors="ignore"))
                except Exception:
                    logger.exception(
                        "Failed to create empty directory %s, defaultencoding: %s, LC_ALL: %s, LANG: %s",
                        empty_path,
                        sys.getdefaultencoding(),
                        os.environ.get("LC_ALL"),
                        os.environ.get("LANG"),
                    )

    def steal(self, into, dec=True):
        if not self._ok:
            return False

        try:
            for x in self._outputs:
                if x.endswith(EMPTY_DIR_OUTPUTS_META):
                    # We need to restore empty dirs file structure for dir outputs
                    self.create_empty_dirs(into, x)
                if x.endswith(CONTENT_HASH_FILE_NAME):
                    # We don't need to copy file with hash info
                    continue
                try:
                    runner_fs.make_hardlink(
                        x, os.path.join(into, os.path.relpath(x, self.path)), retries=1, prepare=True
                    )
                except (
                    OSError
                ) as e:  # check if this output was added to outputs with dir_outputs, it may be removed by another process while we collecting it
                    if x in self._initial_outputs or errno.ENOENT != e.errno:
                        raise
                    else:
                        logger.warning(
                            "Stealing file %s does not exist. This file was added with --dir-outputs. Probably, it was deleted by another process",
                            x,
                        )
        except OSError as e:
            logger.error('Cannot steal from %s into %s: %s', self.path, into, str(e))

            return False
        finally:
            if dec:
                self.dec()

        return True


class BuildRootSet(object):
    def __init__(self, path, keep, incremental_cleanup, defer, limit_output_size, validate_content=False):
        self._store_path = path
        self._lock = threading.Lock()
        self._id = 0
        self._keep = keep
        self._incremental_cleanup = incremental_cleanup
        self._defer = defer
        self._build_root = self._make_root()
        self._limit_output_size = limit_output_size
        self._validate_content = validate_content

        self._roots = []
        self._stamp_name = os.path.join(self._build_root, STAMP_FILE)
        self._flock = filelock.FileLock(self._stamp_name)

        if not self._keep:
            self._sieve()

    def __enter__(self):
        if not self._keep:
            self._flock.acquire()
        return self

    def __exit__(self, *exc_details):
        if not self._keep:
            self._flock.release()
        return False

    def _make_root(self):
        while True:
            cur_name = os.path.join(self._store_path, uniq_id.gen4())
            try:
                os.mkdir(cur_name)
                return cur_name
            except OSError:
                pass

    def _sieve(self):
        for x in os.listdir(self._store_path):
            cur_name = os.path.join(self._store_path, x)
            stamp_fname = os.path.join(cur_name, STAMP_FILE)
            if stamp_fname != self._stamp_name and os.path.exists(stamp_fname):
                self._defer(worker_threads.Action(lambda name=x: self._clenup(name), worker_threads.ResInfo(io=1)))

    def _clenup(self, build_set):
        cur_name = os.path.join(self._store_path, build_set)
        stamp_fname = os.path.join(cur_name, STAMP_FILE)
        if os.path.exists(stamp_fname):
            try:
                lock = None
                lock = filelock.FileLock(stamp_fname)
                if lock.acquire(blocking=False):
                    logger.debug("Removing %s", cur_name)
                    fs.remove_tree_safe(cur_name)
                else:
                    logger.debug("Cannot get lock %s for %s ", stamp_fname, cur_name)
            except Exception as e:
                logger.debug("Cannot get lock %s for %s, exception=%s", stamp_fname, cur_name, str(e))
            finally:
                if lock:
                    lock.release()

    def _gen_id(self):
        with self._lock:
            self._id += 1
            return '{0:06x}'.format(self._id)

    def new(self, outputs, refcount, dir_outputs=None, compute_hash=True):
        root = BuildRoot(
            os.path.join(self._build_root, self._gen_id()),
            self._keep or not self._incremental_cleanup,
            outputs,
            self._limit_output_size,
            refcount,
            dir_outputs,
            validate_content=self._validate_content,
            compute_hash=compute_hash,
        )
        self._roots.append(root)

        return root

    def finalize(self):
        if not self._keep:
            self._flock.release()

    def stats(self):
        try:
            dirs_left = len(os.listdir(self._build_root)) - 1  # Account for STAMP file in self._build_root
            logger.debug('Build root %s created=%s left=%s', self._build_root, len(self._roots), dirs_left)
        except Exception:
            pass

    def cleanup(self):
        if self._keep:
            return

        self.stats()
        acquired = None
        try:
            try:
                acquired = self._flock.acquire(blocking=False)
                if acquired:
                    logger.debug('Removing root %s', self._build_root)
                    try:
                        subprocess.call(["rm", "-rf", fs.fix_path_encoding(self._build_root)])
                    except OSError:
                        fs.remove_tree_safe(self._build_root)
                else:
                    logger.debug('Not removing root %s, lock was not acquired', self._build_root)
            finally:
                if acquired:
                    self._flock.release()
        except Exception as e:
            logger.debug("Cannot get lock to cleanup build root %s, exception=%s", self._build_root, str(e))
