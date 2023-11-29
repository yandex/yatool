# coding: utf-8

import argparse
import os
import re
import signal
import subprocess
import sys

import devtools.ya.test.programs.test_tool.lib.runtime as runtime
import library.python.cores as cores
import six


def colorize(text):
    filters = [
        # Python file path, line number and module name
        (
            re.compile(r'File "(.*?)", line (\d+), in (\S+)', flags=re.MULTILINE),
            r'File "[[unimp]]\1[[rst]]", line [[alt2]]\2[[rst]], in [[alt1]]\3[[rst]]',
        ),
        # cpp/h file path, line number
        (
            re.compile(r'([/A-Za-z0-9\+_\.\-]+)\.(cpp|h):(\d+)', flags=re.MULTILINE),
            r'[[unimp]]\1.\2[[rst]]:[[alt2]]\3[[rst]]',
        ),
    ]
    for regex, substitution in filters:
        text = regex.sub(substitution, text)
    return text


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('programs', nargs='*')
    parser.add_argument('--skip', action="append", default=[])
    parser.add_argument('--markup', action="store_true")
    parser.add_argument('--gdb-path')
    args = parser.parse_args()

    failed = False
    env = os.environ.copy()
    entry_point = "library.python.testing.import_test.import_test:main"
    env["Y_PYTHON_ENTRY_POINT"] = entry_point
    for program in args.programs:
        # Bypass terminating signals from run_test to target program
        with runtime.bypass_signals(["SIGQUIT", "SIGUSR2"]) as reg:
            p = subprocess.Popen([program] + args.skip, env=env, stderr=subprocess.PIPE, **({'text': True} if six.PY3 else {}))
            reg.register(p.pid)
            _, err = p.communicate()

        failed |= bool(p.returncode)

        if p.returncode:
            if -p.returncode == getattr(signal, "SIGUSR2", 0):
                sys.stderr.write(
                    "[[bad]]Import test has overrun timeout and was terminated, see stderr logs for more info.\n"
                )
            else:
                sys.stderr.write("[[bad]]Import test failed with [[imp]]{}[[bad]] return code.".format(p.returncode))

            sys.stderr.write(
                "To reproduce run 'ya make -t --test-type import_test' or 'ya make' and [[rst]]'Y_PYTHON_ENTRY_POINT={} {}'\n".format(
                    entry_point,
                    os.path.basename(program),
                )
            )
            if args.markup:
                sys.stderr.write(colorize(err))
            else:
                sys.stderr.write(err)

        if p.returncode < 0:
            core_path = cores.recover_core_dump_file(program, os.getcwd(), p.pid)

            if not core_path:
                sys.stderr.write("Failed to find core dump file\n")
                continue

            if not args.gdb_path:
                sys.stderr.write("No gddb path specified\n")
                continue

            # XXX We can't register core dump file here to make it available in test logs,
            # there is no access to tracefile - that why we at least print backtrace
            if core_path and args.gdb_path:
                bt = cores.get_gdb_full_backtrace(program, core_path, args.gdb_path)
                sys.stderr.write("Backtrace:\n{}".format(bt))

    return 0 if not failed else 1


if __name__ == "__main__":
    exit(main())
