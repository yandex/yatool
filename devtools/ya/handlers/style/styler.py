from __future__ import annotations

import logging
import os
import re
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
    LUA = auto()
    JSON = auto()
    YAML = auto()
    EOL = auto()

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
    kind: tp.ClassVar[StylerKind]
    name: tp.ClassVar[str]
    # Sequence of strings upon which the proper styler is selected
    suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]]

    def __init__(self, styler_opts: StylerOptions) -> None: ...

    def format(self, path: PurePath, content: str) -> StylerOutput:
        """Format and return output"""
        ...


class ConfigurableStyler(Styler, tp.Protocol):
    _config_finder: cfg.ConfigFinder


def is_configurable(styler: Styler) -> tp.TypeGuard[ConfigurableStyler]:
    return hasattr(styler, '_config_finder')


@_register
class Black:
    kind: tp.ClassVar = StylerKind.PY
    name: tp.ClassVar = const.PythonLinterName.Black
    suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (".py",)

    def __init__(self, styler_opts: StylerOptions) -> None:
        self._tool: str = yalibrary.tools.tool("black" if not styler_opts.py2 else "black_py2")  # type: ignore
        self._config_finder = cfg.ConfigFinder(
            styler_opts.config_loaders
            if styler_opts.config_loaders
            else (
                cfg.AutoincludeConfig.make(const.PythonLinterName.Black),
                cfg.DefaultConfig(
                    linter_name=const.PythonLinterName.Black,
                    defaults_file=const.DefaultLinterConfig.Python,
                    resource_name="pyproject.toml",
                ),
            )
        )

    def _run_black(self, content: str, path: PurePath) -> StylerOutput:
        config = self._config_finder.lookup_config(path)
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
class Ruff:
    kind: tp.ClassVar = StylerKind.PY
    name: tp.ClassVar = const.PythonLinterName.Ruff
    suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (".py",)

    def __init__(self, styler_opts: StylerOptions) -> None:
        self._tool: str = yalibrary.tools.tool("ruff")  # type: ignore
        self._config_finder = cfg.ConfigFinder(
            styler_opts.config_loaders
            if styler_opts.config_loaders
            else (
                cfg.AutoincludeConfig.make(const.PythonLinterName.Ruff),
                cfg.DefaultConfig(
                    linter_name=const.PythonLinterName.Ruff,
                    defaults_file=const.DefaultLinterConfig.Python,
                    resource_name="ruff.toml",
                ),
            )
        )

    def _run_ruff(self, content: str, path: PurePath, config_path: cfg.ConfigPath, cmd_args: list[str]) -> str:
        ruff_args = [self._tool] + cmd_args + ["--config", config_path, "-"]

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
        ruff_config = self._config_finder.lookup_config(path)

        stdin_filename = ["--stdin-filename", str(path)]

        # XXX: We first run `ruff format`. It either formats successfully or fails and returns
        # non-zero exit code and error message which we print and then abort by raising StylingError.
        # If it succeeds, we run `ruff check --fix-only`. It always returns zero code.
        # So we catch and show only critical errors such as SyntaxError but don't notify about any linting errors.
        out = self._run_ruff(content, path, ruff_config.path, ["format"] + stdin_filename)
        # launch check fix to sort imports
        out = self._run_ruff(out, path, ruff_config.path, ["check", "--fix-only"] + stdin_filename)
        return StylerOutput(out, ruff_config)


@_register
class ClangFormat:
    kind: tp.ClassVar = StylerKind.CPP
    name: tp.ClassVar = const.CppLinterName.ClangFormat
    suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (".cpp", ".cc", ".C", ".c", ".cxx", ".h", ".hh", ".hpp", ".H")

    def __init__(self, styler_opts: StylerOptions) -> None:
        self._tool: str = yalibrary.tools.tool("clang-format")  # type: ignore
        self._config_finder = cfg.ConfigFinder(
            styler_opts.config_loaders
            if styler_opts.config_loaders
            else (
                cfg.AutoincludeConfig.make(const.CppLinterName.ClangFormat),
                cfg.DefaultConfig(
                    linter_name=const.CppLinterName.ClangFormat,
                    defaults_file=const.DefaultLinterConfig.Cpp,
                    resource_name=".clang-format",
                ),
            )
        )

    def format(self, path: PurePath, content: str) -> StylerOutput:
        if path.suffix == ".h":
            p = str(path)
            if 'yql/essentials/parser/pg_catalog' in p or 'yql/essentials/parser/pg_wrapper' in p:
                # HACK: (DEVTOOLSSUPPORT-71462) Hardcode until introduction of custom settings in YA-2732
                pass
            else:
                content = self.fix_header(content)

        config = self._config_finder.lookup_config(path)
        p = subprocess.Popen(
            [self._tool, f"-assume-filename={path.name}", f"-style=file:{config.path}"],
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
    name: tp.ClassVar = const.CppLinterName.ClangFormat
    suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (".cu", ".cuh")


@_register
class ClangFormatYT(ClangFormat):
    name: tp.ClassVar = const.CppLinterName.ClangFormatYT

    def __init__(self, styler_opts: StylerOptions) -> None:
        self._tool: str = yalibrary.tools.tool("ads-clang-format")  # type: ignore
        self._config_finder = cfg.ConfigFinder(
            (
                styler_opts.config_loaders
                if styler_opts.config_loaders
                else (cfg.AutoincludeConfig.make(const.CppLinterName.ClangFormatYT),)
            ),
        )


@_register
class ClangFormat15(ClangFormat):
    name: tp.ClassVar = const.CppLinterName.ClangFormat15

    def __init__(self, styler_opts: StylerOptions) -> None:
        self._tool: str = yalibrary.tools.tool("clang-format-15")  # type: ignore
        self._config_finder = cfg.ConfigFinder(
            (
                styler_opts.config_loaders
                if styler_opts.config_loaders
                else (cfg.AutoincludeConfig.make(const.CppLinterName.ClangFormat15),)
            ),
        )


@_register
class ClangFormat18Vanilla(ClangFormat):
    name: tp.ClassVar = const.CppLinterName.ClangFormat18Vanilla

    def __init__(self, styler_opts: StylerOptions) -> None:
        self._tool: str = yalibrary.tools.tool("clang-format-18-vanilla")  # type: ignore
        self._config_finder = cfg.ConfigFinder(
            (
                styler_opts.config_loaders
                if styler_opts.config_loaders
                else (cfg.AutoincludeConfig.make(const.CppLinterName.ClangFormat18Vanilla),)
            ),
        )


@_register
class Golang:
    kind: tp.ClassVar = StylerKind.GO
    name: tp.ClassVar = 'yoimports'
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
    name: tp.ClassVar = 'yamake'
    suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = ("ya.make", "ya.make.inc")

    def __init__(self, styler_opts: StylerOptions) -> None:
        pass

    def format(self, path: PurePath, content: str) -> StylerOutput:
        yamake = yalibrary.makelists.from_str(content)
        return StylerOutput(yamake.dump())


@_register
class Yql:
    kind: tp.ClassVar = StylerKind.YQL
    name: tp.ClassVar = 'yql'
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


@_register
class StyLua:
    kind: tp.ClassVar = StylerKind.LUA
    name: tp.ClassVar = 'stylua'
    suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (".lua",)

    def __init__(self, styler_opts: StylerOptions) -> None:
        self._tool: str = yalibrary.tools.tool("stylua")  # type: ignore

    def _run_format(self, path: PurePath, content: str) -> str:
        args = [self._tool, '--stdin-filepath', str(path), '-']

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
            raise StylingError('error while running stylua on file "{}": {}'.format(path, err.strip()))

        return out

    def format(self, path: PurePath, content: str) -> StylerOutput:
        return StylerOutput(self._run_format(path, content))


@_register
class ClangFormatJson(ClangFormat):
    kind: tp.ClassVar = StylerKind.JSON
    name: tp.ClassVar = const.CustomExplicitLinterName.ClangFormatJson
    suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (".json",)

    def __init__(self, styler_opts: StylerOptions) -> None:
        self._tool: str = yalibrary.tools.tool("clang-format")  # type: ignore
        self._config_finder = cfg.ConfigFinder(
            (
                styler_opts.config_loaders
                if styler_opts.config_loaders
                else (
                    cfg.DefaultConfig(
                        linter_name=const.CustomExplicitLinterName.ClangFormatJson,
                        defaults_file=const.DefaultLinterConfig.Json,
                        resource_name=".clang-format-json",
                    ),
                )
            ),
        )


@_register
class YamlFmt:
    kind: tp.ClassVar = StylerKind.YAML
    name: tp.ClassVar = const.CustomExplicitLinterName.Yamlfmt
    suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (".yaml", ".yml")

    def __init__(self, styler_opts: StylerOptions) -> None:
        self._tool: str = yalibrary.tools.tool("yamlfmt")  # type: ignore
        self._config_finder = cfg.ConfigFinder(
            (
                styler_opts.config_loaders
                if styler_opts.config_loaders
                else (
                    cfg.DefaultConfig(
                        linter_name=const.CustomExplicitLinterName.Yamlfmt,
                        defaults_file=const.DefaultLinterConfig.Yaml,
                        resource_name=".yamlfmt.yml",
                    ),
                )
            ),
        )

    def _run_format(self, path: PurePath, content: str) -> StylerOutput:
        config = self._config_finder.lookup_config(path)
        args = [self._tool, '-conf', config.path, '-']

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
            raise StylingError('error while running yamlfmt on file "{}": {}'.format(path, err.strip()))

        return StylerOutput(out, config)

    def format(self, path: PurePath, content: str) -> StylerOutput:
        return self._run_format(path, content)


@_register
class EOLFmt:
    kind: tp.ClassVar = StylerKind.EOL
    name: tp.ClassVar = 'eolfmt'
    suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (
        '.cpp',
        '.h',
        '.hpp',
        '.json',
        '.ini',
        '.md',
        '.py',
        '.sql',
        '.txt',
        '.xml',
        '.yaml',
        '.yml',
        'Dockerfile',
        'Makefile',
    )

    def __init__(self, styler_opts: StylerOptions) -> None:
        self._pattern = re.compile(r'[ \t]+$', re.MULTILINE)

    def _run_format(self, path: PurePath, content: str) -> StylerOutput:
        out = re.sub(self._pattern, '', content)
        return StylerOutput(out)

    def format(self, path: PurePath, content: str) -> StylerOutput:
        return self._run_format(path, content)
