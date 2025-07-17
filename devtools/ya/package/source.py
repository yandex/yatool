import os
import stat
import codecs
import random
import shutil
import fnmatch
import logging
import path as pathlib

import exts.archive
import exts.path2
import exts.fs
import exts.tmp

from pathlib2 import PurePath

import package
import package.fs_util

import library.python.compress


ATTRIBUTES = ['owner', 'group', 'mode']

TEST_DATA_DIRECTORIES = ['data']

GLOB_PREFIX = "glob:"

logger = logging.getLogger(__name__)


class YaPackageSourceException(Exception):
    mute = True


def filter_files(path, patterns):
    """
    glob patterns discover files inside the symlink to the directory, but common patterns do not
    :param path: path to the directory to be discovered for items
    :param patterns: list of glob and common patterns
    :return: directories, files, links with matching pattern
    """
    common_patterns = [pattern for pattern in patterns if not pattern.startswith(GLOB_PREFIX)]
    paths_for_cp = list(_walk_path(pathlib.Path(path))) if common_patterns else []
    glob_patterns = [pattern for pattern in patterns if pattern.startswith(GLOB_PREFIX)]
    paths_for_gp = list(pathlib.Path(path).walk()) if glob_patterns else []
    return fiter_paths(path, paths_for_cp, common_patterns) + fiter_paths(path, paths_for_gp, glob_patterns)


def _walk_path(path):
    for child in path.iterdir():
        yield child
        if child.is_dir() and not child.islink():
            for item in _walk_path(child):
                yield item


def fiter_paths(root, paths, patterns):
    result = []
    for pattern in patterns:
        if pattern.startswith(GLOB_PREFIX):
            for p_path in [p for p in paths if PurePath(p).match(os.path.join(root, pattern[len(GLOB_PREFIX) :]))]:
                result.append((p_path[len(root) + 1 :], pattern))
        else:
            for p_path in [p for p in paths if fnmatch.fnmatch(p, os.path.join(root, pattern))]:
                result.append((p_path[len(root) + 1 :], pattern))
    return result


def _is_dir_path(path):
    if path.endswith(os.sep):
        return True
    if os.altsep and path.endswith(os.altsep):
        return True
    return False


def _strip_dir_sep(path):
    if path.endswith(os.sep):
        return path[:-1]
    if os.altsep and path.endswith(os.altsep):
        return path[:-1]
    return path


class Source(object):
    def __init__(
        self,
        arcadia_root,
        builds,
        data,
        result_dir,
        package_root,
        global_temp,
        formaters,
        files_comparator,
        opts,
    ):
        self.arcadia_root = arcadia_root
        self.builds = builds
        self.data = data
        self.result_dir = result_dir
        self.package_root = package_root
        self.global_temp = global_temp
        self._destination_paths = []
        self._formaters = formaters or {}
        self._files_comparator = files_comparator or FilesComparator()
        self.opts = opts

    def source_path(self, skip_prefix=False):
        path = self.data['source']['path'].format(**self._formaters)
        if skip_prefix:
            return os.path.join(*exts.path2.path_explode(path)[1:])
        else:
            return path

    def destination_path(self):
        return self.data['destination']['path'][1:].format(**self._formaters)

    def keep_symlinks(self):
        return False

    def prepare(self, apply_attributes=False):
        if 'path' in self.data['destination'] and not self.data['destination']['path'].startswith('/'):
            raise YaPackageSourceException(
                'Destination path \'{}\' should start with \'/\''.format(self.data['destination']['path'])
            )

        if self.data['destination'].get('temp', False):
            if 'path' in self.data['destination']:
                self.data['destination']['path'] = '/' + self.global_temp + self.data['destination']['path']
            else:
                self.data['destination']['path'] = '/' + self.global_temp + '/'

        archive_directory = None
        if 'archive' in self.data['destination']:
            archive_directory = str(random.random())

            if 'path' in self.data['destination']:
                self.data['destination']['path'] = '/' + archive_directory + self.data['destination']['path']
            else:
                self.data['destination']['path'] = '/' + archive_directory + '/'

            package.display.emit_message(
                '{}{}: [[imp]]{}[[rst]]'.format(
                    package.PADDING, 'Temporary directory for archive created', archive_directory
                )
            )

        self._prepare()

        if apply_attributes:
            self._apply_attributes()

        if archive_directory:
            archive_path = os.path.join(self.result_dir, self.data['destination']['archive'][1:])
            self.create_directory_if_necessary(os.path.dirname(archive_path))

            archive_directory = os.path.join(self.result_dir, archive_directory)
            archive_name = os.path.basename(archive_path)

            if '.uc.' in archive_name:
                tmp = archive_path + '.tmp'
                codec = archive_name.split('.')[-1]

                exts.archive.create_tar(archive_directory, tmp, fixed_mtime=None)
                library.python.compress.compress(tmp, archive_path, codec, threads=self.opts.build_threads)
                os.unlink(tmp)
            else:
                if archive_path.endswith('.gz'):
                    compression_filter, compression_level = exts.archive.GZIP, exts.archive.Compression.Default
                else:
                    compression_filter, compression_level = None, None

                exts.archive.create_tar(
                    archive_directory,
                    archive_path,
                    compression_filter,
                    compression_level,
                    fixed_mtime=None,
                )

            package.display.emit_message(
                '{}{}: [[imp]]{}[[rst]] [[good]]{}[[rst]] [[imp]]{}[[rst]]'.format(
                    package.PADDING,
                    'Archive created',
                    os.path.basename(archive_directory),
                    '=>',
                    self.data['destination']['archive'],
                )
            )

            package.fs_util.cleanup(archive_directory, force=True)
            package.display.emit_message(
                '{}{}: [[imp]]{}[[rst]]'.format(
                    package.PADDING, 'Temporary directory for archive removed', os.path.basename(archive_directory)
                )
            )

    def _prepare(self):
        raise NotImplementedError

    def create_directory_if_necessary(self, path):
        if not os.path.exists(path):
            exts.fs.create_dirs(path)
            package.display.emit_message(
                '{}{}: [[imp]]{}[[rst]]'.format(package.PADDING, 'Directory created', path[len(self.result_dir) + 1 :])
            )
        elif not os.path.isdir(path):
            raise YaPackageSourceException('Directory can\'t be created, file {} already exists'.format(path))

    def _copy_link(self, source, destination):
        linkto = os.readlink(source)
        os.symlink(linkto, destination)

    def copy_file(self, source, destination):
        package.display.emit_message(
            '{}{}: [[imp]]{}[[rst]] [[good]]{}[[rst]] [[imp]]{}[[rst]]'.format(
                package.PADDING, 'Copying file', source, '=>', destination[len(self.result_dir) + 1 :]
            )
        )

        if self.keep_symlinks() and os.path.islink(source):
            self._copy_link(source, destination)
        elif self._use_hardlinks():
            exts.fs.hardlink(source, destination)
        else:
            shutil.copy(source, destination)

    def copy_directory(self, source, destination):
        package.display.emit_message(
            '{}{}: [[imp]]{}[[rst]] [[good]]{}[[rst]] [[imp]]{}[[rst]]'.format(
                package.PADDING, 'Copying directory', source, '=>', destination[len(self.result_dir) + 1 :]
            )
        )

        if self.keep_symlinks() and os.path.islink(source):
            self._copy_link(source, destination)
        elif self._use_hardlinks():
            exts.fs.hardlink_tree(source, destination)
        else:
            package.fs_util.copy_tree(source, destination, symlinks=self.keep_symlinks(), dirs_exist_ok=True)

    def copy(self, source, destination):
        if _is_dir_path(source):
            raise YaPackageSourceException(
                "Source path {} is not a certain file or directory(ends with '/')".format(source)
            )
        if not os.path.exists(source):
            raise YaPackageSourceException('No such file or directory {}'.format(source))

        if 'files' in self.data['source']:
            if not isinstance(self.data['source']['files'], list):
                raise YaPackageSourceException('Files parameter should be list')

            if not _is_dir_path(destination):
                raise YaPackageSourceException(
                    "Files should be used only with a directory destination(ends with '/') {}".format(destination)
                )

            if not os.path.isdir(source):
                # TODO: HACK for backward compatibility
                destination_path = os.path.join(destination, os.path.basename(source))
                self.create_directory_if_necessary(os.path.dirname(destination_path))
                self.copy_file(source, destination_path)
                self._destination_paths.append(destination_path)
                return

                # raise YaPackageSourceException(
                #     'Files should be used only with a directory source path {}'.format(source)
                # )

            used_patterns = set()
            expanded_files = [files.format(**self._formaters) for files in self.data['source']['files']]
            for path, pattern in filter_files(source, expanded_files):
                used_patterns.add(pattern)
                source_path = os.path.join(source, path)
                destination_path = os.path.join(destination, path)

                if os.path.exists(destination_path) and not os.path.isdir(destination_path):
                    if self._files_comparator.is_equal(source_path, destination_path):
                        package.display.emit_message(
                            '{}[[warn]]Skip file overwriting: {} (files are equal)'.format(
                                package.PADDING, destination_path[len(self.result_dir) + 1 :]
                            )
                        )
                        continue
                    elif self.opts.overwrite_read_only_files:
                        package.display.emit_message(
                            '{}[[warn]]File overwritten: {} (write permissions are added)'.format(
                                package.PADDING, destination_path[len(self.result_dir) + 1 :]
                            )
                        )
                        os.chmod(
                            destination_path, os.stat(destination).st_mode | stat.S_IWUSR | stat.S_IWGRP | stat.S_IWOTH
                        )
                    else:
                        raise YaPackageSourceException(
                            "{} file is already present in the package. {} (checksum:{}) -> {} (checksum:{})".format(
                                path,
                                source_path,
                                self._files_comparator.get_checksum(source_path),
                                destination_path,
                                self._files_comparator.get_checksum(destination_path),
                            )
                        )

                self.create_directory_if_necessary(os.path.dirname(destination_path))

                if os.path.isdir(source_path):
                    if os.path.islink(source_path) and self.keep_symlinks():
                        self._copy_link(source_path, destination_path)
                    else:
                        self.create_directory_if_necessary(destination_path)
                    self._destination_paths.append(destination_path)
                else:
                    self.copy_file(source_path, destination_path)
                    self._destination_paths.append(destination_path)

            unused_patterns = [p for p in expanded_files if p not in used_patterns]
            if unused_patterns:
                msg = "Misconfiguration: the following 'files' patterns resulted empty filesets: {}".format(
                    ", ".join(unused_patterns)
                )
                if self.opts.verify_patterns_usage:
                    raise YaPackageSourceException(msg)
                else:
                    logger.warning(msg)
        else:
            if _is_dir_path(destination):
                destination = os.path.join(destination, os.path.basename(source))

            if os.path.exists(destination) and not (os.path.isdir(source) and os.path.isdir(destination)):
                raise YaPackageSourceException('File or directory {} already exists'.format(destination))

            self.create_directory_if_necessary(os.path.dirname(destination))

            if os.path.isdir(source):
                self.copy_directory(source, destination)
                self._destination_paths.append(destination)
            else:
                self.copy_file(source, destination)
                self._destination_paths.append(destination)

        if 'exclude' in self.data['source']:
            used_patterns = set()
            formatted_excludes = [exclude.format(**self._formaters) for exclude in self.data['source']['exclude']]
            for path, pattern in filter_files(_strip_dir_sep(destination), formatted_excludes):
                used_patterns.add(pattern)
                destination_path = os.path.join(destination, path)

                if os.path.exists(destination_path):
                    logger.debug("Removing '%s' by pattern '%s'", destination_path, pattern)
                    package.display.emit_message(
                        '{}{}: [[imp]]{}[[rst]]'.format(
                            package.PADDING,
                            'Excluded',
                            destination_path[len(self.result_dir) + 1 :],
                        )
                    )
                    exts.fs.ensure_removed(destination_path)
                    if destination_path in self._destination_paths:
                        self._destination_paths.remove(destination_path)

            unused_patterns = [p for p in formatted_excludes if p not in used_patterns]
            if unused_patterns:
                raise YaPackageSourceException(
                    "Misconfiguration: the following 'exclude' patterns resulted empty filesets: {}".format(
                        ", ".join(unused_patterns)
                    )
                )

    def create_symlink(self, target, destination):
        self.create_directory_if_necessary(os.path.dirname(destination))
        exts.fs.symlink(target, destination)
        package.display.emit_message(
            '{}{}: [[imp]]{}[[rst]] [[good]]{}[[rst]] [[imp]]{}[[rst]]'.format(
                package.PADDING, 'Symlink created', destination[len(self.result_dir) + 1 :], '->', target
            )
        )

    def get_attributes(self):
        attributes = {attribute: {'value': None, 'recursive': False} for attribute in ATTRIBUTES}

        if 'attributes' in self.data['destination']:
            for attribute_name in ATTRIBUTES:
                if attribute_name in self.data['destination']['attributes']:
                    attribute = self.data['destination']['attributes'][attribute_name]

                    if 'value' in attribute:
                        attributes[attribute_name]['value'] = attribute['value']

                    if 'recursive' in attribute:
                        attributes[attribute_name]['recursive'] = attribute['recursive']

        return attributes

    @property
    def destination_paths(self):
        return self._destination_paths

    def _apply_attributes(self):
        def _apply_chmod(p, mode):
            try:
                mode = int(str(mode), 8)
            except (ValueError, TypeError):
                pass

            try:
                return p.chmod(mode)
            except ValueError as e:
                # XXX: Check YA-762 for more details
                logger.warning("Exception caught from chmod: `%s`", e)
                new_mode = "a{}".format(mode)
                logger.info(
                    "Try to modificate permission mask with adding `who` modificator: `%s` -> `%s`", mode, new_mode
                )

                return p.chmod(new_mode)

        actions = {
            "mode": _apply_chmod,
            # XXX: for now set only mode attrs
            # "owner": lambda p, owner: p.chown(owner, -1),
            # "group": lambda p, group: p.chown(-1, group),
        }

        def get_path(p, recursive):
            p = pathlib.Path(p)
            if recursive and p.is_dir():
                return p.walk()
            return [p]

        attributes = self.get_attributes()
        for attr in attributes:
            value = attributes[attr]["value"]
            if value:
                for dest_path in self.destination_paths:
                    for p in get_path(dest_path, attributes[attr]["recursive"]):
                        if attr in actions:
                            actions[attr](p, value)

    def _use_hardlinks(self):
        return self.opts and self.opts.hardlink_package_outputs

    def __str__(self):
        return str(self.data)


class BuildOutputSource(Source):
    def _prepare(self):
        try:
            output_root = self.builds[self.data['source'].get('build_key', None)]["output_root"]
        except KeyError:
            raise YaPackageSourceException(
                "Cannot choose BUILD_OUTPUT for '{}', set 'build_key' explicitly to one of [{}]".format(
                    ["{}={}".format(k, v) for k, v in sorted(self.data['source'].items())],
                    ", ".join(sorted(self.builds.keys())),
                )
            )

        source = os.path.join(output_root, self.source_path())
        destination = os.path.join(self.result_dir, self.destination_path())

        should_be_untared = self.data['source'].get('untar')
        if should_be_untared:
            if not exts.archive.check_archive(source):
                raise YaPackageSourceException("{} does not seem to be a correct archive to be untared".format(source))

            with exts.tmp.temp_dir() as tmp_dir:
                extract_dir = os.path.join(tmp_dir, "extracted")
                exts.fs.ensure_dir(extract_dir)
                exts.archive.extract_from_tar(source, extract_dir)
                if 'files' not in self.data['source']:
                    self.data['source']['files'] = ["*"]
                source = extract_dir

                # need a separate self.copy before temp dir is deleted
                self.copy(source, destination)
        else:
            self.copy(source, destination)


class RelativeSource(Source):
    def _prepare(self):
        source = os.path.join(self.package_root, self.source_path())
        destination = os.path.join(self.result_dir, self.destination_path())
        self.copy(source, destination)

    def keep_symlinks(self):
        return self.data['source'].get('symlinks', False)


class DirectorySource(Source):
    def _prepare(self):
        destination = os.path.join(self.result_dir, self.destination_path())
        self.create_directory_if_necessary(destination)


class ArcadiaSource(Source):
    def _prepare(self):
        source = os.path.join(self.arcadia_root, self.source_path())
        destination = os.path.join(self.result_dir, self.destination_path())
        if os.path.abspath(source) == os.path.abspath(self.arcadia_root):
            logger.warning(
                "['source']['path'] in your ARCADIA data equals to arcadia root. \
It will cause to scan full arcadia repo, so it will be very slow. \
Please consider defining your data paths more explicitly"
            )
        self.copy(source, destination)

    def _use_hardlinks(self):
        # Avoid "[Errno 18] Cross-device link" error
        return False


class TempSource(Source):
    def _prepare(self):
        source = os.path.join(self.result_dir, self.global_temp, self.source_path())
        destination = os.path.join(self.result_dir, self.destination_path())
        self.copy(source, destination)


class SymlinkSource(Source):
    def _prepare(self):
        if 'target' not in self.data['destination']:
            raise YaPackageSourceException('Symlink should have target in destination section')

        target = self.data['destination']['target'].format(**self._formaters)
        destination = os.path.join(self.result_dir, self.destination_path())
        self.create_symlink(target, destination)


class InlineFileSource(Source):
    def _prepare(self):
        destination = os.path.join(self.result_dir, self.destination_path())
        content = self.data['source']['content'].format(**self._formaters)
        with exts.tmp.temp_file() as source:
            with codecs.open(source, "w", encoding="utf-8") as f:
                f.write(content)
            self.copy(source, destination)


class FilesComparator(object):
    def __init__(self):
        self._cache = {}

    def is_equal(self, file1, file2):
        return self.get_checksum(file1) == self.get_checksum(file2)

    def get_checksum(self, filename):
        filename = os.path.realpath(filename)
        return self._cache.setdefault(filename, exts.hashing.md5_path(filename))
