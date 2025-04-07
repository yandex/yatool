import devtools.ya.test.dependency.testdeps as testdeps

import devtools.ya.test.const

import typing as tp

if tp.TYPE_CHECKING:
    from devtools.ya.test.test_types.common import AbstractTestSuite  # noqa


CMD_FILE_START_MARKER = "--ya-start-command-file"
CMD_FILE_END_MARKER = "--ya-end-command-file"


def get_base_environment_relative_options(suite):
    return [
        "--source-root",
        "$(SOURCE_ROOT)",
        "--build-root",
        "$(BUILD_ROOT)",
        "--test-suite-name",
        suite.name,
        "--project-path",
        suite.project_path,
    ]


def get_environment_relative_options(suite, opts):
    # type: (AbstractTestSuite, tp.Any) -> list[str]
    options = get_base_environment_relative_options(suite)

    for path in testdeps.get_test_related_paths(suite, "$(SOURCE_ROOT)", opts):
        options += ["--test-related-path", path]

    if not suite.supports_clean_environment or devtools.ya.test.const.YaTestTags.Dirty in suite.tags:
        options += ["--no-clean-environment"]

    if suite.target_platform_descriptor:
        options += ["--target-platform-descriptor", suite.target_platform_descriptor]

    if suite.multi_target_platform_run:
        options += ["--multi-target-platform-run"]

    if suite.need_wine():
        options += ["--with-wine"]

    return options


def wrap_test_tool_cmd(cmd, handler):
    assert handler in cmd, "Handler not found in command"
    counter = 0
    for item in cmd:
        counter += 1
        if item == handler:
            break
    cmd.insert(counter, CMD_FILE_START_MARKER)
    cmd.append(CMD_FILE_END_MARKER)


def wrap_with_cmd_file_markers(args):
    if not args:
        return args
    return [CMD_FILE_START_MARKER] + args + [CMD_FILE_END_MARKER]
