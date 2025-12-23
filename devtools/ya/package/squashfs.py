import os

import package
import package.packager
import package.process

SQUASH_COMMAND = "mksquashfs"

POSSIBLE_EXTENSIONS = {"xz", "zstd", "lzo", "lz4", "lzma", "gzip"}


def create_squashfs_package(result_dir, package_dir, package_context, compression_filter=None):
    filename = package_context.resolve_filename(extra={"package_ext": "squashfs"})
    squashfs_path = os.path.join(result_dir, filename)
    args = [package_dir, squashfs_path]
    if compression_filter in POSSIBLE_EXTENSIONS:
        args.extend(["-comp", compression_filter])
    package.process.run_process(SQUASH_COMMAND, args)
    return squashfs_path
