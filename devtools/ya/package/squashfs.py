import os

import package
import package.packager
import package.process

SQUASH_COMMAND = 'mksquashfs'


def create_squashfs_package(result_dir, package_dir, package_context):
    filename = package_context.resolve_filename(extra={"package_ext": "squashfs"})
    squashfs_path = os.path.join(result_dir, filename)
    args = [package_dir, squashfs_path]
    package.process.run_process(SQUASH_COMMAND, args)
    return squashfs_path
