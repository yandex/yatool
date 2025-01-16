PY3_LIBRARY()

PY_SRCS(
    run_fuzz_result_node.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/test/util
    devtools/ya/yalibrary/vcs/arc
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/yalibrary/svn
    )
ENDIF()

END()
