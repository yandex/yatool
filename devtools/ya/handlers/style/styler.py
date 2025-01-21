from __future__ import annotations

import logging
import os
import subprocess
import sys
import typing as tp
from enum import StrEnum, auto
from pathlib import PurePath

import devtools.ya.test.const as const
import exts.func
import exts.os2
import yalibrary.display
import yalibrary.makelists
import yalibrary.tools

from . import config
from . import state_helper


logger = logging.getLogger(__name__)
display = yalibrary.display.build_term_display(sys.stdout, exts.os2.is_tty())


class StylingError(Exception):
    pass


class StylerKind(StrEnum):
    PY = auto()
    CPP = auto()
    CUDA = auto()
    YAMAKE = auto()
    GO = auto()
    YQL = auto()


class StylerOptions(tp.NamedTuple):
    py2: bool = False
    config_loaders: tuple[config.ConfigLoader, ...] | None = None


class Spec(tp.NamedTuple):
    kind: StylerKind
    ruff: bool = False


_REGISTRY: dict[Spec, type[Styler]] = {}
_SUFFIX_MAPPING: dict[str, list[Spec]] = {}


def _register[T: Styler](cls: type[T]) -> type[T]:
    _REGISTRY[cls.spec] = cls
    for suffix in cls.suffixes:
        _SUFFIX_MAPPING.setdefault(suffix, []).append(cls.spec)
    return cls


def select_styler(target: PurePath, ruff: bool) -> type[Styler] | None:
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


class Styler(tp.Protocol):
    # Whether a styler should run if not selected explicitly
    default_enabled: tp.ClassVar[bool]
    # Unique description of a styler
    spec: tp.ClassVar[Spec]
    # Series of strings upon which the proper styler is selected
    suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]]

    def __init__(self, styler_opts: StylerOptions) -> None: ...

    def format(self, path: PurePath, content: str) -> str:
        """Format and return formatted file content"""
        ...


@_register
class Black(config.ConfigMixin):
    default_enabled: tp.ClassVar[bool] = True
    spec: tp.ClassVar[Spec] = Spec(StylerKind.PY)
    suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (".py",)

    def __init__(self, styler_opts: StylerOptions) -> None:
        self._tool: str = yalibrary.tools.tool("black" if not styler_opts.py2 else "black_py2")  # type: ignore
        super().__init__(
            styler_opts.config_loaders
            if styler_opts.config_loaders
            else (
                config.AutoincludeConfig(linter_name=const.PythonLinterName.Black),
                config.DefaultConfig(
                    linter_name=const.PythonLinterName.Black,
                    defaults_file=const.DefaultLinterConfig.Python,
                    resource_name="config.toml",
                ),
            )
        )

    def _run_black(self, content: str, path: PurePath) -> str:
        black_args = [self._tool, "-q", "-", "--config", self.lookup_config(path)]

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
            raise StylingError('error while running black on file "{}": {}'.format(path, err.strip()))

        return out

    def format(self, path: PurePath, content: str) -> str:
        return self._run_black(content, path)


@_register
class Ruff(config.ConfigMixin):
    default_enabled: tp.ClassVar[bool] = True
    spec: tp.ClassVar[Spec] = Spec(StylerKind.PY, ruff=True)
    suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (".py",)

    def __init__(self, styler_opts: StylerOptions) -> None:
        self._tool: str = yalibrary.tools.tool("ruff")  # type: ignore
        super().__init__(
            styler_opts.config_loaders
            if styler_opts.config_loaders
            else (
                # XXX: during migration we have the following logic:
                # - if there is a custom config got from autoincludes scheme, use it
                # - else if there is a custom config got from ruff trie, use it
                # - else use default config
                config.AutoincludeConfig(linter_name=const.PythonLinterName.Ruff),
                config.RuffConfig(),
                config.DefaultConfig(
                    linter_name=const.PythonLinterName.Ruff,
                    defaults_file=const.DefaultLinterConfig.Python,
                    resource_name="ruff.toml",
                ),
            )
        )

    def _run_ruff(self, content: str, path: PurePath, config_path: config.ConfigPath, cmd_args: list[str]) -> str:
        ruff_args = [self._tool] + cmd_args + ["--config", config_path, "-s", "-"]

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
            raise StylingError(error_msg)

        # Abort styling on signal
        if p.returncode < 0:
            state_helper.stop()

        if p.returncode != 0 and err:
            raise StylingError(
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
class ClangFormat(config.ConfigMixin):
    default_enabled: tp.ClassVar[bool] = True
    spec: tp.ClassVar[Spec] = Spec(StylerKind.CPP)
    suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (".cpp", ".cc", ".C", ".c", ".cxx", ".h", ".hh", ".hpp", ".H")

    def __init__(self, styler_opts: StylerOptions) -> None:
        self._tool: str = yalibrary.tools.tool("clang-format-18")  # type: ignore
        super().__init__(
            styler_opts.config_loaders
            if styler_opts.config_loaders
            else (
                config.AutoincludeConfig(linter_name=const.CppLinterName.ClangFormat),
                config.DefaultConfig(
                    linter_name=const.CppLinterName.ClangFormat,
                    defaults_file=const.DefaultLinterConfig.Cpp,
                    resource_name="config.clang-format",
                ),
            )
        )

    def format(self, path: PurePath, content: str) -> str:
        if path.suffix == ".h":
            content = self.fix_header(content)

        p = subprocess.Popen(
            [self._tool, "-assume-filename=a.cpp", f"-style=file:{self.lookup_config(path)}"],
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
            raise StylingError("error while running clang-format: " + err)

        return out + "\n" if out and not out.endswith("\n") else out

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
    default_enabled: tp.ClassVar[bool] = False
    spec: tp.ClassVar[Spec] = Spec(StylerKind.CUDA)
    suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (".cu", ".cuh")


@_register
class Golang:
    default_enabled: tp.ClassVar[bool] = True
    spec: tp.ClassVar[Spec] = Spec(StylerKind.GO)
    suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (".go",)

    def __init__(self, styler_opts: StylerOptions) -> None:
        self._tool: str = yalibrary.tools.tool("yoimports")  # type: ignore

    def format(self, path: PurePath, content: str) -> str:
        p = subprocess.Popen(
            [self._tool, "-"],
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
            raise StylingError('error while running yoimports on file "{}": {}'.format(path, err.strip()))

        return out


@_register
class YaMake:
    default_enabled: tp.ClassVar[bool] = False
    spec: tp.ClassVar[Spec] = Spec(StylerKind.YAMAKE)
    suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = ("ya.make", "ya.make.inc")

    def __init__(self, styler_opts: StylerOptions) -> None:
        pass

    def format(self, path: PurePath, content: str) -> str:
        yamake = yalibrary.makelists.from_str(content)
        return yamake.dump()


@_register
class Yql:
    default_enabled: tp.ClassVar[bool] = False
    spec: tp.ClassVar[Spec] = Spec(StylerKind.YQL)
    suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (".yql",)

    def __init__(self, styler_opts: StylerOptions) -> None:
        self._tool: str = yalibrary.tools.tool("yql-format")  # type: ignore

    def _run_format(self, path: PurePath, content: str) -> str:
        args = [self._tool]

        p = subprocess.Popen(
            args,
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
            raise StylingError('error while running sql_formatter on file "{}": {}'.format(path, err.strip()))

        return out

    def format(self, path: PurePath, content: str) -> str:
        return self._run_format(path, content)
