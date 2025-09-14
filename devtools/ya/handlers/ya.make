PY3_LIBRARY(ya-lib)

SRCDIR(devtools/ya)

PEERDIR(
    devtools/ya/handlers/buf
    devtools/ya/handlers/cache
    devtools/ya/handlers/dump
    devtools/ya/handlers/gc
    devtools/ya/handlers/gen_config
    devtools/ya/handlers/ide
    devtools/ya/handlers/java
    devtools/ya/handlers/krevedko
    devtools/ya/handlers/make
    devtools/ya/handlers/maven_import
    devtools/ya/handlers/package
    devtools/ya/handlers/py
    devtools/ya/handlers/style
    devtools/ya/handlers/test
    devtools/ya/handlers/tool
    devtools/ya/handlers/analyze_make
    devtools/ya/handlers/run
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/handlers/__trace__
        devtools/ya/handlers/addremove
        devtools/ya/handlers/clang_tidy
        devtools/ya/handlers/code
        devtools/ya/handlers/completion
        devtools/ya/handlers/dctl
        devtools/ya/handlers/download
        devtools/ya/handlers/fix_includes
        devtools/ya/handlers/notify
        devtools/ya/handlers/paste
        devtools/ya/handlers/profile
        devtools/ya/handlers/project
        devtools/ya/handlers/svn
        devtools/ya/handlers/upload
        devtools/ya/handlers/vmctl
        devtools/ya/handlers/whoami
        devtools/ya/handlers/wine
        devtools/ya/handlers/vault
        devtools/ya/handlers/vim
        devtools/ya/handlers/curl
        devtools/ya/handlers/neovim
        devtools/ya/handlers/gdb
        devtools/ya/handlers/emacs
        devtools/ya/handlers/grep
        devtools/ya/handlers/jstyle
        devtools/ya/handlers/nile
        devtools/ya/handlers/sed
        devtools/ya/handlers/ydb
        devtools/ya/handlers/yql
    )
ENDIF()

END()

RECURSE(
    __trace__
    addremove
    analyze_make
    autocheck
    buf
    cache
    clang_tidy
    completion
    curl
    dctl
    download
    dump
    emacs
    # exec
    # fetch
    fix_includes
    gc
    gdb
    gen_config
    grep
    ide
    java
    jstyle
    krevedko
    make
    maven_import
    neovim
    nile
    notify
    package
    paste
    profile
    project
    py
    # remote_gdb
    # repo_check
    run
    sed
    # shell
    # stat
    style
    svn
    test
    tool
    upload
    vault
    vim
    vmctl
    # webide
    whoami
    wine
    ydb
    yql
)

RECURSE_FOR_TESTS(
    tests
)
