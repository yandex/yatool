PY23_LIBRARY()

PY_SRCS(
    NAMESPACE core.common_opts
    __init__.py
)

PEERDIR(
    contrib/python/six

    devtools/ya/core/config
    devtools/ya/core/profiler
    devtools/ya/core/stages_profiler
    devtools/ya/core/yarg
    devtools/ya/exts
    devtools/ya/test/const
    devtools/ya/yalibrary/upload/consts
    devtools/ya/yalibrary/platform_matcher
    devtools/ya/yalibrary/tools
)

STYLE_PYTHON()

END()
