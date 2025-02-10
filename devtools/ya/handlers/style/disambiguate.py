import functools
import typing as tp
from pathlib import PurePath

import devtools.ya.test.const as const
from . import config as cfg


if tp.TYPE_CHECKING:
    from . import target
    from . import styler


class AmbiguityError(Exception):
    pass


def _with_config_to_styler_cache(func):
    cache: dict[cfg.Config, "type[styler.Styler]"] = {}

    @functools.wraps(func)
    def inner(*args, **kwargs):
        return func(cache, *args, **kwargs)

    return inner


@_with_config_to_styler_cache
def black_vs_ruff(
    cache: dict[cfg.Config, "type[styler.Styler]"],
    target: PurePath,
    *,
    black_cls: "type[styler.Black]",
    ruff_cls: "type[styler.Ruff]",
    mine_opts: "target.MineOptions",
    autoinclude_files: tuple[str, ...] = const.AUTOINCLUDE_PATHS,
):
    if mine_opts.use_ruff:
        return ruff_cls

    ruff_config = cfg.AutoincludeConfig.make(const.PythonLinterName.Ruff, autoinclude_files=autoinclude_files).lookup(
        target
    )
    black_config = cfg.AutoincludeConfig.make(const.PythonLinterName.Black, autoinclude_files=autoinclude_files).lookup(
        target
    )

    # by presence of the config
    if not ruff_config and not black_config:
        return black_cls
    if ruff_config and not black_config:
        return ruff_cls
    if black_config and not ruff_config:
        return black_cls

    assert ruff_config and black_config  # static checker is not happy without this line

    if ruff_config.path != black_config.path:
        raise AmbiguityError(
            f"Can't choose between Black and Ruff, configs are present for both. "
            f"Ruff config: {ruff_config.path}, Black config: {black_config.path}. "
            "Provide style option to disambiguate."
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
            cache[ruff_config] = ruff_cls
            return cache[ruff_config]
        else:
            cache[ruff_config] = black_cls
            return cache[ruff_config]
    else:
        raise AmbiguityError(
            f"Can't choose between Black and Ruff. "
            f"Don't know how to disambiguate using config: {ruff_config.path}. "
        )
