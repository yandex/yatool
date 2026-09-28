import os
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
