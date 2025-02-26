import copy
import json
import pathlib
import platform
import subprocess
import tempfile
import typing as tp

from jinja2 import Environment, select_autoescape

import devtools.ya.core.common_opts as common_opts
import library.python.resource as rs
import yalibrary.debug_store.processor as processor
import yalibrary.evlog as evlog
import yalibrary.platform_matcher as pm
import yalibrary.tools

type ArcRevisionMaybe = str | None
type HasUnaccountedChanges = bool


class Reproducer:
    def __init__(self, debug_item: processor.DumpItem):
        self.debug_item = debug_item
        self.evlog_path = debug_item.path

        self.jinja_env = Environment(autoescape=select_autoescape())

        self.makefile_template = self.jinja_env.from_string(rs.resfs_read("data/Makefile.jinja2").decode('utf-8'))

        self.temp_dir = pathlib.Path(tempfile.mkdtemp(suffix="repro"))

    def _find_evlog_entries(self, namespace=None, event=None) -> tp.Generator[dict, None, None]:
        reader = evlog.EvlogReader(self.evlog_path)
        for line in reader:
            if line.get("namespace") == namespace and line.get("event") == event:
                yield line

    def _mine_trunk_arc_revision(self) -> ArcRevisionMaybe:
        vcs_info = self.debug_item.debug_bundle_data.get("vcs_info", {})

        try:
            return f"r{vcs_info["ARCADIA_SOURCE_LAST_CHANGE"]}"
        except KeyError:
            return None

    def _mine_loose_commit(self) -> list[tuple[HasUnaccountedChanges, ArcRevisionMaybe]]:
        commits = []

        # The following two for-loops are both for loose-commits
        # However, the first message type is not yet widespread.

        for line in self._find_evlog_entries("build.changelist", "cache-revision"):
            commits.append((False, line["value"]["revision"]))
            break

        for line in self._find_evlog_entries("build.changelist", "changelist-status"):
            if line["value"]["status"] == "success":
                commits.append((False, line["value"]["to_hash"]))
                break

        try:
            vcs_info = self.debug_item.debug_bundle_data.get("vcs_info", {})
            commits.append((vcs_info["DIRTY"] == "dirty", vcs_info["ARCADIA_SOURCE_HG_HASH"]))
        except KeyError:
            pass

        if not commits:
            commits.append((True, None))

        return commits

    def make_diff(self) -> tuple[bool, str | None]:
        # TODO return dirty flag in some way
        trunk_revision = self._mine_trunk_arc_revision()
        if trunk_revision is None:
            return None

        candidate_commits = self._mine_loose_commit()

        patch_dir = self.temp_dir / "revisions"
        patch_dir.mkdir(exist_ok=True)
        patch = patch_dir / "diff.patch"

        for dirty, commit in candidate_commits:
            if commit:
                with patch.open("w") as file:
                    arc_tool = yalibrary.tools.tool('arc')
                    proc = subprocess.run(
                        [arc_tool, "diff", trunk_revision, commit],
                        text=True,
                        stdout=file,
                        stderr=subprocess.DEVNULL,
                    )

                if proc.returncode != 0:
                    patch.open('w').close()
                else:
                    return dirty, str(patch)

        return dirty, None

    def _mine_parameters(self) -> dict:
        return self.debug_item.debug_bundle_data.get("params", {})

    def get_ya_minus_json(self, dump_to: pathlib.Path, configure_only=False) -> str | None:
        handler = self.debug_item.debug_bundle_data["handler"]
        if handler is None:
            return None

        prefix = handler['prefix'][1:]
        result_json = {}

        current_nesting = result_json

        for index, part in enumerate(prefix):
            current_nesting['handler'] = part
            if index != len(prefix) - 1:
                current_nesting['args'] = {}
                current_nesting = current_nesting['args']
            else:
                current_nesting['args'] = self._mine_parameters()

        args = current_nesting["args"]

        users_arc_root = args['arc_root']

        if 'abs_targets' in args:
            args['build_targets'] = copy.deepcopy(args['abs_targets'])
            for i, target in enumerate(args['build_targets']):
                args['build_targets'][i] = target[len(users_arc_root) + 1 :]

        if 'target_platforms' in args and not args['target_platforms']:
            args['target_platforms'].append(
                common_opts.CrossCompilationOptions.make_platform(
                    pm.matcher.guess_platform(platform.system()),
                ),
            )

        if configure_only:
            if 'build_threads' in args:
                args['build_threads'] = 0
                args['dump_graph'] = 'text'
            else:
                return None

        dump_to.write_text(json.dumps(result_json, indent=4))
        return str(dump_to)

    def fill_makefile(self, patch_path, configure_generated) -> pathlib.Path:

        trunk_commit = self._mine_trunk_arc_revision()

        data = {
            'command': str(self.debug_item),
            'trunk_revision': trunk_commit,
            'patch_file': patch_path,
            'configure': configure_generated,
            'repro_sources_path': str(self.temp_dir.relative_to(self.temp_dir.parts[0])),
        }

        makefile = self.temp_dir / "Makefile"
        makefile.write_text(self.makefile_template.render(**data))

        return makefile

    def dump_python_scripts(self) -> str:
        for item in ["execute_ya.py"]:
            path = self.temp_dir / item

            path.write_text(rs.resfs_read(f"data/{item}").decode('utf-8'))

    def generate_inputs(self):
        directory = self.temp_dir / "inputs"
        directory.mkdir(exist_ok=True)

        base_name = "last_command"
        configure_generated = False
        for configure in [False, True]:
            path = directory / f"{base_name}{"_configure" if configure else ""}.json"
            result = self.get_ya_minus_json(path, configure)
            if result:
                configure_generated |= configure

        return configure_generated

    def prepare_reproducer(self) -> tuple[bool, pathlib.Path, pathlib.Path]:

        dirty, patch_path = self.make_diff()

        configuration_available = self.generate_inputs()

        makefile_path = self.fill_makefile(patch_path, configuration_available)
        self.dump_python_scripts()

        return dirty, self.temp_dir, makefile_path
