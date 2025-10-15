import calendar
import datetime
import os
import subprocess
import sys
import time


try:
    import six as six_  # noqa
except ImportError:

    class six_:
        if sys.version_info[0] == 3:
            text_type = str
            binary_type = bytes

            PY3 = True
            PY2 = False

        else:
            text_type = unicode  # noqa
            binary_type = str

            PY3 = True
            PY2 = False

        @classmethod
        def ensure_str(cls, s, encoding='utf-8', errors='strict'):
            # Optimization: Fast return for the common case.
            if type(s) is str:
                return s
            if cls.PY2 and isinstance(s, cls.text_type):
                return s.encode(encoding, errors)
            elif cls.PY3 and isinstance(s, cls.binary_type):
                return s.decode(encoding, errors)
            elif not isinstance(s, (cls.text_type, cls.binary_type)):
                raise TypeError("not expecting type '%s'" % type(s))
            return s


class TimeoutException(Exception):
    pass


# logging.getLogger(__name__)
def get_dummy_logger():
    class Logger:
        def debug(self, *args):
            pass

    return Logger()


try:
    import logging

    logger = logging.getLogger(__name__)
    raise ImportError
except ImportError:
    logger = get_dummy_logger()


# vcs.svn.run.parse_info
def svn_parse_info(data):
    import xml.etree.ElementTree

    res = {}
    xml_root = xml.etree.ElementTree.fromstring(data)
    entry = xml_root.find('entry')
    if entry is None:
        return res

    info = {
        'revision': entry.attrib['revision'],
        'url': entry.find('url').text,
        'repository_root': entry.find('repository').find('root').text,
        'depth': None,
    }

    commit = entry.find('commit')
    if commit is not None:
        date = commit.find('date').text
        date_utc = datetime.datetime.strptime(date, "%Y-%m-%dT%H:%M:%S.%fZ")
        info['timestamp'] = calendar.timegm(date_utc.timetuple())
        info['commit_revision'] = commit.attrib['revision']
        author = commit.find('author')
        if author is not None:
            info['commit_author'] = author.text
        info['commit_date'] = date

    # No wc-info for revisioned remote svn info queries.
    wc_info = entry.find('wc-info')
    if wc_info is not None:
        info['wcroot'] = wc_info.find('wcroot-abspath').text
        info['depth'] = wc_info.find('depth').text

    return info


# yalibrary.find_root.detect_root
def detect_root(path, detector=None):
    def is_root(path):
        return os.path.exists(os.path.join(path, ".arcadia.root")) or os.path.exists(
            os.path.join(path, 'build', 'ya.conf.json')
        )

    def _find_path(starts_from, check):
        p = os.path.normpath(starts_from)
        while True:
            if check(p):
                return p
            next_p = os.path.dirname(p)
            if next_p == p:
                return None
            p = next_p

    detector = detector or is_root
    return _find_path(path, detector)


# yalibrary.svn.get_svn_path_from_url
def get_svn_path_from_url(url):
    # urlparse.urlparse(url).path
    components = url.split('/')
    return components[3]


# yalibrary.svn.run_svn_tool
def svn_run_svn_tool(tool_binary, tool_args, cwd=None, env=None, timeout=0):
    import tempfile

    cmd = [tool_binary] + list(tool_args)
    logger.debug('Run svn %s in directory %s with env %s', cmd, cwd, env)

    if timeout:
        deadline = time.time() + timeout
    else:
        deadline = None

    with tempfile.NamedTemporaryFile(prefix='svn_stdout_') as stdout_file, tempfile.NamedTemporaryFile(
        prefix='svn_stderr_'
    ) as stderr_file:
        proc = subprocess.Popen(cmd, stdout=stdout_file, stderr=stderr_file, cwd=cwd, env=env)

        if deadline:
            timeout_remaining = deadline - time.time()
            if timeout_remaining <= 0:
                proc.kill()
                raise TimeoutException()

            try:
                proc.wait(timeout=timeout_remaining)
            except subprocess.TimeoutExpired:
                proc.kill()
                raise TimeoutException()
        else:
            proc.wait()

        rc = proc.returncode

        stdout_file.file.seek(0, os.SEEK_SET)
        out = stdout_file.read()
        stderr_file.file.seek(0, os.SEEK_SET)
        err = stderr_file.read()

    if rc:
        raise get_svn_exception()(stdout=out, stderr=err, rc=rc, cmd=cmd)

    return six_.ensure_str(out)


def get_svn_exception():
    class SvnRuntimeError(Exception):
        def __init__(self, stdout, stderr, rc, cmd):
            self.stdout = stdout
            self.stderr = stderr
            self.rc = rc
            self.cmd = cmd

        def __str__(self):
            return "SvnRuntimeError: command '{cmd}' finished with rc={rc} and stderr:\n{stderr}".format(
                cmd=" ".join(self.cmd),
                rc=self.rc,
                stderr=self.stderr,
            )

        def __repr__(self):
            return self.__str__()

    return SvnRuntimeError


# Supports single path in paths
# yalibrary.vcs.detect
def detect(paths=[], cwd=None, check_tar=None):
    cwd = cwd or os.getcwd()
    logger.debug('detecting vcs from %s for paths: %s', cwd, paths)
    if paths:
        leafs = [os.path.join(cwd, path) for path in paths]
        common_root = leafs[0]
    else:
        common_root = cwd
    logger.debug('common root: %s', common_root)

    mem = {}

    def detect_vcs_root(path):
        types = []
        if os.path.isdir(os.path.join(path, '.svn')):
            types.append('svn')
        if os.path.isdir(os.path.join(path, '.arc')) and os.path.isfile(os.path.join(path, '.arc', 'HEAD')):
            types.append('arc')
        if os.path.isdir(os.path.join(path, '.hg')):
            types.append('hg')
        if os.path.isdir(os.path.join(path, '.git')):
            types.append('git')
        if check_tar and os.path.isfile(os.path.join(path, '__SVNVERSION__')):
            types.append('tar')
        mem['types'] = types
        return len(types) != 0

    vcs_root = detect_root(common_root, detect_vcs_root)
    logger.debug('vcs root: %s (%s)', vcs_root, ' '.join(mem['types']))

    return tuple(mem.get('types', [])), vcs_root, common_root
