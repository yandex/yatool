PY23_LIBRARY()

STYLE_PYTHON()

PEERDIR(
    contrib/python/six
    devtools/ya/core/error
    devtools/ya/build/evlog
)

PY_SRCS(
    NAMESPACE core.event_handling
    __init__.py
    base_subscriber.py
    common_subscribers.py
    event_queue.py
)

END()

RECURSE_FOR_TESTS(
    tests
)
