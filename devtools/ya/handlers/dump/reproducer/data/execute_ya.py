import argparse
import copy
import json
import logging
import os
import pathlib
import re
import tempfile
import shutil
import subprocess
import sys

import typing as tp


logger = logging.getLogger(__name__)

YMAKE_ARGS_TO_EXCLUDE = {"--write-meta-data"}


def parse_args():
    parser = argparse.ArgumentParser()

    parser.add_argument('file', help="Path to input json of ya -")
    parser.add_argument('--trunk-revision', required=True)
    parser.add_argument('--patch-path', required=False)
    parser.add_argument(
        '--repeat-ymake-run', help="Index of ymake run, that should be repeated", required=False, type=int,
    )
    parser.add_argument(
        '--include-ymake-args',
        nargs='*',
        default=None,
        help="List of ymake arguments to force include (override exclusion)",
    )
    parser.add_argument(
        '--exclude-ymake-args',
        nargs='*',
        default=None,
        help="List of ymake arguments to force exclude",
    )

    args = parser.parse_args()

    args.include_ymake_args = set(args.include_ymake_args or [])
    args.exclude_ymake_args = set(args.exclude_ymake_args or [])
    if args.include_ymake_args & args.exclude_ymake_args:
        parser.error(
            f"Arguments cannot be in both --include-ymake-args and --exclude-ymake-args: {', '.join(include_set & exclude_set)}"
        )

    return args


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
        return [traverse_and_work_with_values(item, func) for item in obj]
    elif isinstance(obj, dict):
        result = {}
        for key, value in obj.items():
            if key == "oauth_token_path":
                home = pathlib.Path.home()
                token_path = home / ".ya_token"
                logger.error('Setting %s value to %s', key, token_path)
                result[key] = str(token_path)
            elif key == "oauth_token":
                home = pathlib.Path.home()
                token_path = home / ".ya_token"
                with token_path.open('r') as f:
                    token = f.read()
                obfuscated_token = token[:3] + "*" * (len(token) - 6) + token[-3:]
                logger.error('Setting %s value to %s', key, obfuscated_token)
                result[key] = str(token)
            elif 'token' in key and isinstance(value, str):
                logger.error('Setting %s value to None', key)
                result[key] = None
            else:
                result[key] = traverse_and_work_with_values(value, func)
        return result
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


def filter_ymake_args(args: list[str], include_args: list[str], exclude_args: list[str]) -> list[str]:
    args_to_exclude = YMAKE_ARGS_TO_EXCLUDE | set(exclude_args)
    args_to_exclude = args_to_exclude - set(include_args)

    result = []
    i = 0
    args_length = len(args)

    while i < args_length:
        current_arg = args[i]
        if current_arg in args_to_exclude:
            if i + 1 < args_length and not args[i+1].startswith('-'):
                i += 2  # Skiping flag and its value
            else:
                i += 1  # Skipping flag only
        else:
            result.append(current_arg)
            i += 1

    return result



def copy_file(
    source_root: pathlib.Path,
    target_root: pathlib.Path,
    file_path: str,
) -> None:
    file_path_obj = pathlib.Path(file_path)
    if file_path_obj.is_absolute():
        file_path_obj = file_path_obj.relative_to('/')
    directory, _ = os.path.split(file_path_obj)

    target_dir = target_root / directory
    target_dir.mkdir(parents=True, exist_ok=True)

    source_file = source_root / file_path_obj
    target_file = target_root / file_path_obj
    shutil.copy(source_file, target_file)


def process_file(
    file: pathlib.Path,
    debug_bundle_root: pathlib.Path,
    new_arcadia_root: pathlib.Path,
    repeat_ymake_run: str | None = None,
):
    with file.open('r') as launch:
        initial_launch_deserialized = json.load(launch)
    launch_deserialized = copy.deepcopy(initial_launch_deserialized)

    arc_root = find_value_by_key(initial_launch_deserialized, 'reproducer_arc_root')
    import logging

    logger = logging.getLogger(__name__)

    launch_deserialized['args'] = traverse_and_work_with_values(
        initial_launch_deserialized['args'],
        lambda x: x.replace(arc_root, str(new_arcadia_root)),
    )

    old_dot_ya = find_value_by_key(launch_deserialized, 'reproducer_cache_dir')

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

    if test_list_path := launch_deserialized['args']['list_tests_output_file']:
        if test_list_path.exists():
            test_list_directory, _ = os.path.split(pathlib.Path(test_list_path))
            tests_output_dir = temp_dir / test_list_directory
            tests_output_dir.mkdir(parents=True, exist_ok=True)

    if launch_deserialized['args'][f'custom_conf_{repeat_ymake_run}']:
        source_conf_path = pathlib.Path(
            initial_launch_deserialized['args'][f'custom_conf_{repeat_ymake_run}']
        ).relative_to('/')
        copy_file(
            source_root=debug_bundle_root,
            target_root=temp_dir,
            file_path=str(source_conf_path)
        )

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
        proc = subprocess.run(args=cmd, cwd=str(path), stdout=sys.stderr)
        if proc.returncode != 0:
            raise Exception(f"during {' '.join(cmd)}")


def unmount_arcadia(arc_root: pathlib.Path):
    cmd = ["arc", "unmount", arc_root, "--forget"]

    proc = subprocess.run(args=cmd, stdout=sys.stderr)

    if proc.returncode != 0:
        print("Failed during arc unmount", file=sys.stderr)


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

    env = os.environ.copy()
    result, new_dot_ya = process_file(pathlib.Path(args.file), debug_bundle_root, new_arcadia_root, args.repeat_ymake_run)
    env['YA_CACHE_DIR'] = str(new_dot_ya)

    if args.repeat_ymake_run is not None:
        debug_file = debug_bundle_root / "debug.json"
        debug_data = json.loads(debug_file.read_text())

        try:
            actual_ymake_args = filter_ymake_args(
                debug_data[f'ymake_run_{args.repeat_ymake_run}']['run']['args'],
                include_args=args.include_ymake_args,
                exclude_args=args.exclude_ymake_args,
            )
            old_dot_ya = find_value_by_key(debug_data['params'], 'reproducer_cache_dir')
            ymake_args = traverse_and_work_with_values(
                actual_ymake_args,
                lambda x: x.replace(
                    old_dot_ya,
                    str(new_dot_ya),
                ),
            )

            get_ymake_path_proc = subprocess.run(
                args=["ya", "tool", "ymake", "--help"],
                capture_output=True,
                text=True,
                cwd=str(new_arcadia_root),
                env=env,
            )

            ymake_binary = None
            if get_ymake_path_proc.stdout:
                first_line = get_ymake_path_proc.stdout.split('\n')[0]
                match = re.search(r'Usage:\s+(\S+)', first_line)
                if match:
                    ymake_binary = match.group(1)

            if not ymake_binary:
                raise Exception(f"Failed to extract ymake binary path from 'ya tool ymake --help'. Output: {get_ymake_path_proc.stdout}")

            logger.info('Using ymake binary at: %s', ymake_binary)

            cmd = [ymake_binary] + ymake_args[1:]

        except KeyError as e:
            logging.exception('ERROR: Unexistent ymake run number was given. Available: %s', debug_data.keys())
            raise Exception() from e

        proc = subprocess.run(
            args=cmd,
            input=json.dumps(debug_data[f'ymake_run_{args.repeat_ymake_run}']).encode('utf-8'),
            cwd=str(new_arcadia_root),
            env=env,
        )

    else:
        path_to_launch_data = result / args.file

        cmd = ["ya", "-"]

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
        print(f"NOT Removing {new_dot_ya}", file=sys.stderr)
        shutil.rmtree(str(new_dot_ya))
    else:
        print(
            f"Ya execution results are stored in {new_dot_ya}",
            file=sys.stderr,
        )


if __name__ == "__main__":
    main(parse_args())
