import os
import shutil
import six

import package.process

NPM_BINARY = "npm"


def create_npm_package(content_dir, result_dir, publish_to, package_context):
    set_package_version(content_dir, package_context.version)
    tarball_name = mv_to = pack(content_dir)

    if package_context.should_use_package_filename:
        mv_to = package_context.resolve_filename(
            extra={"package_ext": "tgz", "package_name": extract_package_name(tarball_name)}
        )

    tarball_path = move_package_to_result_dir(content_dir, result_dir, tarball_name, mv_to)

    if publish_to:
        publish(publish_to, tarball_path)

    return tarball_path


def set_package_version(content_dir, package_version):
    version_args = ["version", package_version]
    package.process.run_process(NPM_BINARY, version_args, cwd=content_dir)


def pack(content_dir):
    pack_args = ["pack"]
    pack_output, _ = package.process.run_process(NPM_BINARY, pack_args, cwd=content_dir)
    return get_tarball_name(pack_output)


def get_tarball_name(pack_output):
    pack_lines = pack_output.splitlines()
    assert pack_lines, "npm-pack output is empty"
    return six.ensure_str(pack_lines[-1])


def extract_package_name(tarball_name):
    """
    Extract package name from tarball name
    Npm docs says that they name it like that <name>-<version>.tgz
    https://docs.npmjs.com/cli/v8/commands/npm-pack#description
    """
    assert '-' in tarball_name
    return tarball_name.rsplit('-', 1)[0]


def move_package_to_result_dir(content_dir, result_dir, tarball_name, mv_to):
    src = os.path.join(content_dir, tarball_name)
    dst = os.path.join(result_dir, mv_to)
    shutil.move(src, dst)
    return dst


def publish(publish_to, tarball_path):
    for npm_registry in publish_to:
        publish_args = ["publish", tarball_path, "--registry", npm_registry]
        package.process.run_process(NPM_BINARY, publish_args)
