import collections
import logging
import os.path
import six

logger = logging.getLogger(__name__)


def skip_prefix(path):
    while path.startswith('$S/') or path.startswith('$B/'):
        path = path[3:]

    return path


def missing_dirs(evlog, force_root_dirs):
    root_dirs = set()

    def mine_from_bad_incl(evlog):
        for ev in evlog:
            if ev['_typename'] == 'NEvent.TBadIncl':
                incl = ev['Include']
                frm = ev['FromHere']
                if incl.startswith('$B/'):
                    continue  # Known to be generated
                if incl.startswith('$S/'):
                    yield os.path.dirname(incl[3:])  # Known to be source
                elif incl.startswith('..'):
                    yield os.path.dirname(os.path.normpath(os.path.join(os.path.dirname(skip_prefix(frm)), incl)))
                else:
                    yield os.path.dirname(incl)

    def mine_from_invalid_files(evlog):
        for ev in evlog:
            if ev['_typename'] == 'NEvent.TInvalidFile':
                if ev['File'].startswith('$B/'):
                    continue
                rootrel = ev['File'].startswith('$S/')
                f = skip_prefix(ev['File'])
                dirs = ev.get('Dirs', []) if not rootrel else []
                for d in dirs:
                    yield os.path.dirname(os.path.normpath(os.path.join(skip_prefix(d), f)))
                if not f.startswith('..'):
                    d = os.path.dirname(f)
                    if not dirs or d in force_root_dirs:
                        yield d
                    else:
                        root_dirs.add(d)

    invalid_dirs = [x.get('Dirs', []) for x in evlog if x['_typename'] == 'NEvent.TInvalidFile']
    invalid_dirs = [skip_prefix(item) for sublist in invalid_dirs for item in sublist]

    peerdirs = [skip_prefix(x['Dir']) for x in evlog if x['_typename'] == 'NEvent.TInvalidPeerdir']
    no_makefiles = [skip_prefix(x['Where']) for x in evlog if x['_typename'] == 'NEvent.TNoMakefile']
    recurses = [skip_prefix(x['Dir']) for x in evlog if x['_typename'] == 'NEvent.TInvalidRecurse']
    addincls = [skip_prefix(x['Dir']) for x in evlog if x['_typename'] == 'NEvent.TInvalidAddIncl']
    src_dirs = [skip_prefix(x['Dir']) for x in evlog if x['_typename'] == 'NEvent.TInvalidSrcDir']
    data_dirs = [skip_prefix(x['Dir']) for x in evlog if x['_typename'] == 'NEvent.TInvalidDataDir']
    java_dirs = [skip_prefix(x['Dir']) for x in evlog if x['_typename'] == 'JavaMissingDir']

    bad_incl = list(mine_from_bad_incl(evlog))
    invalid_files = list(mine_from_invalid_files(evlog))

    logger.debug('Missing dirs via peerdirs %s', sorted(set(peerdirs)))
    logger.debug('Missing dirs via no_makefiles %s', sorted(set(no_makefiles)))
    logger.debug('Missing dirs via recurses %s', sorted(set(recurses)))
    logger.debug('Missing dirs via addincls %s', sorted(set(addincls)))
    logger.debug('Missing dirs via srcdirs %s', sorted(set(src_dirs)))
    logger.debug('Missing dirs via invalid_dirs %s', sorted(set(invalid_dirs)))
    logger.debug('Missing dirs via bad_incls %s', sorted(set(bad_incl)))
    logger.debug('Missing dirs via invalid files %s', sorted(set(invalid_files)))
    logger.debug('Missing dirs via data_dirs %s', sorted(set(data_dirs)))
    logger.debug('Missing dirs via jbuild %s', sorted(set(java_dirs)))
    logger.debug('Skipped root dirs %s', sorted(root_dirs))

    all_unique = sorted(
        set(
            peerdirs
            + no_makefiles
            + recurses
            + addincls
            + src_dirs
            + invalid_dirs
            + bad_incl
            + invalid_files
            + data_dirs
            + java_dirs
        )
    )

    all_filtered = [x for x in all_unique if x]

    return all_filtered, root_dirs


def parse_failed_deps_and_errors(evlog, targets, arc_root, build_root):
    def find_top_level_target(tgt):
        res = collections.deque()
        logger.debug("Looking for top-level target for %s", tgt)
        marked = collections.defaultdict(lambda: False)

        def topo_sort(v):
            marked[v] = True
            for vv in failed_deps[v]:
                if not marked[vv]:
                    topo_sort(vv)
            res.appendleft(v)

        topo_sort(tgt)
        for v in res:
            if v in targets:
                logger.debug("Top level for target %s is %s", tgt, v)
                return v

    failed_targets_ = collections.defaultdict(lambda: list())
    failed_deps = collections.defaultdict(lambda: set())
    unknown_failed_deps = []
    for event in evlog:
        if event['_typename'] == "NEvent.TTaskFailure":
            name = event['Name']
            stderr = None
            if 'StdErr' in event:
                stderr = event['StdErr']
            elif 'StdError' in event:
                stderr = event['StdError']
            failed_targets_[name].append(
                stderr.replace(build_root, '$(BUILD_ROOT)').replace(arc_root, '$(SOURCE_ROOT)')
            )
        elif event['_typename'] == "NEvent.TTaskInducedFailure":
            if 'BrokenDeps' in event:
                for b in event['BrokenDeps']:
                    failed_deps[b].add(event['Name'])
            else:
                unknown_failed_deps.append(event['Name'])

    failed_targets = collections.defaultdict(lambda: list())
    for k, v in six.iteritems(failed_targets_):
        name = k
        if name not in targets:
            name = find_top_level_target(name)
        failed_targets[name].extend(v)

    res = dict()
    for k, v in six.iteritems(failed_targets):
        res[targets[k]] = v

    deps = collections.defaultdict(lambda: set())
    for t in failed_targets.keys():
        marked = collections.defaultdict(lambda: False)
        d = []

        def find_all_deps(v):
            marked[v] = True
            for vv in failed_deps[v]:
                if not marked[vv]:
                    d.append(vv)
                    find_all_deps(vv)

        find_all_deps(t)
        for tgt in d:
            if tgt in targets:
                deps[targets[tgt]].add(targets[t])
    for t in unknown_failed_deps:
        deps[targets[t]].add('unknown')
    for proj, dep in six.iteritems(deps):
        if proj not in res:
            res[proj] = ["Depends on broken targets:\n" + "\n".join(dep)]
        else:
            deps[proj] = None
    failed_deps = {}
    for k, v in six.iteritems(deps):
        if v:
            failed_deps[k] = list(v)
    build_errors = dict(res)

    return failed_deps, build_errors
