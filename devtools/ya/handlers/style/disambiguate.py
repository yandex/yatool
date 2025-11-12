import functools
import typing as tp
from collections.abc import Callable
from pathlib import PurePath

import devtools.ya.test.const as const
from . import config as cfg
from . import styler


type ConfigCache = dict[cfg.Config, type[styler.Styler]]


class DisambiguationOptions(tp.NamedTuple):
    use_ruff: bool = False
    use_clang_format_yt: bool = False
    use_clang_format_15: bool = False
    use_clang_format_18_vanilla: bool = False
    autoinclude_files: tuple[str, ...] = const.AUTOINCLUDE_PATHS


def _with_config_to_styler_cache[**P, R](func: Callable[tp.Concatenate[ConfigCache, P], R]) -> Callable[P, R]:
    cache: ConfigCache = {}

    @functools.wraps(func)
    def inner(*args: P.args, **kwargs: P.kwargs) -> R:
        return func(cache, *args, **kwargs)

    return inner


@_with_config_to_styler_cache
def _black_vs_ruff(
    cache: ConfigCache,
    target: PurePath,
    disambiguation_opts: DisambiguationOptions,
) -> type[styler.Styler] | str:
    if disambiguation_opts.use_ruff:
        return styler.Ruff

    ruff_config = cfg.AutoincludeConfig.make(
        const.PythonLinterName.Ruff, autoinclude_files=disambiguation_opts.autoinclude_files
    ).lookup(target)
    black_config = cfg.AutoincludeConfig.make(
        const.PythonLinterName.Black, autoinclude_files=disambiguation_opts.autoinclude_files
    ).lookup(target)

    # by presence of the config
    if not ruff_config and not black_config:
        return styler.Black
    if ruff_config and not black_config:
        return styler.Ruff
    if black_config and not ruff_config:
        return styler.Black

    assert ruff_config and black_config  # static checker is not happy without this line

    # longest path wins
    if ruff_config.path.parent == black_config.path.parent:
        # directory is relative to itself, skip this scenario
        pass
    elif ruff_config.path.parent.is_relative_to(black_config.path.parent):
        return styler.Ruff
    elif black_config.path.parent.is_relative_to(ruff_config.path.parent):
        return styler.Black

    if ruff_config.path != black_config.path:
        return (
            f"[{target}] Can't choose between Black and Ruff, configs are present for both. "
            f"Ruff config: {ruff_config.path}, Black config: {black_config.path}."
        )

    # from here ruff_config == black_config

    if ruff_config in cache:
        return cache[ruff_config]

    if ruff_config.path.name == "pyproject.toml":
        import tomllib

        config = tomllib.loads(ruff_config.path.read_text())

        # by existence of the [tool.ruff] and [tool.black] sections in pyproject.toml
        ruff_section = config.get("tool", {}).get("ruff")
        black_section = config.get("tool", {}).get("black")

        if ruff_section and not black_section:
            cache[ruff_config] = styler.Ruff
            return cache[ruff_config]
        else:
            cache[ruff_config] = styler.Black
            return cache[ruff_config]
    else:
        return (
            f"[{target}] Can't choose between Black and Ruff. "
            f"Don't know how to disambiguate using config: {ruff_config.path}. "
        )


def _clang_formats(
    disambiguation_opts: DisambiguationOptions,
) -> type[styler.Styler]:
    if disambiguation_opts.use_clang_format_yt:
        return styler.ClangFormatYT
    elif disambiguation_opts.use_clang_format_15:
        return styler.ClangFormat15
    elif disambiguation_opts.use_clang_format_18_vanilla:
        return styler.ClangFormat18Vanilla
    return styler.ClangFormat


def disambiguate_targets(
    target: PurePath,
    styler_classes: set[type[styler.Styler]],
    disambiguation_opts: DisambiguationOptions,
) -> type[styler.Styler] | str:
    if len(styler_classes) == 1:
        return next(iter(styler_classes))
    elif styler_classes == {styler.Black, styler.Ruff}:
        return _black_vs_ruff(target, disambiguation_opts)
    elif styler_classes == {
        styler.ClangFormat,
        styler.ClangFormatYT,
        styler.ClangFormat15,
        styler.ClangFormat18Vanilla,
    }:
        return _clang_formats(disambiguation_opts)

    raise AssertionError(f"[{target}] Can't choose between {' and '.join(m.__name__ for m in styler_classes)}.")
