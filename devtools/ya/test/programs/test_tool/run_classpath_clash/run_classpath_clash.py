from __future__ import print_function
import collections
import functools
import os
import re
import sys
import time
import zipfile
import traceback

import exts.hashing as hashing


_TAB = ' ' * 4

# because reasons
IGNORED_CLASSES_REGEX = re.compile(
    r'(.*(module|package)-info\.class$)|(ru/yandex/library/svnversion/SvnConstants\.class)'
)
IGNORE_CLASS_MARKER = 'ignore_class:'
VERBOSE = [False]


# TODO refactor run_check to be able to use logging module
def log(line):
    if VERBOSE[0]:
        sys.stderr.write(line + '\n')


def timeit(func):
    @functools.wraps(func)
    def wrapper(*args, **kwargs):
        started = time.time()
        res = func(*args, **kwargs)
        log('{}(): {}s'.format(func.__name__, time.time() - started))
        return res

    return wrapper


def get_class_names(filename, ignored):
    res = set()
    with zipfile.ZipFile(filename) as jf:
        for entry in jf.infolist():
            if not entry.filename.endswith('.class') or entry.filename in ignored:
                continue

            if IGNORED_CLASSES_REGEX.match(entry.filename):
                continue

            res.add(entry.filename)
    return res


@timeit
def build_classnames_map(files, ignored):
    classmap = {}
    for filename in files:
        class_names = get_class_names(filename, ignored)
        if class_names:
            classmap[filename] = class_names
    return classmap


def get_classnames_count(classmap):
    return sum(len(x) for x in classmap.values())


@timeit
def strip_unique_jars(classmap):
    log('Original number of classnames: {}'.format(get_classnames_count(classmap)))

    tr = collections.defaultdict(list)
    for filename, names in classmap.items():
        for x in names:
            tr[x].append(filename)

    classmap = collections.defaultdict(set)
    for name, files in tr.items():
        if len(files) != 1:
            for filename in files:
                classmap[filename].add(name)

    log('Number of suspicious classnames: {}'.format(get_classnames_count(classmap)))
    return classmap


@timeit
def get_clashed_classes(build_root, jar_files, ignored, strict):
    files = [os.path.join(build_root, i) for i in sorted(jar_files)]
    classmap = build_classnames_map(files, ignored)

    classmap = strip_unique_jars(classmap)

    duplicate_classes_jars = collections.defaultdict(list)
    classlist = []
    for jar, names in classmap.items():
        my_classes = set()
        jar_info = {'name': jar, 'classes': {}}
        with zipfile.ZipFile(jar) as jf:
            for entry in jf.infolist():
                if entry.filename not in names:
                    continue

                if entry.filename not in my_classes:
                    my_classes.add(entry.filename)
                    jar_info['classes'][entry.filename] = None if strict else hashing.fast_hash(jf.read(entry))
                else:
                    duplicate_classes_jars[jar].append(entry.filename)
        classlist.append(jar_info)
    clashmap = collections.defaultdict(list)
    for c1 in range(len(classlist)):
        for c2 in range(c1 + 1, len(classlist)):
            fst = classlist[c1]
            snd = classlist[c2]
            intersect = [
                i
                for i in fst['classes']
                if (i in snd['classes'] and (fst['classes'][i] is None or fst['classes'][i] != snd['classes'][i]))
            ]
            if intersect:
                clashmap[(fst['name'], snd['name'])] = intersect
    return clashmap, duplicate_classes_jars


def classname(cls):
    return os.path.splitext(cls.replace(os.path.sep, '.'))[0]


def pretty_print_clashed_classes(root, clashed_map, snippet_classes_limit=None):
    err_msg = []
    snippet_msg = []
    for files, classes in clashed_map.items():
        classes = sorted(classes)
        msg = '\n'.join(['[[bad]]{}[[rst]]'.format(os.path.relpath(i, root)) for i in files])
        msg += '\nConflicted classes:\n'
        msg += '\n'.join(['{}[[imp]]{}[[rst]]'.format(_TAB, classname(i)) for i in classes[:snippet_classes_limit]])
        if snippet_classes_limit and len(classes) > snippet_classes_limit:
            msg += '\n{}and {} more'.format(_TAB, len(classes) - snippet_classes_limit)
        snippet_msg.append(msg)
        msg = '\n'.join([os.path.relpath(i, root) for i in files]) + '\n'
        msg += '\n'.join([(_TAB + classname(i)) for i in classes])
        err_msg.append(msg)
    return ('There are clashed archives:\n' + '\n'.join(sorted(snippet_msg))), '\n\n'.join(sorted(err_msg))


def pretty_print_duplicated_classes(root, duplicate_jars, snippet_classes_limit=None):
    err_msg = []
    snippet_msg = []
    for jar, classes in duplicate_jars.items():
        classes = sorted(classes)
        msg = '[[bad]]{}[[rst]]'.format(os.path.relpath(jar, root))
        msg += '\nDuplicated classes:\n'
        msg += '\n'.join(['{}[[imp]]{}[[rst]]'.format(_TAB, classname(i)) for i in classes[:snippet_classes_limit]])
        if snippet_classes_limit and len(classes) > snippet_classes_limit:
            msg += '\n{}and {} more'.format(_TAB, len(classes) - snippet_classes_limit)
        snippet_msg.append(msg)
        msg = os.path.relpath(jar, root) + '\n'
        msg += '\n'.join([(_TAB + classname(i)) for i in classes])
        err_msg.append(msg)
    return ('This archives have duplicated classes:\n' + '\n'.join(sorted(snippet_msg))), '\n\n'.join(sorted(err_msg))


def parse_input(inp):
    ignored, jars = set(), []
    for item in inp:
        if item.startswith(IGNORE_CLASS_MARKER):
            item = item[len(IGNORE_CLASS_MARKER) :].replace('.', '/')
            if not item.endswith('.class'):
                item += '.class'
            ignored.add(item)
        else:
            jars.append(item)
    return ignored, jars


def is_enabled(arg):
    if arg in sys.argv:
        sys.argv.remove(arg)
        return True
    return False


def main():
    build_root = sys.argv[1]
    # TODO refactor run_check to be able to use argparse in checkers
    strict = is_enabled("--strict")
    VERBOSE[0] = is_enabled("--verbose")

    ignored, jars = parse_input(sys.argv[2:])

    try:
        clashed_classes, duplicated_classes = get_clashed_classes(build_root, jars, ignored, strict)
    except Exception:
        comment = "[[bad]]Couldn't get clashed classes due to: {}".format(traceback.format_exc())
        print(comment, file=sys.stderr)
        print(comment)
        return 1

    if not clashed_classes and not duplicated_classes:
        return 0
    if clashed_classes:
        snippet, err = pretty_print_clashed_classes(build_root, clashed_classes, 3)
        print(err, file=sys.stderr)
        print(snippet)
    if duplicated_classes:
        snippet, err = pretty_print_duplicated_classes(build_root, duplicated_classes, 3)
        print(err, file=sys.stderr)
        print(snippet)
    print('Full errors list in Stderr')
    return 1
