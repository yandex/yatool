PY23_LIBRARY()

PEERDIR(
    contrib/python/humanfriendly
)

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE yalibrary.fetcher.progress_info
    __init__.py
)

END()
