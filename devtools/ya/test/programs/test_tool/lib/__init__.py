# coding: utf-8

import sys
import importlib


def get_tool_args():
    args = []
    for arg in sys.argv[2:]:
        if arg.startswith("@") and arg.endswith("args"):
            with open(arg[1:]) as cmdfile:
                args.extend(cmdfile.read().splitlines())
        else:
            args.append(arg)
    return args


def run():
    if len(sys.argv) < 2:
        sys.stderr.write('test_tool tool_name ...')
        exit(1)

    file_args = get_tool_args()

    sys.argv[:] = sys.argv[:2] + file_args
    tool_name = sys.argv[1]
    prefix = 'devtools.ya.test.programs.test_tool'
    module_name = "{0}.{1}.{1}".format(prefix, tool_name)

    profile = None
    if '--profile-test-tool' in sys.argv:
        sys.argv.remove('--profile-test-tool')

        import pstats
        import cProfile

        profile = cProfile.Profile()
        profile.enable()

    import faulthandler

    # Dump backtrace to the stderr in case of receiving fault signal
    faulthandler.enable()

    try:
        module = importlib.import_module(module_name)
    except ImportError:
        sys.stderr.write('Failed to import tool: {} (import name - {})\n'.format(tool_name, module_name))
        raise
    sys.argv[:] = [sys.argv[0]] + sys.argv[2:]
    rc = module.main()

    if profile:
        profile.disable()
        ps = pstats.Stats(profile, stream=sys.stderr).sort_stats('cumulative')
        ps.print_stats()
    exit(rc)
