from __future__ import annotations

import abc
import difflib
import json
import logging
import os
import subprocess
import sys
import tempfile
import typing as tp
import yaml

from collections.abc import Callable
from pathlib import Path, PurePath

import core.config
import core.resource
import devtools.ya.test.const as const
import exts.func
import exts.os2
import marisa_trie
import yalibrary.display
import yalibrary.makelists
import yalibrary.tools

from library.python.testing.style import rules
from library.python.fs import replace_file
from . import state_helper
from .enums import StylerKind, STDIN_FILENAME_STAMP


logger = logging.getLogger(__name__)
display = yalibrary.display.build_term_display(sys.stdout, exts.os2.is_tty())


class Spec(tp.NamedTuple):
    kind: StylerKind
    ruff: bool = False


_REGISTRY: dict[Spec, type[BaseStyler]] = {}
_SUFFIX_MAPPING: dict[str, list[Spec]] = {}


def _register(cls: type[BaseStyler]) -> type[BaseStyler]:
    _REGISTRY[cls.SPEC] = cls
    for suffix in cls.SUFFIXES:
        _SUFFIX_MAPPING.setdefault(suffix, []).append(cls.SPEC)
    return cls


def select_styler(target: PurePath, ruff: bool) -> type[BaseStyler] | None:
    """Find matching spec and return respective styler class"""

    if target.suffix in _SUFFIX_MAPPING:
        key = target.suffix
    elif target.name in _SUFFIX_MAPPING:
        key = target.name
    else:
        return

    # find the first full match
    for spec in _SUFFIX_MAPPING[key]:
        if (s := Spec(spec.kind, ruff)) in _REGISTRY:
            return _REGISTRY[s]


@exts.func.lazy
def _find_root() -> str:
    return core.config.find_root()


def _flush_to_file(path: Path, content: str) -> None:
    display.emit_message(f'[[good]]fix {path}')

    tmp = path.with_suffix(".tmp")
    tmp.write_bytes(content.encode())

    # never break original file
    path_st_mode = os.stat(path).st_mode
    replace_file(str(tmp), str(path))
    os.chmod(path, path_st_mode)


def _flush_to_terminal(path: Path, content: str, formatted_content: str, full_output: bool = False) -> None:
    if full_output:
        display.emit_message(f'[[good]]fix {path}[[rst]]\n{formatted_content}\n')
    else:
        diff = difflib.unified_diff(content.splitlines(), formatted_content.splitlines())
        diff = list(diff)[2:]  # Drop header with filenames
        diff = "\n".join(diff)

        display.emit_message(f'[[good]]fix {path}[[rst]]\n{diff}\n')


class BaseStyler(abc.ABC):
    # Whether a styler should run if not selected explicitly
    DEFAULT_ENABLED: tp.ClassVar[bool]
    # Unique description of a styler
    SPEC: tp.ClassVar[Spec]
    # Series of strings upon which the proper styler is selected
    SUFFIXES: tp.ClassVar[tuple[tp.LiteralString, ...]]

    def __init__(self, args) -> None:
        self.args = args

    @abc.abstractmethod
    def format(self, path: PurePath, content: str) -> str:
        """Format and return formatted file content"""

    def style(self, target: PurePath | Path, loader: Callable[..., str]):
        """
        Execute `format` and store or display the result.
        Return 0 if no formatting happened, 1 otherwise
        """
        content = loader()

        def run_format() -> str:
            formatted_content = self.format(target, content)
            if formatted_content and formatted_content[-1] != '\n':
                return formatted_content + '\n'
            return formatted_content

        if target.name.startswith(STDIN_FILENAME_STAMP):
            print(run_format())
        else:
            target = tp.cast(Path, target)
            if self.args.force or not (reason := rules.get_skip_reason(str(target), content)):
                formatted_content = run_format()
                if formatted_content != content:
                    if self.args.dry_run:
                        _flush_to_terminal(target, content, formatted_content, self.args.full_output)
                    elif not self.args.check:
                        _flush_to_file(target, formatted_content)
                    return 1
            else:
                logger.warning("skip by rule: %s", reason)
        return 0


@_register
class Black(BaseStyler):
    DEFAULT_ENABLED = True
    SPEC = Spec(StylerKind.PY)
    SUFFIXES = (".py",)

    def __init__(self, args) -> None:
        super().__init__(args)
        self.tool: str = yalibrary.tools.tool("black" if not self.args.py2 else "black_py2")  # type: ignore
        self.config_file = self.load_config()

    def load_config(self) -> str:
        try:
            config_map = core.config.config_from_arc_rel_path(const.DefaultLinterConfig.Python)
        except Exception as e:
            logger.warning("Couldn't obtain config from fs due to error %s, reading from memory", repr(e))
            temp = tempfile.NamedTemporaryFile(delete=False)  # will be deleted by tmp_dir_interceptor
            temp.write(core.resource.try_get_resource("config.toml"))  # type: ignore
            temp.flush()  # will be read from other subprocesses
            config_file = temp.name
        else:
            config_file = os.path.join(_find_root(), config_map[const.PythonLinterName.Black])
        return config_file

    def _run_black(self, content: str, path: PurePath) -> str:
        black_args = [self.tool, "-q", "-", "--config", self.config_file]

        p = subprocess.Popen(
            black_args,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            shell=False,
            text=True,
        )
        out, err = p.communicate(input=content)

        # Abort styling on signal
        if p.returncode < 0:
            state_helper.stop()

        if err:
            raise RuntimeError('error while running black on file "{}": {}'.format(path, err.strip()))

        return out

    def format(self, path: PurePath, content: str) -> str:
        return self._run_black(content, path)


@_register
class Ruff(BaseStyler):
    DEFAULT_ENABLED = True
    SPEC = Spec(StylerKind.PY, ruff=True)
    SUFFIXES = (".py",)

    _RUFF_CONFIG_PATHS_FILE = "build/config/tests/ruff/ruff_config_paths.json"

    def __init__(self, args) -> None:
        super().__init__(args)
        self.tool: str = yalibrary.tools.tool("ruff")  # type: ignore
        self.config_file = ""
        self.trie: marisa_trie.Trie | None = None
        self.config_paths: list[str] = []

        self.load_configs()

    def load_configs(self) -> None:
        arc_root = _find_root()
        try:
            config_map = {}
            for prefix, config_path in core.config.config_from_arc_rel_path(self._RUFF_CONFIG_PATHS_FILE).items():
                config_map[os.path.normpath(os.path.join(arc_root, prefix))] = config_path
        except Exception as e:
            logger.warning("Couldn't obtain config from fs due to error %s, reading from memory", repr(e))
            temp = tempfile.NamedTemporaryFile(delete=False)  # will be deleted by tmp_dir_interceptor
            temp.write(core.resource.try_get_resource("ruff.toml"))  # type: ignore
            temp.flush()  # will be read from other subprocesses
            self.config_file = temp.name
        else:
            # Trie assigns indexes randomly, have to map back
            self.config_paths = [''] * len(config_map)
            self.trie = marisa_trie.Trie(config_map.keys())
            for prefix, idx in self.trie.items():  # type: ignore
                self.config_paths[idx] = os.path.join(arc_root, config_map[prefix])

    def lookup_config(self, path: PurePath) -> str:
        """
        Return path to the linter's config file.
        If config paths mapping were successfully loaded to trie,
        find the one with the longest prefix matching the file being linted (custom config).
        Otherwise, return path to default config
        """
        if self.trie:
            keys = self.trie.prefixes(str(path))
            key = sorted(keys, key=len)[-1]
            return self.config_paths[self.trie[key]]
        else:
            return self.config_file

    def _run_ruff(self, content: str, path: PurePath, config_path: str, cmd_args: list[str]) -> str:
        ruff_args = [self.tool] + cmd_args + ["--config", config_path, "-s", "-"]

        p = subprocess.Popen(
            ruff_args,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            shell=False,
            text=True,
            env=dict(os.environ.copy(), RUFF_OUTPUT_FORMAT="concise"),
        )
        out, err = p.communicate(input=content)

        if p.returncode is None:
            with open("ruff.out", "w") as fd:
                fd.write(f"Ruff out: {out}")
                fd.write(f"\nRuff err: {err}")
            error_msg = f'Something went wrong while running ruff {" ".join(cmd_args)} on file "{path}"'
            error_msg += "\nCheck file 'ruff.out' for errors"
            raise RuntimeError(error_msg)

        # Abort styling on signal
        if p.returncode < 0:
            state_helper.stop()

        if p.returncode != 0 and err:
            raise RuntimeError(
                'error while running ruff {} on file "{}": {}'.format(" ".join(cmd_args), path, err.strip())
            )

        return out

    def format(self, path: PurePath, content: str) -> str:
        ruff_config = self.lookup_config(path)
        stdin_filename = ["--stdin-filename", path]

        out = self._run_ruff(content, path, ruff_config, ["format"] + stdin_filename)
        # launch check fix to sort imports
        out = self._run_ruff(out, path, ruff_config, ["check", "--fix"] + stdin_filename)
        return out


@_register
class Golang(BaseStyler):
    DEFAULT_ENABLED = True
    SPEC = Spec(StylerKind.GO)
    SUFFIXES = (".go",)

    def __init__(self, args) -> None:
        super().__init__(args)
        self.tool: str = yalibrary.tools.tool("yoimports")  # type: ignore

    def format(self, path: PurePath, content: str) -> str:
        p = subprocess.Popen(
            [self.tool, "-"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            shell=False,
            text=True,
        )
        out, err = p.communicate(input=content)

        # Abort styling on signal
        if p.returncode < 0:
            state_helper.stop()

        if err:
            raise Exception('error while running yoimports on file "{}": {}'.format(path, err.strip()))

        return out


@_register
class ClangFormat(BaseStyler):
    DEFAULT_ENABLED = True
    SPEC = Spec(StylerKind.CPP)
    SUFFIXES = (".cpp", ".cc", ".C", ".c", ".cxx", ".h", ".hh", ".hpp", ".H")

    def __init__(self, args) -> None:
        super().__init__(args)
        self.tool: str = yalibrary.tools.tool("clang-format")  # type: ignore
        self.config = self.load_config()

    def load_config(self) -> str:
        try:
            config_map = core.config.config_from_arc_rel_path(const.DefaultLinterConfig.Cpp)
        except Exception as e:
            logger.warning("Couldn't obtain config from fs due to error %s, reading from memory", repr(e))
            style_config = core.resource.try_get_resource("config.clang-format")
            return json.dumps(yaml.safe_load(style_config))
        else:
            config_file = config_map[const.CppLinterName.ClangFormat]
            with open(os.path.join(_find_root(), config_file)) as afile:
                return json.dumps(yaml.safe_load(afile))

    def format(self, path: PurePath, content: str) -> str:
        if path.suffix == ".h":
            content = self.fix_header(content)

        p = subprocess.Popen(
            [self.tool, "-assume-filename=a.cpp", "-style=" + self.config],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            shell=False,
            text=True,
        )

        out, err = p.communicate(input=content)

        # Abort styling on signal
        if p.returncode < 0:
            state_helper.stop()

        if err:
            raise Exception("error while running clang-format: " + err)

        return out

    @staticmethod
    def fix_header(content: str) -> str:
        expected = "#pragma once"
        for line in content.split("\n"):
            if line.strip() == "" or line.strip().startswith("//"):
                continue
            elif line.strip() == expected:
                return content
            return expected + "\n\n" + content

        return content


@_register
class Cuda(ClangFormat):
    DEFAULT_ENABLED = False
    SPEC = Spec(StylerKind.CUDA)
    SUFFIXES = (".cu", ".cuh")


@_register
class YaMake(BaseStyler):
    DEFAULT_ENABLED = False
    SPEC = Spec(StylerKind.YAMAKE)
    SUFFIXES = ("ya.make", "ya.make.inc")

    def __init__(self, args) -> None:
        super().__init__(args)

    def format(self, path: PurePath, content: str) -> str:
        yamake = yalibrary.makelists.from_str(content)
        return yamake.dump()
