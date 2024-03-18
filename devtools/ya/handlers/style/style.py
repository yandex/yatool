from __future__ import absolute_import
from __future__ import print_function
import os
import sys
import difflib
import tempfile
import threading
import functools

import six.moves.queue as Queue
from six.moves import range
import coloredlogs
import logging

from termcolor import colored

import yalibrary.tools
import yalibrary.makelists

from .state_helper import check_cancel_state
from handlers.style.cpp_style import fix_clang_format
from handlers.style.golang_style import fix_golang_style
from handlers.style.python_style import fix_python_with_black, fix_python_with_ruff, python_config
from library.python.fs import replace_file
from library.python.testing.style import rules

logger = logging.getLogger(__name__)


CPP_EXTS = ('.cpp', '.cc', '.C', '.c', '.cxx', '.h', '.hh', '.hpp', '.H')
CUDA_EXTS = ('.cu', '.cuh')
GO_EXTS = ('.go',)
PY_EXTS = ('.py',)
YAMAKE_EXTS = ('ya.make', 'ya.make.inc')


def check_file(path, exts):
    # type(str, Tuple[str, ...]) -> bool:
    return path.endswith(exts)


def colorize(text, *args, **kwargs):
    # type(str, Any, Any) -> str
    if sys.stdout.isatty():
        return colored(text, *args, **kwargs)

    return text


def apply_style(path, data, new_data, args):
    # type(str, str, str, StyleOptions) -> None:
    if new_data != data:
        if args and args.dry_run:
            if args.full_output:
                sys.stdout.write('fix {}\n{}\n'.format(colorize(path, color='green', attrs=['bold']), new_data))
            else:
                diff = difflib.unified_diff(data.splitlines(), new_data.splitlines())
                diff = list(diff)[2:]  # Drop header with filenames
                diff = '\n'.join(diff)

                sys.stdout.write('fix {}\n{}\n'.format(colorize(path, color='green', attrs=['bold']), diff))
        else:
            logger.info('fix %s', colorize(path, color='green', attrs=['bold']))

            tmp = path + '.tmp'

            with open(tmp, 'w') as f:
                f.write(new_data)

            # never break original file
            path_st_mode = os.stat(path).st_mode
            replace_file(tmp, path)
            os.chmod(path, path_st_mode)


def fix_yamake(path, data, args):
    # type(str, str, StyleOptions) -> str:
    yamake = yalibrary.makelists.from_str(data)
    return yamake.dump()


def fix_python(path, data, args):
    # type(str, str, StyleOptions) -> str:
    try:
        return fix_python_with_black(data, path, False, args)
    except RuntimeError as e:
        if "Black produced different code on the second pass of the formatter" in str(e):  # https://st.yandex-team.ru/DEVTOOLS-7642
            for i in range(0, 2):
                data = fix_python_with_black(data, path, True, args)

            return data
        else:
            raise


def fix_python_ruff(path, data, args):
    # type(str, str, StyleOptions) -> str:
    return fix_python_with_ruff(data, path)


def fix_golang(path, data, args):
    # type(str, str, StyleOptions) -> str:
    return fix_golang_style(data, path)


def fix_common(path, data, args):
    # type(str, str, StyleOptions) -> str:
    if data and data[-1] != '\n':
        return data + '\n'
    return data


def wrap_format(func, loader, args):
    @functools.wraps(func)
    def f():
        path, data = loader()

        if path == '-':
            print(func(path, data, args))
        else:
            if args.force or rules.style_required(path, data):
                apply_style(path, data, func(path, data, args), args)
            else:
                logger.warning('skip %s', path)

    return f


def choose_styler(filename, args):
    # type(str, StyleOptions) -> Tuple[str, Callable, bool]:
    if check_file(filename, YAMAKE_EXTS):
        return ('yamake', fix_yamake, False)

    if check_file(filename, CPP_EXTS):
        return ('cpp', fix_clang_format, True)

    if check_file(filename, PY_EXTS):
        if args.use_ruff:
            return ('py', fix_python_ruff, True)
        return ('py', fix_python, True)

    if check_file(filename, GO_EXTS):
        return ('go', fix_golang, True)

    if check_file(filename, CUDA_EXTS):
        return ('cuda', fix_clang_format, False)


def stdin_loader(filename):
    # type(str) -> Tuple[str, Callable[[], Tuple[str, str]]]
    return filename, lambda: ('-', sys.stdin.read())


def file_loader(filename):
    # type(str) -> Tuple[str, Callable[[], Tuple[str, str]]]
    def func():
        with open(filename, 'r') as f:
            return filename, f.read()

    return filename, func


def prepare_tools(kinds, args):
    # type(frozenset, StyleOptions) -> None
    if 'cpp' in kinds:
        # ensure clang-format downloaded
        yalibrary.tools.tool('clang-format')

    if 'py' in kinds:
        if args.use_ruff:
            # ensure ruff downloaded
            yalibrary.tools.tool('ruff')
        else:
            # ensure black downloaded
            yalibrary.tools.tool('black' if not args.py2 else 'black_py2')

            args.python_config_file = tempfile.NamedTemporaryFile(delete=False)  # will be deleted by tmp_dir_interceptor
            args.python_config_file.write(python_config())
            args.python_config_file.flush()  # will be read from other subprocesses
            # keep file open

    if 'go' in kinds:
        # ensure yoimports downloaded
        yalibrary.tools.tool('yoimports')


def run_style(args):
    # type(StyleOptions) -> None
    console_log = logging.StreamHandler()

    for h in list(logging.root.handlers):
        logging.root.removeHandler(h)

    console_log.setLevel(logging.INFO)
    console_log.setFormatter(coloredlogs.ColoredFormatter('%(levelname).1s | %(message)s'))
    logging.root.addHandler(console_log)
    if args.quiet:
        logging.disable(logging.WARNING)

    sys.setrecursionlimit(10000)

    targets = args.targets

    if not targets:
        if os.isatty(sys.stdin.fileno()):
            targets.append(os.getcwd())

    def iter_files():
        if not targets:
            yield stdin_loader(args.stdin_filename)

        for x in targets:
            if os.path.isfile(x):
                yield file_loader(x)
            elif os.path.isdir(x):
                for a, b, c in os.walk(x):
                    for f in c:
                        yield file_loader(os.path.join(a, f))
            else:
                logger.warning('skip %s (no such file or directory)', x)

    def generate_func_with_common(func):
        def func_with_common(p, data, args):
            return fix_common(p, func(p, data, args), args)

        return func_with_common

    def iter_descr():
        file_types = set(args.file_types)
        for filename, loader in iter_files():
            check_cancel_state()
            descr = choose_styler(filename, args)

            if descr:
                kind, func, enabled = descr
                if file_types:
                    # Enable style check if explicitly requested
                    if kind in file_types:
                        enabled = True
                    else:
                        logger.warning('skip %s (filtered by extensions)', filename)
                        continue

                if enabled:
                    yield kind, loader, generate_func_with_common(func)
                else:
                    logger.warning('skip %s (require explicit --%s or --all)', filename, kind)
            else:
                logger.warning('skip %s', filename)

    files = list(iter_descr())

    prepare_tools(frozenset(x[0] for x in files), args)

    q = Queue.Queue()

    threads = []

    def thr_func():
        while True:
            check_cancel_state()
            try:
                q.get()()
            except StopIteration:
                return
            except Exception:
                logger.exception('in thr_func()')

    for i in range(0, args.build_threads):
        t = threading.Thread(target=thr_func)
        t.start()
        threads.append(t)

    try:
        for kind, loader, func in files:
            q.put(wrap_format(func, loader, args))
    finally:
        for t in threads:
            def f():
                raise StopIteration()

            q.put(f)

        for t in threads:
            t.join()
