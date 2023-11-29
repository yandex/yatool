PY3_LIBRARY()

PY_SRCS(
    NAMESPACE ide.gradle
    __init__.py
)

# TODO: Cleanup
PEERDIR(
    contrib/python/six
    contrib/python/termcolor
    contrib/python/pygtrie
    devtools/msvs
    devtools/ya/build
    devtools/ya/core
    devtools/ya/exts
    devtools/ya/ide/templates
    devtools/ya/jbuild
    devtools/ya/jbuild/idea_templates
    devtools/ya/yalibrary/display
    devtools/ya/yalibrary/find_root
    devtools/ya/yalibrary/graph
    devtools/ya/yalibrary/makelists
    devtools/ya/yalibrary/platform_matcher
    devtools/ya/yalibrary/qxml
    devtools/ya/yalibrary/tools
    devtools/ya/yalibrary/vcs
)

END()
