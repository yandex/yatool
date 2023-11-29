import devtools.ya.test.dependency.testdeps as testdeps

import test.const


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
    options = get_base_environment_relative_options(suite)

    for path in testdeps.get_test_related_paths(suite, "$(SOURCE_ROOT)", opts):
        options += ["--test-related-path", path]

    atd_paths = testdeps.get_test_data_paths(suite, testdeps.get_atd_root(opts))
    if atd_paths:
        options += ["--test-data-root", testdeps.get_atd_root(opts)]
        for path in atd_paths:
            options += ["--test-data-path", path]

    if not suite.supports_clean_environment or test.const.YaTestTags.Dirty in suite.tags:
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
    cmd.insert(counter, "--ya-start-command-file")
    cmd.append("--ya-end-command-file")
