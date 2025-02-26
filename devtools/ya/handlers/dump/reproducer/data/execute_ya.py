import argparse
import json
import os
import pathlib
import tempfile
import shutil
import subprocess
import sys

import typing as tp


def parse_args():
    parser = argparse.ArgumentParser()

    parser.add_argument('file', help="Path to input json of ya -")
    parser.add_argument('--trunk-revision', required=True)
    parser.add_argument('--patch-path', required=False)

    return parser.parse_args()


def detect_bundle_root():
    path = pathlib.Path.cwd()
    while path != path.parent:
        if any([x.name == 'debug.json' for x in path.iterdir()]):
            return path
        else:
            path = path.parent

    raise Exception('Failed to detect bundle root. Try running script from bundle')


def find_value_by_key(obj: dict, target_key: str):
    for key, value in obj.items():
        if key == target_key:
            return value
        if isinstance(value, dict):
            found = find_value_by_key(value, target_key)
            if found:
                return found

    return None


def traverse_and_work_with_values(obj: tp.Any, func: tp.Callable[[str], str]):
    if isinstance(obj, list):
        for index, item in enumerate(obj):
            obj[index] = traverse_and_work_with_values(item, func)
    elif isinstance(obj, dict):
        for key, value in obj.items():
            if key == "oauth_token_path":
                home = pathlib.Path.home()
                token_path = home / ".ya_token"
                print(f"Setting {key} value to {token_path}", file=sys.stderr)
                obj[key] = str(token_path)
            elif 'token' in key and isinstance(value, str):
                print(f"Setting {key} value to None", file=sys.stderr)
                obj[key] = None
            else:
                obj[key] = traverse_and_work_with_values(value, func)
    elif isinstance(obj, str):
        res = func(obj)
        if obj != res:
            print(f'Replace {obj} -> {res}', file=sys.stderr)
        return res
    return obj


def add_prefix_to_path(x: str, target: pathlib.Path, ignore_list: tuple[str]):
    if x.startswith(ignore_list):
        return x

    if x.startswith('/') and not x.startswith('//'):
        return str(target / x[1:])

    return x


def process_file(file: pathlib.Path, debug_bundle_root: pathlib.Path, new_arcadia_root: pathlib.Path):
    with file.open('r') as launch:
        launch_deserialized = json.load(launch)

    arc_root = find_value_by_key(launch_deserialized, 'arc_root')

    launch_deserialized['args'] = traverse_and_work_with_values(
        launch_deserialized['args'],
        lambda x: x.replace(arc_root, str(new_arcadia_root)),
    )

    old_dot_ya = find_value_by_key(launch_deserialized, 'bld_dir')

    temp_dir = pathlib.Path(tempfile.mkdtemp('_reproducer'))
    new_dot_ya = temp_dir / old_dot_ya[1:]

    launch_deserialized['args'] = traverse_and_work_with_values(
        launch_deserialized['args'],
        lambda x: x.replace(
            old_dot_ya,
            str(new_dot_ya),
        ),
    )

    launch_deserialized['args'] = traverse_and_work_with_values(
        launch_deserialized['args'],
        lambda x: add_prefix_to_path(
            x,
            temp_dir,
            (str(new_dot_ya), str(new_arcadia_root)),
        ),
    )

    new_inputs = temp_dir / "inputs"
    new_inputs.mkdir(exist_ok=True)

    (new_inputs / file.name).write_text(json.dumps(launch_deserialized))

    return temp_dir, new_dot_ya


def mount_arcadia():
    arc_root = tempfile.mkdtemp(suffix="arcadia")

    cmd = ["arc", "mount", arc_root]

    proc = subprocess.run(args=cmd, stdout=sys.stderr)

    if proc.returncode != 0:
        raise Exception("Failed during arc mount")

    return pathlib.Path(arc_root)


def clean_arcadia(path: pathlib.Path):

    cmd_list = [
        ["arc", "clean", "-dx"],
        ["arc", "checkout", "."],
        ["arc", "checkout", "trunk"],
    ]

    for cmd in cmd_list:
        proc = subprocess.run(
            args=cmd,
            cwd=str(path),
            stdout=sys.stderr
        )
        if proc.returncode != 0:
            raise Exception(f"during {' '.join(cmd)}")


def unmount_arcadia(arc_root: pathlib.Path):
    cmd = ["arc", "unmount", arc_root, "--forget"]

    proc = subprocess.run(args=cmd, stdout=sys.stderr)

    if proc.returncode != 0:
        raise Exception("Failed during arc unmount")


def prepare_arcadia(arc_root: pathlib.Path, revision: str, patch_path: pathlib.Path):
    cmd_list = [
        ["arc", "checkout", revision],
    ]

    if patch_path.exists():
        with tempfile.NamedTemporaryFile('w', delete=False) as temp_patch:
            temp_patch.write(patch_path.read_text())

        cmd_list.append(["patch", "-p0", "-i", temp_patch.name])

    for cmd in cmd_list:
        try:
            proc = subprocess.run(
                args=cmd,
                cwd=arc_root,
                stdout=sys.stderr,
            )
            if proc.returncode != 0:
                raise Exception(f"during {' '.join(cmd)}")
        except Exception:
            raise Exception(cmd, arc_root)
    return


def main(args):
    if 'REPRO_ARCADIA_ROOT' not in os.environ:
        new_arcadia_root = mount_arcadia()
    else:
        new_arcadia_root = pathlib.Path(os.environ['REPRO_ARCADIA_ROOT'])

        if not (new_arcadia_root.exists() and new_arcadia_root.is_dir()):
            print(f'Path {new_arcadia_root} not found.', file=sys.stderr)
            sys.exit(1)

        if 'UNMOUNT_ARCADIA' not in os.environ:
            os.environ['UNMOUNT_ARCADIA'] = '0'

    if os.environ.get('PREPARE_ARCADIA', '1') == '1':
        clean_arcadia(new_arcadia_root)
        prepare_arcadia(new_arcadia_root, args.trunk_revision, pathlib.Path(args.patch_path))

    debug_bundle_root = detect_bundle_root()

    result, new_dot_ya = process_file(pathlib.Path(args.file), debug_bundle_root, new_arcadia_root)

    path_to_launch_data = result / args.file

    cmd = ["ya", "-"]

    env = os.environ.copy()
    env['YA_CACHE_DIR'] = str(new_dot_ya)

    with path_to_launch_data.open() as file:
        proc = subprocess.run(
            args=cmd,
            stdin=file,
            cwd=str(new_arcadia_root),
            env=env,
        )

    if 'REPRO_ARCADIA_ROOT' not in os.environ and os.environ.get('UNMOUNT_ARCADIA', '1') == '1':
        unmount_arcadia(new_arcadia_root)
    else:
        print(
            f"Will not unmount {new_arcadia_root} due to set REPRO_ARCADIA_ROOT or UNMOUNT_ARCADIA_ROOT",
            file=sys.stderr,
        )

    if os.environ.get('REMOVE_YA_ROOT', '1') == '1':
        print(f"Removing {new_dot_ya}", file=sys.stderr)
        shutil.rmtree(str(new_dot_ya))
    else:
        print(
            f"Ya execution results are stored in {new_dot_ya}",
            file=sys.stderr,
        )


if __name__ == "__main__":
    main(parse_args())
