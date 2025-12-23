PY3_LIBRARY()

PY_SRCS(
    execute.py
    jbuild_opts.py
    maven/fetcher.py
    maven/license.py
    maven/pom_parser.py
    maven/version_filter.py
    maven/maven_import.py
    maven/legacy.py
    maven/unified.py
    maven/utils.py
)

PEERDIR(
    contrib/python/Jinja2
    devtools/ya/build
    devtools/ya/core/common_opts
    devtools/ya/core/config
    devtools/ya/core/yarg
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
