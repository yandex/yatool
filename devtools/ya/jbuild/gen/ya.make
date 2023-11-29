PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE jbuild.gen
    gen.py
)

PEERDIR(
    devtools/ya/build/targets
    devtools/ya/exts
    devtools/ya/jbuild/gen/actions
    devtools/ya/jbuild/gen/base
    devtools/ya/jbuild/gen/configure
    devtools/ya/jbuild/gen/consts
    devtools/ya/jbuild/gen/java_target2
    devtools/ya/jbuild/gen/makelist_parser2
    devtools/ya/jbuild/gen/node
    devtools/ya/yalibrary/graph
    devtools/ya/yalibrary/platform_matcher
    devtools/ya/yalibrary/vcs
    devtools/ya/yalibrary/vcs/vcsversion
)

END()

RECURSE(
    actions
    base
    configure
    consts
    java_target2
    makelist_parser2
    node
    tests
)
