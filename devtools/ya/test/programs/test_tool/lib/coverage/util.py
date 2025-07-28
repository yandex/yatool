import collections
import itertools
import logging
import os
import six

import cityhash
import exts.func

logger = logging.getLogger(__name__)


@exts.func.memoize()
def get_top_level2_dirs(source_root):
    try:
        l1_dirs = next(os.walk(source_root))[1]
    except StopIteration:
        raise Exception("Specified source root is empty: {} ({})".format(source_root, os.listdir(source_root)))

    res = []
    for l1 in l1_dirs:
        _, dirs, files = next(os.walk(os.path.join(source_root, l1)))
        res += [os.path.join(l1, d) for d in dirs]
        res += [os.path.join(l1, f) for f in files]
    return res


@exts.func.memoize()
def normalize_path(filename, source_root):
    top_level2 = get_top_level2_dirs(source_root)
    parts = filename.split(os.sep)
    if six.PY2:
        pairs = list(itertools.izip_longest(parts, parts[1:]))
    else:
        pairs = list(itertools.zip_longest(parts, parts[1:]))
    while len(pairs) > 1:
        if os.path.join(*pairs[0]) in top_level2:
            return os.sep.join([p[0] for p in pairs])
        pairs = pairs[1:]


def get_default_export_skip_pattern():
    # Provide default skip pattern to speed up coverage export
    # and avoid processing generated files, which will be dropped later
    pattern = "|".join(
        [
            '(/contrib/(libs|python|tools)/)',
            # protobuf and flatbuffers
            r'(\.(pb|fbs)\.(h|cc|c)$)',
            # GENERATE_ENUM_SERIALIZATION_*
            r'(_serialized\.(h|cpp|cc|c)$)',
            # SPLIT_CODEGEN kernel/web_factors_info/factors_codegen
            r'(factors_gen\.\d+\.cc?p?p?$)',
            # ragel generated files
            r'(\.rl6\.cc?p?p?$)',
            # python generated files
            r'(\.pyx\.cc?p?p?$)',
            # VCS info generated file
            r'(/__vcs_version__.\w+$)',
        ]
    )
    return "({})".format(pattern)


def get_default_llvm_export_args():
    return [
        '--ignore-filename-regex',
        get_default_export_skip_pattern(),
    ]


def guess_llvm_coverage_filename(covtype, data):
    if covtype == 'files':
        pattern = '"filename":"'
    elif covtype == 'functions':
        pattern = '"filenames":["'
    else:
        raise AssertionError(covtype)

    pos = data.find(pattern)
    assert pos != -1, (pattern, data)
    end = data.find('"', pos + 1 + len(pattern))
    return data[pos + len(pattern) : end]


@exts.func.memoize()
def should_skip(filename, source_root):
    relpath = normalize_path(filename, source_root)
    if not relpath:
        logger.debug('Skipping source code from external resource: %s', filename)
        return True

    if not os.path.exists(os.path.join(source_root, relpath)):
        logger.debug('Skipping generated code: %s (relpath: %s)', filename, relpath)
        return True

    return False


def get_python_profiles(files):
    # Skip json reports - coverage machinery doesn't know what to do with it
    # https://a.yandex-team.ru/arcadia/library/python/coverage/__init__.py?rev=r10736712#L53-81
    return [x for x in files if not x.endswith(".json")]


def get_coverage_profiles_map(filenames):
    files_map = group_by_bin_name(filenames)

    for bin_name, filenames in list(files_map.items()):
        nprofiles = len(filenames)
        broken = get_malformed_profiles(filenames)
        filenames = set(filenames) - set(broken)
        # Remove duplicates to speed up merge without loosing completeness of coverage,
        # but loosing aggregated counters values. It seems that aggregated counters are not really interesting
        # because they will never show actual value, because tests may be launched several times
        # due to crashes with relaunching machinery or fork_subtests, etc
        files_map[bin_name] = get_unique_files(filenames)
        logger.debug(
            "Profiles found for '%s': %d (broken(skipped): %d uniq: %d)",
            bin_name,
            nprofiles,
            len(broken),
            len(files_map[bin_name]),
        )

    return files_map


def group_by_bin_name(files):
    files_map = collections.defaultdict(list)
    for filename in files:
        basename = os.path.basename(filename)
        # {binname}.{pid}.clang.profraw
        assert basename.count('.') >= 3, filename
        binname = basename.rsplit('.', 3)[0]
        assert binname, filename
        files_map[binname] += [filename]
    return files_map


def get_malformed_profiles(filenames):
    empty = []
    for filename in filenames:
        size = os.stat(filename).st_size
        if size == 0:
            empty.append(filename)
    return empty


def get_unique_files(files):
    smap = {}
    for filename in files:
        size = os.stat(filename).st_size
        if size not in smap:
            smap[size] = [filename]
        else:
            smap[size].append(filename)

    uniq = []
    dups = []
    for filenames in six.itervalues(smap):
        if len(filenames) == 1:
            uniq.append(filenames[0])
        else:
            dups += filenames

    d = {}
    for filename in dups:
        h = cityhash.filehash64(six.ensure_binary(filename))
        if h not in d:
            d[h] = filename
    return list(d.values()) + uniq
