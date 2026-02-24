from __future__ import annotations

import fnmatch
import functools
import logging
import os
import re
import subprocess
import sys
import typing as tp
import yaml
from collections.abc import Sequence, Generator
from enum import StrEnum, auto
from pathlib import PurePath, Path

import devtools.ya.test.const as const
import exts.os2
import yalibrary.display
import yalibrary.makelists
import yalibrary.tools

import devtools.ya.handlers.style.config as cfg
import devtools.ya.handlers.style.state_helper as state_helper

if tp.TYPE_CHECKING:
    import devtools.ya.handlers.style.target as trgt

logger = logging.getLogger(__name__)
display = yalibrary.display.build_term_display(sys.stdout, exts.os2.is_tty())


_DISABLE_PATHS_WILDCARDS = "disable-paths-wildcards"


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
    EOF = auto()

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
_NAME_MAPPING: dict[str, set[type[Styler]]] = {}


def _register[T: Styler](cls: type[T]) -> type[T]:
    for suffix in cls.match_suffixes:
        _SUFFIX_MAPPING.setdefault(suffix, set()).add(cls)
    for name in cls.match_names:
        _NAME_MAPPING.setdefault(name, set()).add(cls)
    return cls


@functools.cache
def _read_linters_yaml_ignores(linters_yaml: Path) -> tuple[list[str], dict[type[Styler], list[str]]]:
    config = yaml.safe_load(linters_yaml.read_text())
    if not isinstance(config, dict):
        return [], {}
    linters = config.get("linters")
    if not linters:
        return [], {}
    common_ignores = linters.get(_DISABLE_PATHS_WILDCARDS, [])
    tool_ignores: dict[type[Styler], list[str]] = {}
    for name in _TAXI_NAME_TO_STYLER_MAP:
        if name in linters and (ignores := linters[name].get(_DISABLE_PATHS_WILDCARDS)):
            tool_ignores[_TAXI_NAME_TO_STYLER_MAP[name]] = ignores
    return common_ignores, tool_ignores


def _walk_linters_yaml(target: trgt.Target) -> Generator[Path]:
    if not (root := cfg.find_root()):
        return
    else:
        root = Path(root)
    # It's not PurePath because earlier we made sure it's not from stdin
    temp = tp.cast(Path, target.path)
    while temp != root and temp.parent != temp:
        config_path = temp.parent / "linters.yaml"
        if config_path.exists():
            yield config_path
        temp = temp.parent


def _apply_linters_yaml_ignores(
    target: trgt.Target,
    config_path: Path,
    stylers: set[type[Styler]],
    common_ignores: list[str],
    styler_ignores: dict[type[Styler], list[str]],
) -> set[type[Styler]]:
    target_path = target.path.relative_to(config_path.parent).as_posix()
    for pat in common_ignores:
        if fnmatch.fnmatch(target_path, pat):
            return set()
    filtered: set[type[Styler]] = set()
    for styler in stylers:
        if styler in styler_ignores:
            for pat in styler_ignores[styler]:
                if fnmatch.fnmatch(target_path, pat):
                    break
            else:
                filtered.add(styler)
        else:
            filtered.add(styler)
    return filtered


def select_suitable_stylers(
    target: trgt.Target,
    file_types: Sequence[StylerKind],
    enable_implicit_taxi_formatters: bool = False,
    paths_with_integrations: tuple[str, ...] = (),
) -> set[type[Styler]]:
    """Find and return matching styler class"""
    if "/canondata/" in target.path.as_posix() and not target.passed_directly:
        # Skip files inside canondata prior to other checks
        # TODO: Python3.13 Use pathlib.PurePath.full_match
        return set()

    matches = _SUFFIX_MAPPING.get(target.path.suffix, set()) | _NAME_MAPPING.get(target.path.name, set())
    if not matches:
        logger.warning('skip %s (sufficient styler not found)', target.path)
        return set()

    if file_types:
        stylers = {m for m in matches if m.kind in file_types}
        if not stylers:
            logger.warning('skip %s (filtered by file type)', target.path)
            return set()
    else:
        stylers = set()
        for m in matches:
            if m.kind.default_enabled:
                stylers.add(m)
            elif (
                enable_implicit_taxi_formatters
                and m.kind in (StylerKind.JSON, StylerKind.YAML, StylerKind.EOL)
                and 'taxi/' in target.path.as_posix()
            ):
                # HACK: Hardcode until introduction of custom settings in YA-2732
                stylers.add(m)
        if not stylers:
            options = ' or '.join(f'--{m.kind}' for m in matches)
            logger.warning('skip %s (require explicit %s or --all)', target.path, options)
            return set()

    if target.passed_directly:
        # For files passed directly filtering by ignore rules is not applied
        return stylers

    filtered: set[type[Styler]] = set()
    for styler in stylers:
        for pattern in styler.ignore:
            if target.path.match(pattern):
                # TODO: Python3.13 Use pathlib.PurePath.full_match
                logger.warning('skip %s (filtered by ignore rules)', target.path)
                break
        else:
            filtered.add(styler)

    target_s = target.path.as_posix()
    if not target.stdin and not target.passed_directly and any(p in target_s for p in paths_with_integrations):
        for linters_yaml in _walk_linters_yaml(target):
            ignores = _read_linters_yaml_ignores(linters_yaml)
            filtered2 = _apply_linters_yaml_ignores(target, linters_yaml, filtered, *ignores)
            if ClangFormat in filtered and ClangFormat not in filtered2:
                # XXX: We assume that linters.yaml can only contain common clang-format settings.
                # So we apply it to every other clang-format flavor
                filtered2 -= {ClangFormatYT, ClangFormat18Vanilla, ClangFormat15}

            filtered = filtered2
            if not filtered:
                break

    return filtered


class Styler(tp.Protocol):
    kind: tp.ClassVar
    name: tp.ClassVar[str]
    # Settings upon which the suitable stylers are selected
    match_suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]]
    match_names: tp.ClassVar[tuple[tp.LiteralString, ...]]
    # Goes to pathlib.PurePath.match TODO: Python3.13 Use pathlib.PurePath.full_match
    ignore: tp.ClassVar[tuple[tp.LiteralString, ...]]

    def __init__(self, styler_opts: StylerOptions) -> None: ...

    def format(self, path: PurePath, content: str, stdin: bool) -> StylerOutput:
        """
        Format and return output. The function mustn't change the file.
        It should only return the formatted content.

        `path` is a PurePath when it's passed with --stdin-filename option,
        otherwise it's a Path
        `content` is a file content or content read from stdin
        `stdin` signifies that the input comes from stdin
        """
        ...


class ConfigurableStyler(Styler, tp.Protocol):
    config_finder: cfg.ConfigFinder


def is_configurable(styler: Styler) -> tp.TypeGuard[ConfigurableStyler]:
    return hasattr(styler, 'config_finder')


@functools.cache
def init_styler_cached(cls: type[Styler], opts: StylerOptions):
    return cls(opts)


@_register
class Black:
    kind: tp.ClassVar = StylerKind.PY
    name: tp.ClassVar = const.PythonLinterName.Black
    match_suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (".py",)
    match_names: tp.ClassVar[tuple[tp.LiteralString, ...]] = ()
    ignore: tp.ClassVar[tuple[tp.LiteralString, ...]] = ()

    def __init__(self, styler_opts: StylerOptions) -> None:
        self._tool: str = yalibrary.tools.tool("black" if not styler_opts.py2 else "black_py2")  # type: ignore
        self.config_finder = cfg.ConfigFinder(
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
        config = self.config_finder.lookup_config(path)
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

    def format(self, path: PurePath, content: str, stdin: bool) -> StylerOutput:
        return self._run_black(content, path)


@_register
class Ruff:
    kind: tp.ClassVar = StylerKind.PY
    name: tp.ClassVar = const.PythonLinterName.Ruff
    match_suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (".py",)
    match_names: tp.ClassVar[tuple[tp.LiteralString, ...]] = ()
    ignore: tp.ClassVar[tuple[tp.LiteralString, ...]] = ()

    def __init__(self, styler_opts: StylerOptions) -> None:
        self._tool: str = yalibrary.tools.tool("ruff")  # type: ignore
        self.config_finder = cfg.ConfigFinder(
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

    def format(self, path: PurePath, content: str, stdin: bool) -> StylerOutput:
        ruff_config = self.config_finder.lookup_config(path)

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
    match_suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (
        ".cpp",
        ".cc",
        ".C",
        ".c",
        ".cxx",
        ".h",
        ".hh",
        ".hpp",
        ".H",
    )
    match_names: tp.ClassVar[tuple[tp.LiteralString, ...]] = ()
    ignore: tp.ClassVar[tuple[tp.LiteralString, ...]] = ()

    def __init__(self, styler_opts: StylerOptions) -> None:
        self._tool: str = yalibrary.tools.tool("clang-format")  # type: ignore
        self.config_finder = cfg.ConfigFinder(
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

    def format(self, path: PurePath, content: str, stdin: bool) -> StylerOutput:
        if path.suffix == ".h":
            p = path.as_posix()
            if 'yql/essentials/parser/pg_catalog' in p or 'yql/essentials/parser/pg_wrapper' in p:
                # HACK: (DEVTOOLSSUPPORT-71462) Hardcode until introduction of custom settings in YA-2732
                # TODO: Python3.13 Move to `ignore` and use pathlib.PurePath.full_match
                pass
            else:
                content = self.fix_header(content)

        config = self.config_finder.lookup_config(path)
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
    match_suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (".cu", ".cuh")


@_register
class ClangFormatYT(ClangFormat):
    name: tp.ClassVar = const.CppLinterName.ClangFormatYT

    def __init__(self, styler_opts: StylerOptions) -> None:
        self._tool: str = yalibrary.tools.tool("ads-clang-format")  # type: ignore
        self.config_finder = cfg.ConfigFinder(
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
        self.config_finder = cfg.ConfigFinder(
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
        self.config_finder = cfg.ConfigFinder(
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
    match_suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (".go",)
    match_names: tp.ClassVar[tuple[tp.LiteralString, ...]] = ()
    ignore: tp.ClassVar[tuple[tp.LiteralString, ...]] = ()

    def __init__(self, styler_opts: StylerOptions) -> None:
        self._tool: str = yalibrary.tools.tool("yoimports")  # type: ignore

    def format(self, path: PurePath, content: str, stdin: bool) -> StylerOutput:
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
    match_suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = ()
    match_names: tp.ClassVar[tuple[tp.LiteralString, ...]] = ("ya.make", "ya.make.inc")
    ignore: tp.ClassVar[tuple[tp.LiteralString, ...]] = ()

    def __init__(self, styler_opts: StylerOptions) -> None:
        pass

    def format(self, path: PurePath, content: str, stdin: bool) -> StylerOutput:
        yamake = yalibrary.makelists.from_str(content)
        return StylerOutput(yamake.dump())


@_register
class Yql:
    kind: tp.ClassVar = StylerKind.YQL
    name: tp.ClassVar = 'yql'
    match_suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (".yql",)
    match_names: tp.ClassVar[tuple[tp.LiteralString, ...]] = ()
    ignore: tp.ClassVar[tuple[tp.LiteralString, ...]] = ()

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

    def format(self, path: PurePath, content: str, stdin: bool) -> StylerOutput:
        return StylerOutput(self._run_format(path, content))


@_register
class StyLua:
    kind: tp.ClassVar = StylerKind.LUA
    name: tp.ClassVar = 'stylua'
    match_suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (".lua",)
    match_names: tp.ClassVar[tuple[tp.LiteralString, ...]] = ()
    ignore: tp.ClassVar[tuple[tp.LiteralString, ...]] = ()

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

    def format(self, path: PurePath, content: str, stdin: bool) -> StylerOutput:
        return StylerOutput(self._run_format(path, content))


@_register
class ClangFormatJson(ClangFormat):
    kind: tp.ClassVar = StylerKind.JSON
    name: tp.ClassVar = const.CustomExplicitLinterName.ClangFormatJson
    match_suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (".json",)
    match_names: tp.ClassVar[tuple[tp.LiteralString, ...]] = ()
    ignore: tp.ClassVar[tuple[tp.LiteralString, ...]] = ()

    def __init__(self, styler_opts: StylerOptions) -> None:
        self._tool: str = yalibrary.tools.tool("clang-format")  # type: ignore
        self.config_finder = cfg.ConfigFinder(
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
    match_suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (".yaml", ".yml")
    match_names: tp.ClassVar[tuple[tp.LiteralString, ...]] = ()
    ignore: tp.ClassVar[tuple[tp.LiteralString, ...]] = ("a.yaml", "t.yaml")

    def __init__(self, styler_opts: StylerOptions) -> None:
        self._tool: str = yalibrary.tools.tool("yamlfmt")  # type: ignore
        self.config_finder = cfg.ConfigFinder(
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

    def _run_format(self, path: PurePath, content: str, stdin: bool) -> StylerOutput:
        config = self.config_finder.lookup_config(path)
        if stdin:
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
        else:
            # XXX: yamlfmt doesn't support -stdin_filename, include/exclude
            # config settings don't work if the input comes from stdin so we
            # first have to check if the modification would happen. If yes, then
            # we get the formatted content by passing it to yamlfmt's stdin.
            # If no, then we might as well return the original file content.
            args = [self._tool, '-conf', config.path, '-dry', '-q', path]
            p = subprocess.run(
                args,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            if not p.stdout:
                out, err = content, p.stderr
            elif not p.returncode:
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

    def format(self, path: PurePath, content: str, stdin: bool) -> StylerOutput:
        return self._run_format(path, content, stdin)


@_register
class EOLFmt:
    kind: tp.ClassVar = StylerKind.EOL
    name: tp.ClassVar = 'eolfmt'
    match_suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (
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
    )
    match_names: tp.ClassVar[tuple[tp.LiteralString, ...]] = ('Dockerfile', 'Makefile')
    ignore: tp.ClassVar[tuple[tp.LiteralString, ...]] = ()

    def __init__(self, styler_opts: StylerOptions) -> None:
        self._pattern = re.compile(r'[ \t]+$', re.MULTILINE)

    def _run_format(self, path: PurePath, content: str) -> StylerOutput:
        out = re.sub(self._pattern, '', content)
        return StylerOutput(out)

    def format(self, path: PurePath, content: str, stdin: bool) -> StylerOutput:
        return self._run_format(path, content)


@_register
class EOFFmt:
    kind: tp.ClassVar = StylerKind.EOF
    name: tp.ClassVar = 'eoffmt'
    match_suffixes: tp.ClassVar[tuple[tp.LiteralString, ...]] = (
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
    )
    match_names: tp.ClassVar[tuple[tp.LiteralString, ...]] = ('Dockerfile', 'Makefile')
    ignore: tp.ClassVar[tuple[tp.LiteralString, ...]] = ()

    def format(self, path: PurePath, content: str, stdin: bool) -> StylerOutput:
        return StylerOutput(content + "\n" if content and not content.endswith("\n") else content)


_TAXI_NAME_TO_STYLER_MAP = {
    "ruff": Ruff,
    "black": Black,
    "clang-format": ClangFormat,
    "eolfmt": EOLFmt,
    "jsonfmt": ClangFormatJson,
    "yamlfmt": YamlFmt,
    "gofmt": Golang,
}
