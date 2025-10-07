import os
import shutil

import library.python.compress

import multiprocessing
import exts.tmp
import exts.archive


def create_tarball_package(
    result_dir,
    package_dir,
    package_filename,
    compress=True,
    codec=None,
    threads=None,
    compression_filter=None,
    compression_level=None,
    stable_archive=False,
):
    archive_file = package_filename

    is_compression_ext_passed_via_filename = archive_file.endswith(('.zst', '.gz'))
    if compress and not codec and not is_compression_ext_passed_via_filename:
        if compression_filter == exts.archive.ZSTD:
            archive_file += '.zst'
        else:
            archive_file += '.gz'

    if codec:
        threads = threads or multiprocessing.cpu_count()

    with exts.tmp.temp_dir() as temp_dir:
        tar_archive = os.path.join(temp_dir, archive_file)
        if compress and not codec:
            compression_filter = compression_filter or exts.archive.GZIP
            if compression_level is None:
                compression_level = exts.archive.get_compression_level(
                    compression_filter, exts.archive.Compression.Default
                )
        else:
            compression_filter, compression_level = None, None
        exts.archive.create_tar(
            package_dir,
            tar_archive,
            compression_filter,
            compression_level,
            fixed_mtime=0 if stable_archive else None,
        )

        if codec:
            uc_archive_path = archive_file + ".uc." + codec
            library.python.compress.compress(tar_archive, uc_archive_path, codec, threads=threads)
            archive_file = uc_archive_path
        else:
            archive_file = tar_archive

        result_path = os.path.join(result_dir, os.path.basename(archive_file))
        shutil.move(archive_file, result_path)
        return result_path


def get_codecs_list():
    return library.python.compress.list_all_codecs()
