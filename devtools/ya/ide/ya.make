PY3_LIBRARY()

PEERDIR(
    contrib/python/six
    contrib/python/termcolor
    contrib/python/pygtrie
    devtools/ya/app
    devtools/ya/build
    devtools/ya/core/common_opts
    devtools/ya/core/config
    devtools/ya/core/event_handling
    devtools/ya/core/resource
    devtools/ya/core/yarg
    devtools/ya/exts
    devtools/ya/ide/gradle
    devtools/ya/ide/templates
    devtools/ya/jbuild
    devtools/ya/jbuild/idea_templates
    devtools/ya/test/const
    devtools/ya/yalibrary/display
    devtools/ya/yalibrary/find_root
    devtools/ya/yalibrary/graph
    devtools/ya/yalibrary/makelists
    devtools/ya/yalibrary/platform_matcher
    devtools/ya/yalibrary/qxml
    devtools/ya/yalibrary/tools
    devtools/ya/yalibrary/vcs
)

PY_SRCS(
    clion2016.py
    gdb_wrapper.py
    goland.py
    ide_common.py
    idea.py
    pycharm.py
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
    sync.py.template /clion/sync.py
)

END()

RECURSE(
    fsnotifier
    yigck
)

RECURSE_FOR_TESTS(
    tests_gradle
)
