PY23_LIBRARY()

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

IF (PYTHON3)
    PEERDIR(
        devtools/ya/ide/gradle
    )
ENDIF()

PY_SRCS(
    NAMESPACE ide
    clion2016.py
    gdb_wrapper.py
    goland.py
    ide_common.py
    idea.py
    msbuild.py
    msvs.py
    msvs_lite.py
    msvs_lite_utils.py
    pycharm.py
    qt.py
    remote_ide_qt.py
    venv/__init__.py
    venv/project.py
    vscode/__init__.py
    vscode/common.py
    vscode/configurations.py
    vscode/consts.py
    vscode/dump.py
    vscode/excludes.py
    vscode/graph.py
    vscode/opts.py
    vscode/tasks.py
    vscode/workspace.py
    vscode_clangd.py
    vscode_all.py
    vscode_go.py
    vscode_py.py
    vscode_ts.py
)

RESOURCE(
    msvs_lite_settings.template /msvs/settings/msvs_lite.vssettings
    sync.py.template /clion/sync.py
)

END()

RECURSE(
    fsnotifier
)
