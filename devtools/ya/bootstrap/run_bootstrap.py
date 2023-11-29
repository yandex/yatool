#!/usr/bin/python3

import sys
import random
import string
import argparse
import subprocess


def _get_random_name(length=10):
    return ''.join([random.choice(string.ascii_letters) for _ in range(length)])


def _get_mount_arg(source, target, readonly=False):
    return ["--mount", f"type=bind,source={source},target={target}{',readonly' if readonly else ''}"]


def _execute_docker_cmd(args, cwd=None, stdout=None, stderr=None):
    cmd = ["docker"] + args
    proc = subprocess.Popen(
        cmd,
        stdout=stdout,
        stderr=stderr,
        cwd=cwd,
    )
    proc.communicate()
    assert proc.returncode == 0
    return


def _check_docker():
    try:
        _execute_docker_cmd(["--version"])
    except Exception:
        print("ERROR: Please install docker in your system or add it to PATH.", file=sys.stderr)
        sys.exit(1)


def _build_images(source_root, all_platforms_build):
    base_cmd = ["build", "."]

    images = {}
    STAGES = 2 if all_platforms_build else 1
    for stage in range(1, STAGES + 1):
        path = f"devtools/ya/bootstrap/stage{stage}.Dockerfile"
        images[f"stage{stage}"] = {
            "path": path,
            "tag": f"ya-bootstrap-stage{stage}:latest",
        }

    for stage, info in images.items():
        tag = info["tag"]
        path = info["path"]
        print(f"Building image {tag} from {path}", file=sys.stderr)
        cmd = base_cmd + [
            "-f",
            path,
            "--tag",
            tag,
        ]
        _execute_docker_cmd(cmd, source_root)

    return images


def prepare_docker(source_root, all_platforms_build):
    _check_docker()
    images = _build_images(source_root, all_platforms_build)
    return images


def execute_image(source_root, result_root, image_tag):
    name = _get_random_name()
    cmd = (
        [
            "run",
            "--name",
            name,
        ]
        + _get_mount_arg(source_root, "/source_root", True)
        + _get_mount_arg(result_root, "/result", False)
        + [image_tag]
    )

    _execute_docker_cmd(cmd)
    return name


def remove_images(images):
    tags = []
    for image in images.values():
        tags.append(image["tag"])
    _execute_docker_cmd(["rmi"] + tags)


def remove_containers(containers):
    _execute_docker_cmd(["rm"] + containers)


def parse_args():
    parser = argparse.ArgumentParser()

    parser.add_argument("--source-root", action="store", required=True, help="Path to repository with sources")
    parser.add_argument("--result-root", action="store", required=True, help="Path for results placing.")
    parser.add_argument(
        "--all-platforms-build",
        action="store_true",
        required=False,
        default=False,
        help="Build toolchain for all platforms (with all-new ya and ymake)",
    )
    parser.add_argument(
        "--cleanup",
        action="store_true",
        required=False,
        default=False,
        help="Remove docker images and containers after execution",
    )

    args = parser.parse_args()
    return args


def main():
    args = parse_args()
    images = prepare_docker(args.source_root, args.all_platforms_build)

    executed_containers = []

    st1 = execute_image(args.source_root, args.result_root, images["stage1"]["tag"])
    executed_containers.append(st1)

    if args.all_platforms_build:
        st2 = execute_image(args.source_root, args.result_root, images["stage2"]["tag"])
        executed_containers.append(st2)

    if args.cleanup:
        print("cleaning up containers and images", file=sys.stderr)
        remove_containers(executed_containers)
        remove_images(images)


if __name__ == "__main__":
    main()
