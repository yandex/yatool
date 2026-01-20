import devtools.ya.test.const
from .matcher import stringize_platform

# Import core functions that don't require external dependencies
from .platform_params_core import (  # noqa: F401
    make_platform_params,
    make_yamake_options,
    transform_toolchain,
    get_target_platform_alias,
)


def stringize_toolchain(tc):
    platform = []
    platform.append(stringize_platform(tc['platform']['target']).lower())
    if tc.get('build_type'):
        platform.append(tc['build_type'])
    if tc.get('run_tests', False):
        platform.append('tests')
    if 'test_size_filters' in tc:
        for size in sorted(tc['test_size_filters']):
            platform.append('test-size={}'.format(size))
    if 'test_class_filters' in tc:
        if {devtools.ya.test.const.SuiteClassType.REGULAR} == set(tc['test_class_filters']):
            platform.append('regular-tests')
    if 'test_type_filters' in tc:
        for test_type in sorted(tc['test_type_filters']):
            platform.append('test-type={}'.format(test_type))
    flags = tc.get('flags', {})
    for k in sorted(flags):
        platform.append('{}={}'.format(k, flags[k]))

    return ','.join(platform)
