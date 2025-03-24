import collections
import logging
import os
import re

import requests

import exts.archive
import exts.fs
import exts.tmp
import exts.yjson as json

import package.source
import package.fs_util
import package.process

from devtools.ya.package import const

logger = logging.getLogger(__name__)
PackageInfo = collections.namedtuple('PackageInfo', ['image_tag', 'digest'])

DOCKER_BIN_PATHS = [
    "/usr/bin/docker",
    "/usr/local/bin/docker",
    "/opt/homebrew/bin/docker",
]


def get_docker_binary():
    for p in DOCKER_BIN_PATHS:
        if os.path.exists(p):
            return p
    raise Exception("Docker binary not found by {}".format(DOCKER_BIN_PATHS))


def get_image_name(registry, repository, image_name, package_name, package_version):
    docker_image_name = image_name or package_name
    prefix = "/".join([_f for _f in [registry, repository] if _f])
    if prefix:
        return "{}/{}:{}".format(prefix, docker_image_name, package_version)
    else:
        return "{}:{}".format(docker_image_name, package_version)


def extract_digest(data):
    m = re.search(r"digest:\s+(\w+:\w+)", data)
    if m:
        return m.group(1)


def create_package(
    registry,
    repository,
    image_name,
    package_context,
    docker_root,
    result_dir,
    save_docker_image,
    push,
    nanny_release,
    network,
    build_args,
    docker_no_cache,
    docker_use_remote_cache,
    docker_remote_image_version,
    platform,
    add_host,
    target,
    docker_secret,
    labels=None,
):
    package_name = package_context.package_name
    package_version = package_context.version
    image_full_name = get_image_name(registry, repository, image_name, package_name, package_version)

    buildx_required = False
    build_command = ["build", ".", "-t", image_full_name]
    docker_build_env = os.environ.copy()
    if network:
        build_command += ["--network={}".format(network)]

    if docker_no_cache:
        build_command += ['--no-cache']

    if add_host:
        build_command += ["--add-host={}".format(x) for x in add_host]

    if target:
        build_command += ["--target={}".format(target)]

    if docker_use_remote_cache:
        remote_name = image_full_name
        if docker_remote_image_version:
            remote_name = get_image_name(registry, repository, image_name, package_name, docker_remote_image_version)
        build_command += ['--cache-from', remote_name]

    if platform:
        build_command += ["--platform={}".format(platform)]
        buildx_required = True

    if docker_secret:
        # We need enable BuildKit to use --mount. See https://docs.docker.com/go/buildkit/
        docker_build_env["DOCKER_BUILDKIT"] = "1"
        build_command += ["--secret={}".format(s) for s in docker_secret]

    if build_args:
        for k, v in build_args.items():
            if v is None:
                build_command += ["--build-arg", k]
            else:
                build_command += ["--build-arg", "{}={}".format(k, v)]

    if labels:
        for k, v in labels.items():
            build_command += ["--label", "{}={}".format(k, v)]

    if buildx_required:
        build_command = ["buildx"] + build_command

    build_out, _ = package.process.run_process(
        get_docker_binary(),
        build_command,
        cwd=docker_root,
        add_line_timestamps=True,
        tee=True,
        env=docker_build_env,
    )
    out = ["docker build:", build_out]

    digest = None
    if push:
        push_out, _ = package.process.run_process(
            get_docker_binary(),
            [
                "push",
                image_full_name,
            ],
            add_line_timestamps=True,
            tee=True,
        )
        digest = extract_digest(push_out)

        out += ["docker push:", push_out]

        if nanny_release:
            nanny_release = nanny_release.upper()
            assert nanny_release in const.NANNY_RELEASE_TYPES
            image_name, image_tag = image_full_name.rsplit(":", 1)
            data = {
                "spec": {
                    "type": "DOCKER_RELEASE",
                    "docker_release": {
                        "image_name": image_name[len(registry + "/") :],  # nanny needs name wo registry
                        "image_tag": image_tag,
                        "release_type": nanny_release,
                    },
                },
            }
            logger.info("Will notify nanny about release: %s", data)
            requests.post(
                const.NANNY_RELEASE_URL,
                headers={'Content-Type': 'application/json'},
                data=json.dumps(data),
            )
    with exts.tmp.temp_dir() as temp_dir:
        base_result_name = package_context.resolve_filename(extra={"pattern": "{package_name}.{package_version}"})
        out_file = os.path.join(temp_dir, "build_docker_package.out.txt")
        with open(out_file, "w") as f:
            f.write("\n".join(out))
        if save_docker_image:
            image_file = os.path.join(temp_dir, base_result_name + ".img")
            exts.fs.ensure_dir(os.path.dirname(image_file))
            package.process.run_process(get_docker_binary(), ["save", "-o", image_file, image_full_name])

        package_filename = package_context.resolve_filename(extra={"package_ext": "tar.gz"})
        result_file = os.path.join(result_dir, package_filename)
        exts.fs.ensure_dir(os.path.dirname(result_file))
        exts.archive.create_tar(
            temp_dir, result_file, exts.archive.GZIP, exts.archive.Compression.Default, fixed_mtime=None
        )

    return result_file, PackageInfo(image_full_name, digest)
