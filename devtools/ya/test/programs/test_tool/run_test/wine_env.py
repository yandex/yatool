import os
import shutil
import stat

# Patched wine setupapi (DEVTOOLSSUPPORT-93760) hard links fake dlls in the prefix to the
# files of the wine resource instead of copying them (~500 MB less to write per prefix).
FAKEDLL_HARDLINK_ENV = 'WINE_FAKEDLL_HARDLINK'
# Set by the distbuild runner for every node it starts. An informal contract with the worker
# (runner/main.cpp), and it leaks into nested runs (ytexec, recipes, tests running ya make),
# so it is only a hint: safety rests on the ownership checks below.
DISTBUILD_MARKER_ENV = 'DISTBUILD_RUNNER_BINARY_START_TIMESTAMP'
# A fake dll present in every wine prefix. The worker unpacks the resource with uniform modes,
# so one probe file stands for all of them.
PROBE_DLL = os.path.join('lib', 'wine', 'x86_64-windows', 'ntdll.dll')
# Kill switch that needs no test_tool release: YA_* variables of the environment of the ya
# client reach every test node (sysenv.get_common_env), so YA_WINE_FAKEDLL_HARDLINK=0 in the
# environment of the ya process of autocheck turns the links off everywhere. A user does the
# same for one test with --test-env WINE_FAKEDLL_HARDLINK=0.
KILL_SWITCH_ENV = 'YA_WINE_FAKEDLL_HARDLINK'


def fakedll_hardlink_decision(wine_root, prefix_dir, environ=None, euid=None, access=None, stat_func=None):
    """Decide whether the wine prefix may hard link fake dlls to the wine resource.

    Returns (env, reason): env holds the variables enabling the links, reason is a string
    saying why they stay off (None when they are on). Links are enabled only when writing
    through them can't damage the shared resource:
      - the test runs under distbuild;
      - the resource files belong to another user and are not writable by the test process,
        so they can't be made writable or modified through a link;
      - the resource and the prefix are on the same file system (otherwise linking fails
        anyway; link() may still fail between bind mounts of one file system, then wine
        copies and the diagnostic line shows links=0);
      - the kill switch YA_WINE_FAKEDLL_HARDLINK=0 is not set.
    A user may still override the variable via test env.
    """
    environ = os.environ if environ is None else environ
    euid = os.geteuid() if euid is None else euid
    access = os.access if access is None else access
    stat_func = os.stat if stat_func is None else stat_func
    if not wine_root:
        return {}, 'no wine resource'
    if DISTBUILD_MARKER_ENV not in environ:
        return {}, 'not under distbuild'
    if environ.get(KILL_SWITCH_ENV, '') == '0':
        return {}, 'disabled by {}=0'.format(KILL_SWITCH_ENV)
    probe = os.path.join(wine_root, PROBE_DLL)
    try:
        probe_st = stat_func(probe)
        prefix_st = stat_func(prefix_dir)
    except OSError as e:
        return {}, 'cannot stat: {}'.format(e)
    if probe_st.st_uid == euid:
        return {}, 'resource owned by the test user'
    if access(probe, os.W_OK):
        return {}, 'resource writable by the test user'
    if probe_st.st_dev != prefix_st.st_dev:
        return {}, 'resource and prefix on different file systems'
    return {FAKEDLL_HARDLINK_ENV: '1'}, None


def fakedll_hardlink_env(wine_root, prefix_dir, environ=None, euid=None, access=None):
    """Env vars enabling the links, see fakedll_hardlink_decision."""
    return fakedll_hardlink_decision(wine_root, prefix_dir, environ, euid, access)[0]


# Fake dlls live here inside the prefix
SYSTEM32 = os.path.join('drive_c', 'windows', 'system32')


def prefix_report(prefix_dir):
    """Count how the prefix was populated: regular files, hard links (nlink > 1), own bytes.

    Cheap (about 900 files, more if the test wrote into the prefix); used only for a diagnostic
    log line so that a run under distbuild shows whether links were used and how much the node
    wrote. Approximate by design: wineserver may still be rewriting the registry files when the
    line is printed, and a file linked twice inside the prefix counts as a link.
    """
    files = links = 0
    own_bytes = 0
    for root, _, names in os.walk(prefix_dir):
        for name in names:
            try:
                st = os.lstat(os.path.join(root, name))
            except OSError:
                continue
            if not stat.S_ISREG(st.st_mode):
                continue
            files += 1
            if st.st_nlink > 1:
                links += 1
            else:
                own_bytes += st.st_size
    return {'files': files, 'links': links, 'own_mb': round(own_bytes / 1e6, 1)}


def report_line(prefix_dir):
    """prefix_report as 'files=N links=N own_mb=N' for the log."""
    return ' '.join('{}={}'.format(k, v) for k, v in sorted(prefix_report(prefix_dir).items()))


def describe(env, user_env):
    """Mode string for the diagnostic log line (env may be an Environ: get() needs a default)."""
    if FAKEDLL_HARDLINK_ENV in user_env:
        return 'user:{}'.format(user_env[FAKEDLL_HARDLINK_ENV])
    return 'links' if env.get(FAKEDLL_HARDLINK_ENV, '') == '1' else 'copy'


# --- Prefix template (form A) ----------------------------------------------------------------
# A ready prefix built by wineboot in the resource recipe (contrib/tools/wine) and shipped
# inside the wine resource, next to bin, lib and share. Cloning it (hard links to the template
# and to the rest of the resource, private copies of the registry) replaces the per-chunk
# wineboot: 862 files and ~580 MB of writes become a few small copies and the 5-30 s bootstrap
# disappears.
TEMPLATE_DIR = 'prefix-template'
TEMPLATE_TIMESTAMP = '.update-timestamp'  # wine compares its content with the mtime of wine.inf
TEMPLATE_MANIFEST = 'links.tsv'  # "<path in prefix>\t<path in the wine resource>" per line
WINE_INF = os.path.join('share', 'wine', 'wine.inf')
# Wine rewrites these on every start, so they must be private copies; everything else is read-only
TEMPLATE_COPIED = frozenset(['system.reg', 'user.reg', 'userdef.reg', TEMPLATE_TIMESTAMP])
# Kill switch without a test_tool release: YA_WINE_PREFIX_CLONE=0 in the environment of the ya client
CLONE_KILL_SWITCH_ENV = 'YA_WINE_PREFIX_CLONE'
# A user turns cloning off for one test with --test-env WINE_PREFIX_CLONE=0
CLONE_USER_ENV = 'WINE_PREFIX_CLONE'


def _foreign_read_only(path, euid, access, dev):
    """True when path belongs to another user, is not writable and is on the file system dev."""
    try:
        st = os.stat(path)
    except OSError:
        return False
    return st.st_uid != euid and not access(path, os.W_OK) and st.st_dev == dev


def _empty_dir(path):
    """Remove everything inside path (used to undo a failed clone)."""
    for name in os.listdir(path):
        p = os.path.join(path, name)
        if os.path.isdir(p) and not os.path.islink(p):
            shutil.rmtree(p)
        else:
            os.unlink(p)


def _raise(error):
    raise error


def _link_or_copy(src, dst, link, counters):
    try:
        link(src, dst)
        counters['linked'] += 1
    except OSError:
        # another file system, protected_hardlinks, link limit: a copy is slower but correct
        shutil.copyfile(src, dst)
        counters['copied'] += 1


def clone_prefix(wine_root, prefix_dir, environ=None, euid=None, access=None, link=os.link):
    """Populate the empty prefix_dir from the template of the wine resource. Returns (report, reason).

    report is {'linked': n, 'copied': n} on success and None when cloning was not attempted or
    failed; then reason says why and prefix_dir is left empty, so wine creates the prefix itself
    as before. The same safety conditions as for the fake dll links apply to the template and
    to the rest of the resource (another owner, not writable, same file system as the prefix),
    and the template must match the wine it ships with: its .update-timestamp equals the mtime
    of share/wine/wine.inf, which is the check wine itself does at start-up (a mismatch would
    mean a wineboot -u on every chunk instead of none).
    """
    environ = os.environ if environ is None else environ
    euid = os.geteuid() if euid is None else euid
    access = os.access if access is None else access
    if not wine_root:
        return None, 'no wine resource'
    if DISTBUILD_MARKER_ENV not in environ:
        return None, 'not under distbuild'
    if environ.get(CLONE_KILL_SWITCH_ENV, '') == '0':
        return None, 'disabled by {}=0'.format(CLONE_KILL_SWITCH_ENV)
    if environ.get(CLONE_USER_ENV, '') == '0':
        return None, 'disabled by {}=0'.format(CLONE_USER_ENV)
    # the clone is made of hard links too, so the kill switch of the fake dll links stops it as well
    for name in (KILL_SWITCH_ENV, FAKEDLL_HARDLINK_ENV):
        if environ.get(name, '') == '0':
            return None, 'fake dll links are disabled by {}=0'.format(name)
    template_root = os.path.join(wine_root, TEMPLATE_DIR)
    if not os.path.isdir(template_root):
        return None, 'no prefix template in the wine resource'
    try:
        with open(os.path.join(template_root, TEMPLATE_TIMESTAMP)) as f:
            stamp = f.read().strip()
        inf_mtime = str(int(os.stat(os.path.join(wine_root, WINE_INF)).st_mtime))
        dev = os.stat(prefix_dir).st_dev
    except (OSError, IOError) as e:
        return None, 'template, wine.inf or prefix missing: {}'.format(e)
    if stamp != inf_mtime:
        return None, 'template {} {} does not match wine.inf mtime {}'.format(TEMPLATE_TIMESTAMP, stamp, inf_mtime)
    for probe in (os.path.join(template_root, 'system.reg'), os.path.join(wine_root, PROBE_DLL)):
        if not _foreign_read_only(probe, euid, access, dev):
            return None, 'not read-only, owned by the test user or on another file system: {}'.format(probe)

    counters = {'linked': 0, 'copied': 0}
    try:
        # os.walk skips a directory it cannot list unless told otherwise, and a partly listed
        # template must not become a prefix that looks complete
        for root, dirs, names in os.walk(template_root, onerror=_raise):
            rel = os.path.relpath(root, template_root)
            dest_dir = prefix_dir if rel == os.curdir else os.path.join(prefix_dir, rel)
            for d in dirs:
                src = os.path.join(root, d)
                if os.path.islink(src):
                    os.symlink(os.readlink(src), os.path.join(dest_dir, d))
                else:
                    os.mkdir(os.path.join(dest_dir, d))
            for name in names:
                src = os.path.join(root, name)
                dst = os.path.join(dest_dir, name)
                if rel == os.curdir and name == TEMPLATE_MANIFEST:
                    continue
                if os.path.islink(src):
                    os.symlink(os.readlink(src), dst)
                elif rel == os.curdir and name in TEMPLATE_COPIED:
                    shutil.copyfile(src, dst)
                    counters['copied'] += 1
                else:
                    _link_or_copy(src, dst, link, counters)
        with open(os.path.join(template_root, TEMPLATE_MANIFEST)) as f:
            for line in f:
                line = line.rstrip('\r\n')
                if not line:
                    continue
                dst_rel, src_rel = line.split('\t')
                for rel in (dst_rel, src_rel):
                    if os.path.isabs(rel) or os.pardir in rel.split('/') or '\\' in rel:
                        raise ValueError('unsafe path in {}: {}'.format(TEMPLATE_MANIFEST, rel))
                dst = os.path.join(prefix_dir, dst_rel)
                parent = os.path.dirname(dst)
                if not os.path.isdir(parent):
                    os.makedirs(parent)
                _link_or_copy(os.path.join(wine_root, src_rel), dst, link, counters)
    except (OSError, IOError, ValueError) as e:
        reason = 'clone failed: {}'.format(e)
        try:
            _empty_dir(prefix_dir)
        except OSError as e2:
            # a half-populated prefix must not look complete: without the timestamp wine updates it
            reason += '; cleanup failed: {}'.format(e2)
            try:
                os.unlink(os.path.join(prefix_dir, TEMPLATE_TIMESTAMP))
            except OSError:
                pass
        return None, reason
    return counters, None
