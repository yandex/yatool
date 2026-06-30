PY3_LIBRARY()

PY_SRCS(
    NAMESPACE package
    __init__.py
    aar.py
    artifactory.py
    debian.py
    docker.py
    fs_util.py
    noconffiles.py
    npm.py
    packager.py
    postprocessor.py
    process.py
    rpm.py
    source.py
    squashfs.py
    tarball.py
    vcs.py
    wheel.py
    utils.py
    debian_uploader/__init__.py
    debian_uploader/uploader_curl.py
    debian_uploader/uploader_dist.py
    debian_uploader/uploader_dist2.py
    package_tree/__init__.py
    package_tree/consts.py
    package_tree/tree.py
    package_tree/visitor.py
    package_tree/loader.py
)

RESOURCE_FILES(
    package.schema.json
)

PEERDIR(
    contrib/python/jsonschema
    contrib/python/pathlib2
    contrib/python/path.py
    contrib/python/six
    devtools/ya/app_config
    devtools/ya/build
    devtools/ya/core/common_opts
    devtools/ya/core/config
    devtools/ya/core/error
    devtools/ya/core/profiler
    devtools/ya/core/stage_tracer
    devtools/ya/core/yarg
    devtools/ya/exts
    devtools/ya/handlers/package/opts
    devtools/ya/package/const
    devtools/ya/yalibrary/find_root
    devtools/ya/yalibrary/term
    devtools/ya/yalibrary/tools
    devtools/ya/yalibrary/vcs
    devtools/ya/yalibrary/vcs/vcsversion
    devtools/ya/yalibrary/yandex/sandbox/misc
    library/python/compress
    library/python/resource
    library/python/strings
)

IF (NOT YA_OPENSOURCE)
    PY_SRCS(
        NAMESPACE package
        sandbox_source.py
        sandbox_postprocessor.py
    )
    PEERDIR(
        devtools/ya/yalibrary/checkout
        devtools/ya/yalibrary/upload
        devtools/ya/yalibrary/yandex/sandbox
    )
ENDIF()

IF (NOT OS_WINDOWS)
    PEERDIR(
        contrib/python/python-magic
    )
ENDIF()

END()

RECURSE_FOR_TESTS(
    tests
)
