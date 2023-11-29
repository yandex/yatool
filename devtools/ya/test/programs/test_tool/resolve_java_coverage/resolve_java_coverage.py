# coding: utf-8
import six

import os
import re
import logging
import argparse
import itertools
import collections

import ujson as json

from exts import func
from devtools.ya.test.programs.test_tool.lib import runtime
from test.util import shared
from library.python.testing import coverage_utils as coverage_utils_library
import devtools.ya.test.programs.test_tool.lib.coverage as lib_coverage

logger = logging.getLogger(__name__)
# https://docs.oracle.com/javase/specs/jls/se7/html/jls-8.html#jls-8.1.1
CLASS_DEC_REGEX = re.compile(
    r"^(public|protected|private|abstract|strictfp|static|final|\s)*(class|enum|interface)\s+([A-Za-z0-9_]+)",
    flags=re.MULTILINE,
)


def split_java_ext(filename):
    if filename.endswith(".java"):
        return os.path.splitext(filename)[0]
    return filename


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True)
    parser.add_argument('--cpsf', dest='cpsfs', required=True, default=[], action='append')
    parser.add_argument('--coverage-path', required=True)
    parser.add_argument('--source-root')
    parser.add_argument('--log-path')
    parser.add_argument('--prefix-filter')
    parser.add_argument('--exclude-regexp')
    parser.add_argument('--test-mode', action='store_true')

    args = parser.parse_args()
    return args


class Node(object):
    _id_iter = iter(itertools.count())

    def __init__(self, name, parent):
        self.id = self._gen_id()
        self.name = name
        self.children = {}
        self.parent = parent
        self.filenames = {}

    def child(self, name):
        return self.children[name]

    def iter_children(self):
        for n in six.itervalues(self.children):
            yield n

    def add_filename(self, filename):
        basename = os.path.basename(filename)
        if basename in self.filenames:
            logger.debug(
                "Found collision in global scope: %s (previously found: %s)", filename, self.filenames[basename]
            )
        self.filenames[basename] = filename

    def _gen_id(self):
        return next(self._id_iter)

    def __str__(self):
        def dump(n):
            if n:
                return "{} ({})".format(n.name or "''", n.id)
            return "None"

        return "Node(name={} id={}  parent={} children={} filenames={})".format(
            self.name,
            self.id,
            dump(self.parent),
            [dump(node) for node in six.itervalues(self.children)],
            self.filenames,
        )

    def __repr__(self):
        return str(self)


class PackageBundle(object):
    def __init__(self, source_root):
        self.source_root = source_root
        self.proot = Node('', None)

    def add(self, pack_prefix, src_path):
        curr = self._extend(self.proot, self._split(pack_prefix)[:-1])
        curr.add_filename(src_path)

    @staticmethod
    def _extend(root, parts):
        for pname in parts:
            if pname not in root.children:
                node = Node(pname, root)
                root.children[pname] = node
                root = node
            else:
                root = root.child(pname)
        return root

    @staticmethod
    def _split(name):
        return [_f for _f in name.split("/") if _f]

    @shared.timeit
    def find_files(self, package_name, source_files):
        source_files = set(source_files)
        node = self.proot

        for pname in self._split(package_name):
            # Looks like a package from external jar
            if pname not in node.children:
                return None
            node = node.children[pname]

        files = self._get_files(node, source_files)
        missing = source_files - set(files)

        if missing:
            # There might be generated files - it's ok
            logger.debug(
                "Failed to find all required files for package '{}': (found:{} expected:{} diff:{})".format(
                    package_name, files, source_files, set(files) ^ source_files
                )
            )
        return files

    def _get_files(self, root, source_files):
        files = root.filenames
        # Search for files located on different levels on the fs,
        # but stored in the same package, for example:
        # JAVA_SRCS(PACKAGE_PREFIX com.noyandex X/Y.java D.java)
        # package com.noyandex will contain D.java and Y.java without 'X/' prefix
        missing = source_files - set(files)

        if missing:
            logger.debug("Searching for missing files: %s for %s", missing, root)
            q = collections.deque(root.iter_children())
            while q and missing:
                node = q.popleft()

                found = missing & set(node.filenames)
                for filename in found:
                    files[filename] = node.filenames[filename]
                missing -= found

                if missing:
                    q.extend(node.iter_children())
        # Return only presented in the coverage report files
        return {f: files[f] for f in source_files if f in files}

    @staticmethod
    def _join(p1, p2):
        if p1:
            return p1 + "/" + p2
        return p2

    def __str__(self):
        def transform(node, pname, level, data):
            full_name = self._join(pname, node.name)
            entry = {
                'name': node.name,
                'name_full': full_name,
                'id': node.id,
                'parent_id': node.parent.id if node.parent else None,
                'level': level,
            }
            if node.filenames:
                entry['filenames'] = node.filenames
            if node.children:
                # pretty print with sorted keys
                entry['|children_count'] = len(node.children)
                entry['|children_names'] = node.children.keys()
                entry['~children'] = {}
                for child in node.iter_children():
                    transform(child, full_name, level + 1, entry['~children'])

            data[node.name] = entry
            return data

        return json.dumps(transform(self.proot, '', 0, {}), indent=1, sort_keys=True).replace(r"\/", "/")

    def __repr__(self):
        return str(self)


class Package(object):
    def __init__(self, files, sourcedata, classdata):
        self._files = files
        self._sourcedata = sourcedata
        self._classdata = classdata

    def iter_files(self):
        for f in self._files:
            yield f

    def get_lines(self, abs_path):
        filename = os.path.basename(abs_path)
        return self._sourcedata[filename]['lines']

    def get_methods(self, abs_path):
        classname = split_java_ext(os.path.basename(abs_path))
        return self._classdata[classname]['methods']

    def __str__(self):
        return str(
            "Package(files={}, sourcedata={}, classdata={})".format(self._files, self._sourcedata, self._classdata)
        )

    def __repr__(self):
        return self.__str__()


@shared.timeit
def build_package(pack_name, packdata, bundle):
    source_files = packdata.get("sourcefiles", {}).keys()

    files_map = bundle.find_files(pack_name, source_files)
    if not files_map:
        return None

    # XXX it's a hack, but there might be generated files - we need to drop them from packdata
    existing_files = set(files_map)
    packdata['sourcefiles'] = {k: v for k, v in six.iteritems(packdata['sourcefiles']) if k in existing_files}

    sourcedata = get_source_files(packdata)
    classdata = get_merged_classdata(packdata, pack_name, files_map, bundle.source_root)
    # There might be classes marked with @YaIgnore and they are missing in classdata
    # Add empty records to show not covered file
    if len(sourcedata.keys()) != len(classdata.keys()):
        classes = set([split_java_ext(s) for s in sourcedata.keys()])
        for classname in classes - set(classdata.keys()):
            classdata[classname] = {'counters': {}, 'methods': {}}
        assert len(sourcedata.keys()) == len(classdata.keys()), (classes ^ set(classdata.keys()), pack_name, packdata)

    return Package(six.itervalues(files_map), sourcedata, classdata)


@shared.timeit
def resolve_coverage(rawcov, bundle, prefix_filter, exclude_regexp):
    logger.debug("Prefix filter:%s exclude regexp:%s", prefix_filter, exclude_regexp)
    file_filter = coverage_utils_library.make_filter(prefix_filter, exclude_regexp)

    covdata = {}
    for package_name, packdata in six.iteritems(rawcov["packages"]):
        package = build_package(package_name, packdata, bundle)
        if not package:
            source_files = packdata.get("sourcefiles", {}).keys()
            logger.debug(
                "Failed to resolve package name: %s (package files: %s)", package_name, get_samples(source_files, 2)
            )
            continue

        for filename in package.iter_files():
            if not file_filter(filename):
                logger.debug("Filtered %s", filename)
                continue

            covdata[filename] = {
                'segments': get_segments(package.get_lines(filename)),
                'functions': get_functions(package.get_methods(filename)),
            }
    return covdata


@func.memoize(limit=1000)
def get_class_declarations(filename):
    with open(filename) as afile:
        data = afile.read()
    return {m.group(3) for m in CLASS_DEC_REGEX.finditer(data)}


def get_source_files(packdata):
    source_files = packdata["sourcefiles"]
    return source_files


def merge_counters(src, dst):
    for k, v in six.iteritems(src):
        if k not in dst:
            dst[k] = v
            continue
        if isinstance(v, dict):
            merge_counters(src[k], dst[k])
        elif isinstance(v, int):
            dst[k] += v
        else:
            raise Exception("Unknown type ({}): src:{} dst:{}".format(type(v), src, dst))


def merge_classes(src, dst, innername):
    merge_counters(src['counters'], dst['counters'])

    dst_methods = dst['methods']
    for name, data in six.iteritems(src['methods']):
        if innername:
            name = "{}${}".format(innername, name)
        if name in dst_methods:
            logger.debug("Found duplicate: %s\n%s\n%s\n", name, data, dst_methods[name])
        else:
            dst_methods[name] = data


@shared.timeit
def get_merged_classdata(packdata, package_name, files_map, source_root):
    class_files = [split_java_ext(f) for f in files_map]
    split_regex = re.compile(r"[\$;]")
    classes = packdata["classes"]
    res = {}
    source_names = {}

    for class_name in list(classes):
        data = classes[class_name]

        if package_name and class_name.startswith(package_name):
            name = class_name[len(package_name) + 1 :]
        else:
            name = class_name

        parts = split_regex.split(name)
        # drop inner class name
        name = name.split("$")[0]
        if len(parts) > 1:
            innername = "$".join(parts[1:])
        else:
            innername = ''

        # there are some other classes in java-file
        # we will attach minor classes to the main class
        if name not in class_files:
            # Don't overwrite innername of internal class
            if not innername:
                innername = name
            key = (name, package_name)
            if key not in source_names:
                source_names[key] = get_source_name(name, files_map, package_name, source_root)
            name = source_names[key]
            # looks like it came from external resource - just drop it
            if not name:
                del classes[class_name]
                continue

        if name not in res:
            res[name] = {'counters': {}, 'methods': {}}
        merge_classes(data, res[name], innername)
    return res


def get_samples(files, count):
    return list(files)[:count] if len(files) > count else files


@shared.timeit
def get_source_name(classname, files_map, package_name, source_root):
    for filename, filepath in six.iteritems(files_map):
        if classname in get_class_declarations(os.path.join(source_root, filepath)):
            return split_java_ext(filename)
    # Class might be marked with @YaIgnore
    logger.debug(
        "Failed to find java file with classname:%s (pacakge: '%s' filename samples:%s)",
        classname,
        package_name,
        get_samples(files_map.values(), 2),
    )


def lines_to_unified_segments(lines):
    # (line number, missed instructions, covered instructions, missed branches, covered branches)
    for ln, mi, ci, mb, cb in lines:
        # default java coverage dumps line number for coverage - change it to index (starts from 0)
        ln -= 1
        # one one more branches missed
        if mb & cb:
            # add empty non-covered segment to report about missed branch
            yield (ln, 0, ln, 0, 0)
            yield (ln, 0, ln + 1, 0, 1)
        else:
            covered = ci | cb
            yield (ln, 0, ln + 1, 0, 1 if covered else 0)


def get_segments(lines):
    return lib_coverage.merge.compact_segments(lines_to_unified_segments(lines))


def get_functions(methods):
    res = {}
    for name, data in six.iteritems(methods):
        # annotation's arguments doesn't provide positions
        if 'line' not in data:
            continue
        # line no -> index (starts with 0)
        pos = (data['line'] - 1, 0)
        covered = int(any(c['covered'] for c in data['counters'].values()))
        if pos in res:
            assert name not in res[pos], (pos, name, res)
            res[pos][name] = int(covered)
        else:
            res[pos] = {name: covered}
    return res


@shared.timeit
def build_package_bundle(files, source_root):
    bundle = PackageBundle(source_root)
    for filename in files:
        if not os.path.exists(filename):
            logger.debug("Skipping missing file: %s", filename)
            continue

        with open(filename) as afile:
            for line in afile.readlines():
                inner_path, src_path = line.strip().split(":")
                bundle.add(inner_path, src_path)

    # logger.debug("Package bundle: %s", bundle)
    return bundle


def main():
    args = parse_args()
    shared.setup_logging(logging.DEBUG, args.log_path)
    logger.debug("Source root: %s", args.source_root)

    # create node's output
    with open(args.output, "w") as afile:
        afile.write("no valid output was provided")

    if os.path.exists(args.coverage_path):
        logger.debug("Coverage file size: %db", os.stat(args.coverage_path).st_size)
    else:
        # test might be skipped by filter and there will be no coverage data
        logger.debug("No profdata available - file doesn't exist: %s", args.coverage_path)
        with open(args.output, 'w') as afile:
            json.dump({}, afile)
        return

    bundle = build_package_bundle(args.cpsfs, args.source_root)
    logger.debug("Resolving coverage (source root: %s)", args.source_root)
    with open(args.coverage_path) as afile:
        covdata = resolve_coverage(json.load(afile), bundle, args.prefix_filter, args.exclude_regexp)

    lib_coverage.export.dump_coverage(covdata, args.output)

    logger.debug('Time spend: %s', json.dumps(shared.timeit.stat, sort_keys=True))
    logger.debug('maxrss: %d', runtime.get_maxrss())
    return os.environ.get("YA_COVERAGE_FAIL_RESOLVE_NODE", 0)


if __name__ == '__main__':
    exit(main())
