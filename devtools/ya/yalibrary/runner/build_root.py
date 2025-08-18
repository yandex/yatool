import errno
import json
import logging
import os
import pathlib
import stat
import subprocess
import sys
import threading
import time

import six

import exts.func
import exts.windows

from exts import archive
from exts import uniq_id
from exts import filelock
from exts import fs

from yalibrary import formatter
from yalibrary.runner import fs as runner_fs
from yalibrary.runner import worker_threads

from devtools.libs.limits.python import limits
import devtools.libs.acdigest.python as acdigest

STAMP_FILE = 'STAMP'
CONTENT_HASH_FILE_NAME = '.content_hash.md5'  # deprecated
OUTPUT_DIGESTS_FILE_NAME = '.output_digests.json'
EMPTY_DIR_OUTPUTS_META = '.empty_dir_outputs.json'

logger = logging.getLogger(__name__)


class BuildRootError(Exception):
    pass


class BuildRootIntegrityError(BuildRootError):
    pass


class OutputsExceedLimitError(BuildRootError):
    pass


class OutputDigests(object):
    def __init__(self, root_path, version, file_digests, outputs_uid):
        self._root_path = root_path
        self.version = version
        self.file_digests = file_digests
        self.outputs_uid = outputs_uid

    def to_json(self):
        return {
            "version": self.version,
            "file_digests": {
                os.path.relpath(fn, self._root_path): acdigest.FileDigest.to_json(v)
                for fn, v in self.file_digests.items()
            },
            "outputs_uid": self.outputs_uid,
        }

    @staticmethod
    def from_json(root_path, data):
        version = data["version"]
        file_digests = {
            os.path.join(root_path, fn): acdigest.FileDigest.from_json(v) for fn, v in data["file_digests"].items()
        }
        outputs_uid = six.ensure_str(data["outputs_uid"])
        return OutputDigests(root_path, version, file_digests, outputs_uid)

    @staticmethod
    def from_outputs_uid(outputs_uid):
        return OutputDigests(None, acdigest.digest_git_like_version, {}, outputs_uid)


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
            if not x.endswith((CONTENT_HASH_FILE_NAME, EMPTY_DIR_OUTPUTS_META, OUTPUT_DIGESTS_FILE_NAME)):
                yield x

    @property
    def output(self):
        for x in self._outputs:
            yield x

    def add_output(self, new_output):
        self._outputs.append(new_output.replace('$(BUILD_ROOT)', self.path))
        self._original_outputs.append(new_output)

    def validate(self):
        outset = None
        brpath = None

        for x in self.output:
            try:
                lst = os.lstat(x)
            except FileNotFoundError as e:
                raise BuildRootIntegrityError(f'Cannot find {x} in build root') from e

            if stat.S_ISLNK(lst.st_mode):
                brpath = brpath or pathlib.Path(self.path)
                xpath = pathlib.Path(x)
                lpath = xpath.readlink()

                if lpath.is_absolute():
                    # We can't cache such symlink output in the dist and local caches
                    # because the caching subsystem does not control the life cycle of the linkpath file.
                    # Even if symlink points to a file inside the buildroot,
                    # since the buildroot is an ephemeral entity that can be disposed of at any time.
                    raise BuildRootIntegrityError(
                        f'{x} (linkpath: {lpath}) is a symlink with an absolute path, which is why it cannot be cached correctly'
                    )
                elif not xpath.resolve().is_relative_to(brpath.resolve()):
                    raise BuildRootIntegrityError(
                        f'{x} (real: {xpath.resolve()} linkpath: {lpath}) is a symlink output which points to a file outside the buildroot {self.path} (real: {brpath.resolve()})'
                    )
                else:
                    # lazy init - realpath is quite an expensive operation
                    outset = outset or set(os.path.realpath(o) for o in self.output if o != x)

                    if str(xpath.resolve()) in outset:
                        # This particular symlink is valid because it's
                        # relative and pointing to a file that is part of the node's output
                        pass
                    else:
                        raise BuildRootIntegrityError(
                            f'{x} (real: {xpath.resolve()} linkpath: {lpath}) is a symlink output pointing to a file that is not part of the output: {outset}'
                        )
            elif not stat.S_ISREG(lst.st_mode):
                raise BuildRootIntegrityError(f'Invalid file type {x} ({lst.st_mode:o}) in build root')

        if self._validate_content:
            if self._compute_hash and os.path.exists(self._output_digests_file()):
                cached_hash = self.read_output_digests()
                if cached_hash.version == acdigest.digest_current_version:
                    immediate_hash = self.read_output_digests(force_recalc=True)
                    if cached_hash.outputs_uid != immediate_hash.outputs_uid:
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

    def _output_digests_file(self):
        return os.path.join(self.path, OUTPUT_DIGESTS_FILE_NAME)

    def read_output_digests(self, force_recalc=False, write_if_absent=False):
        digests_file = self._output_digests_file()
        if not force_recalc and os.path.exists(digests_file):
            try:
                with open(digests_file) as f:
                    data = json.load(f)
                return OutputDigests.from_json(self.path, data)
            except (KeyError, json.JSONDecodeError):
                logger.exception("Cannot load digest file %s", digests_file)

        if not self._compute_hash:
            return OutputDigests.from_outputs_uid("rndhash-{}".format(time.time()))

        digests = {}
        hashes = []
        for output in self._outputs:
            if os.path.exists(output):
                d = acdigest.get_file_digest(output)
                digests[output] = d
                hashes.append(d.content_digest)
            else:
                return None
        outputs_uid = acdigest.combine_hashes(hashes)

        output_digests = OutputDigests(self.path, acdigest.digest_current_version, digests, outputs_uid)

        if write_if_absent:
            self.add_output("$(BUILD_ROOT)/" + OUTPUT_DIGESTS_FILE_NAME)
            with open(digests_file, "w") as f:
                json.dump(output_digests.to_json(), f)

        return output_digests

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
                if x.endswith(CONTENT_HASH_FILE_NAME) or x.endswith(OUTPUT_DIGESTS_FILE_NAME):
                    # We don't need to copy file with hash info
                    continue
                try:
                    runner_fs.make_hardlink(
                        x, os.path.join(into, fs.fast_relpath(x, self.path)), retries=1, prepare=True
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
    def __init__(self, path, keep, defer, limit_output_size, validate_content=False):
        self._store_path = path
        self._lock = threading.Lock()
        self._id = 0
        self._keep = keep
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
            self._keep,
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

    def _rm_rf_root(self):
        # XXX See DEVTOOLSSUPPORT-41205
        try:
            import signal
            from library.python.prctl import prctl

            def pdeath():
                prctl.set_pdeathsig(signal.SIGTERM)

        except ImportError:
            pdeath = None

        subprocess.call(
            ["rm", "-rf", fs.fix_path_encoding(self._build_root)],
            preexec_fn=pdeath,
        )

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
                    if exts.windows.on_win():
                        fs.remove_tree_safe(self._build_root)
                    else:
                        try:
                            self._rm_rf_root()
                        except OSError:
                            fs.remove_tree_safe(self._build_root)
                else:
                    logger.debug('Not removing root %s, lock was not acquired', self._build_root)
            finally:
                if acquired:
                    self._flock.release()
        except Exception as e:
            logger.debug("Cannot get lock to cleanup build root %s, exception=%s", self._build_root, str(e))
