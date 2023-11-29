PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE jbuild
    execute.py
    jbuild_opts.py
    java_build2.py
    maven/fetcher.py
    maven/pom_parser.py
    maven/version_filter.py
    maven/maven_import.py
    resolve_java_srcs.py
)

PEERDIR(
    contrib/python/Jinja2
    devtools/ya/build
    devtools/ya/core
    devtools/ya/exts
    devtools/ya/jbuild/commands
    devtools/ya/jbuild/gen
    devtools/ya/handlers/dump
    devtools/ya/test/const
    devtools/ya/test/opts
    devtools/ya/yalibrary/graph
    devtools/ya/yalibrary/makelists
    devtools/ya/yalibrary/platform_matcher
    devtools/ya/yalibrary/rglob
    devtools/ya/yalibrary/tools
    devtools/ya/yalibrary/vcs
    devtools/ya/yalibrary/vcs/vcsversion
)

RESOURCE(
    maven/maven_import_tmp_ya.make.jinja maven_import_tmp/ya.make.jinja
)

END()

RECURSE(
    commands
    gen
    maven
)
