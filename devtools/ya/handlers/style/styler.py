from __future__ import annotations

import logging
import os
import subprocess
import sys
import typing as tp
from collections.abc import Sequence
from enum import StrEnum, auto
from pathlib import PurePath

import devtools.ya.test.const as const
import exts.func
import exts.os2
import yalibrary.display
import yalibrary.makelists
import yalibrary.tools

from . import config as cfg
from . import state_helper


logger = logging.getLogger(__name__)
display = yalibrary.display.build_term_display(sys.stdout, exts.os2.is_tty())


class StylingError(Exception):
    pass


class StylerKind(StrEnum):
    """Corresponds to a file type"""

    PY = auto()
    CPP = auto()
    CUDA = auto()
    YAMAKE = auto()
    GO = auto()
    YQL = auto()

    @property
    def default_enabled(self) -> bool:
        """Whether styling is enabled by default"""
        return self in (self.PY, self.CPP, self.GO)


class StylerOptions(tp.NamedTuple):
    py2: bool = False
    config_loaders: tuple[cfg.ConfigLoader, ...] | None = None


class StylerOutput(tp.NamedTuple):
    content: str
    config: cfg.MaybeConfig = None


_SUFFIX_MAPPING: dict[str, set[type[Styler]]] = {}


def _register[T: Styler](cls: type[T]) -> type[T]:
    for suffix in cls.suffixes:
        _SUFFIX_MAPPING.setdefault(suffix, set()).add(cls)
    return cls


def select_suitable_stylers(target: PurePath, file_types: Sequence[StylerKind]) -> set[type[Styler]] | None:
    """Find and return matching styler class"""

    if target.suffix in _SUFFIX_MAPPING:
        key = target.suffix
    elif target.name in _SUFFIX_MAPPING:
        key = target.name
    else:
        logger.warning('skip %s (sufficient styler not found)', target)
        return

    suffix_matches = _SUFFIX_MAPPING[key]
    if not suffix_matches:
        raise AssertionError(f'No styler found for target {target}, suffix {key}')

    if file_types:
        matches = {m for m in suffix_matches if m.kind in file_types}
        if not matches:
            logger.warning('skip %s (filtered by file type)', target)
            return
    else:
        matches = {m for m in suffix_matches if m.kind.default_enabled}
        if not matches:
            options = ' or '.join(f'--{m.kind}' for m in suffix_matches)
            logger.warning('skip %s (require explicit %s or --all)', target, options)
            return

    return matches


class Styler(tp.Protocol):
    # Unique description of a styler
    kind: tp.ClassVar[StylerKind]
    # Series of strings upon which the proper styler is selected
    suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]]

    def __init__(self, styler_opts: StylerOptions) -> None: ...

    def format(self, path: PurePath, content: str) -> StylerOutput:
        """Format and return output"""
        ...


@_register
class Black(cfg.ConfigMixin):
    kind: tp.ClassVar = StylerKind.PY
    suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (".py",)

    def __init__(self, styler_opts: StylerOptions) -> None:
        self._tool: str = yalibrary.tools.tool("black" if not styler_opts.py2 else "black_py2")  # type: ignore
        super().__init__(
            styler_opts.config_loaders
            if styler_opts.config_loaders
            else (
                cfg.AutoincludeConfig.make(const.PythonLinterName.Black),
                cfg.DefaultConfig(
                    linter_name=const.PythonLinterName.Black,
                    defaults_file=const.DefaultLinterConfig.Python,
                    resource_name="config.toml",
                ),
            )
        )

    def _run_black(self, content: str, path: PurePath) -> StylerOutput:
        config = self.lookup_config(path)
        black_args = [self._tool, "-q", "-", "--config", config.path]

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

        return StylerOutput(out, config)

    def format(self, path: PurePath, content: str) -> StylerOutput:
        return self._run_black(content, path)


@_register
class Ruff(cfg.ConfigMixin):
    kind: tp.ClassVar = StylerKind.PY
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
                cfg.AutoincludeConfig.make(const.PythonLinterName.Ruff),
                cfg.RuffConfig(),
                cfg.DefaultConfig(
                    linter_name=const.PythonLinterName.Ruff,
                    defaults_file=const.DefaultLinterConfig.Python,
                    resource_name="ruff.toml",
                ),
            )
        )

    def _run_ruff(self, content: str, path: PurePath, config_path: cfg.ConfigPath, cmd_args: list[str]) -> str:
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

    def format(self, path: PurePath, content: str) -> StylerOutput:
        ruff_config = self.lookup_config(path)

        stdin_filename = ["--stdin-filename", path]

        out = self._run_ruff(content, path, ruff_config.path, ["format"] + stdin_filename)
        # launch check fix to sort imports
        out = self._run_ruff(out, path, ruff_config.path, ["check", "--fix"] + stdin_filename)
        return StylerOutput(out, ruff_config)


@_register
class ClangFormat(cfg.ConfigMixin):
    kind: tp.ClassVar = StylerKind.CPP
    suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (".cpp", ".cc", ".C", ".c", ".cxx", ".h", ".hh", ".hpp", ".H")

    def __init__(self, styler_opts: StylerOptions) -> None:
        self._tool: str = yalibrary.tools.tool("clang-format-18")  # type: ignore
        super().__init__(
            styler_opts.config_loaders
            if styler_opts.config_loaders
            else (
                cfg.AutoincludeConfig.make(const.CppLinterName.ClangFormat),
                cfg.DefaultConfig(
                    linter_name=const.CppLinterName.ClangFormat,
                    defaults_file=const.DefaultLinterConfig.Cpp,
                    resource_name="config.clang-format",
                ),
            )
        )

    def format(self, path: PurePath, content: str) -> StylerOutput:
        if path.suffix == ".h":
            content = self.fix_header(content)

        config = self.lookup_config(path)
        p = subprocess.Popen(
            [self._tool, "-assume-filename=a.cpp", f"-style=file:{config.path}"],
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

        return StylerOutput(out + "\n" if out and not out.endswith("\n") else out, config)

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
    kind: tp.ClassVar = StylerKind.CUDA
    suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (".cu", ".cuh")


@_register
class Golang:
    kind: tp.ClassVar = StylerKind.GO
    suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (".go",)

    def __init__(self, styler_opts: StylerOptions) -> None:
        self._tool: str = yalibrary.tools.tool("yoimports")  # type: ignore

    def format(self, path: PurePath, content: str) -> StylerOutput:
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

        return StylerOutput(out)


@_register
class YaMake:
    kind: tp.ClassVar = StylerKind.YAMAKE
    suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = ("ya.make", "ya.make.inc")

    def __init__(self, styler_opts: StylerOptions) -> None:
        pass

    def format(self, path: PurePath, content: str) -> StylerOutput:
        yamake = yalibrary.makelists.from_str(content)
        return StylerOutput(yamake.dump())


@_register
class Yql:
    kind: tp.ClassVar = StylerKind.YQL
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

    def format(self, path: PurePath, content: str) -> StylerOutput:
        return StylerOutput(self._run_format(path, content))
