# coding: utf-8
import calendar
import datetime
import json
import locale
import re
import os
import socket
import subprocess
import sys
import time


try:
    import logging

    logger = logging.getLogger(__name__)
except ImportError:
    if __name__ == '__main__':
        from standalone import get_dummy_logger
    else:
        from .standalone import get_dummy_logger
    logger = get_dummy_logger()

try:
    from traceback import format_exc
except ImportError:

    def format_exc():
        return 'Not available'


try:
    from exts.windows import on_win
except ImportError:

    def on_win():
        return os.name == 'nt'


try:
    from vcs.svn.run import parse_info as svn_parse_info
except ImportError:
    if __name__ == '__main__':
        from standalone import svn_parse_info
    else:
        from .standalone import svn_parse_info

try:
    from yalibrary.find_root import detect_root
except ImportError:
    if __name__ == '__main__':
        from standalone import detect_root
    else:
        from .standalone import detect_root


try:
    import six as six_
except ImportError:
    if __name__ == '__main__':
        from standalone import six_
    else:
        from .standalone import six_


try:
    from yalibrary.svn import run_svn_tool as svn_run_svn_tool
    from yalibrary.svn import get_svn_path_from_url

    def get_svn_exception():
        from yalibrary.svn import SvnRuntimeError

        return SvnRuntimeError

except ImportError:
    if __name__ == '__main__':
        from standalone import svn_run_svn_tool
        from standalone import get_svn_path_from_url
    else:
        from .standalone import svn_run_svn_tool
        from .standalone import get_svn_path_from_url


try:
    from yalibrary.vcs import detect
except ImportError:
    if __name__ == '__main__':
        from standalone import detect
    else:
        from .standalone import detect


INDENT = " " * 4


def _dump_json(
    arc_root,
    info,
    other_data=None,
    build_user=None,
    build_host=None,
    build_date=None,
    build_timestamp=0,
    custom_version='',
    release_version='',
):
    j = {}
    j['PROGRAM_VERSION'] = info['scm_text'] + "\n" + _SystemInfo._to_text(other_data)
    j['CUSTOM_VERSION'] = str(_SystemInfo._to_text(custom_version)) if custom_version else ''
    j['RELEASE_VERSION'] = str(_SystemInfo._to_text(release_version)) if release_version else ''
    j['SCM_DATA'] = info['scm_text']
    j['ARCADIA_SOURCE_PATH'] = _SystemInfo._to_text(arc_root)
    j['ARCADIA_SOURCE_URL'] = info.get('url', info.get('svn_url', ''))
    j['ARCADIA_SOURCE_REVISION'] = info.get('revision', -1)
    j['ARCADIA_SOURCE_HG_HASH'] = info.get('hash', '')
    j['ARCADIA_SOURCE_LAST_CHANGE'] = info.get('commit_revision', info.get('svn_commit_revision', -1))
    j['ARCADIA_SOURCE_LAST_AUTHOR'] = info.get('commit_author', '')
    j['ARCADIA_PATCH_NUMBER'] = info.get('patch_number', 0)
    j['BUILD_USER'] = _SystemInfo._to_text(build_user)
    j['BUILD_HOST'] = _SystemInfo._to_text(build_host)
    j['VCS'] = info.get('vcs', '')
    j['REPOSITORY'] = info.get('repository', '')
    j['BRANCH'] = info.get('branch', '')
    j['ARCADIA_TAG'] = info.get('tag', '')
    j['DIRTY'] = info.get('dirty', '')

    if 'url' in info or 'svn_url' in info:
        j['SVN_REVISION'] = info.get('svn_commit_revision', info.get('revision', -1))
        j['SVN_ARCROOT'] = info.get('url', info.get('svn_url', ''))
        j['SVN_TIME'] = info.get('commit_date', info.get('svn_commit_date', ''))

    j['BUILD_DATE'] = build_date
    j['BUILD_TIMESTAMP'] = build_timestamp

    return json.dumps(j, sort_keys=True, indent=4, separators=(',', ': '))


def _get_user_locale():
    try:
        if six_.PY3:
            return [locale.getencoding()]
        else:
            return [locale.getdefaultlocale()[1]]
    except Exception:
        return []


class _SystemInfo:
    LOCALE_LIST = _get_user_locale() + [sys.getfilesystemencoding(), 'utf-8']

    @classmethod
    def get_locale(cls):
        import codecs

        for i in cls.LOCALE_LIST:
            if not i:
                continue
            try:
                codecs.lookup(i)
                return i
            except LookupError:
                continue

    @staticmethod
    def _to_text(s):
        if isinstance(s, six_.binary_type):
            return s.decode(_SystemInfo.get_locale(), errors='replace')
        return s

    @staticmethod
    def get_user(fake_build_info=False):
        if fake_build_info:
            return 'unknown'
        sys_user = os.environ.get("USER")
        if not sys_user:
            sys_user = os.environ.get("USERNAME")
        if not sys_user:
            sys_user = os.environ.get("LOGNAME")
        if not sys_user:
            sys_user = "Unknown user"
        return sys_user

    @staticmethod
    def get_hostname(fake_build_info=False):
        if fake_build_info:
            return 'localhost'
        hostname = socket.gethostname()
        if not hostname:
            hostname = "No host information"
        return hostname

    @staticmethod
    def get_date(stamp=None):
        # Format compatible with SVN-xml format.
        return time.strftime("%Y-%m-%dT%H:%M:%S.000000Z", time.gmtime(stamp))

    @staticmethod
    def get_timestamp(fake_build_info=False):
        if fake_build_info:
            return 0
        # Unix timestamp.
        return int(time.time())

    @staticmethod
    def get_other_data(src_dir, build_dir, data_file='local.ymake', fake_build_info=False):
        other_data = "Other info:\n"
        other_data += INDENT + "Build by: " + _SystemInfo.get_user(fake_build_info) + "\n"
        other_data += INDENT + "Top src dir: {src_dir}\n".format(src_dir=os.curdir if fake_build_info else src_dir)
        other_data += INDENT + "Top build dir: {build_dir}\n".format(
            build_dir=os.curdir if fake_build_info else build_dir
        )
        # other_data += INDENT + "Build date: " + get_date() + "\n"
        other_data += INDENT + "Hostname: " + _SystemInfo.get_hostname(fake_build_info) + "\n"
        other_data += INDENT + "Host information: \n" + _SystemInfo._get_host_info(fake_build_info) + "\n"

        other_data += INDENT + _SystemInfo._get_local_data(src_dir, data_file)  # to remove later?

        logger.debug("Other data: %s", other_data)

        return other_data

    @staticmethod
    def _get_local_data(src_dir, data_file):
        local_ymake = ""
        fymake = os.path.join(src_dir, data_file)
        if os.path.exists(fymake):
            with open(fymake, "r") as local_ymake_file:
                local_ymake = INDENT + data_file + ":\n"
                for line in local_ymake_file:
                    local_ymake += INDENT + INDENT + line
        return local_ymake

    @staticmethod
    def _get_host_info(fake_build_info=False):
        if fake_build_info:
            host_info = '*sys localhost 1.0.0 #dummy information '
        elif not on_win():
            host_info = ' '.join(os.uname())
        else:
            host_info = _SystemInfo._system_command_call("VER")  # XXX: check shell from cygwin to call VER this way!

        if not host_info:
            return ""

        host_info_ = six_.ensure_str(host_info)  # type: str

        return "{}{}{}\n".format(INDENT, INDENT, host_info_.strip())

    @staticmethod
    def _system_command_call(command, **kwargs):
        if isinstance(command, list):
            command = subprocess.list2cmdline(command)
        try:
            process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True, **kwargs)
            stdout, stderr = process.communicate()
            if process.returncode != 0:
                logger.debug('{}\nRunning {} failed with exit code {}\n'.format(stderr, command, process.returncode))
                raise get_svn_exception()(stdout=stdout, stderr=stderr, rc=process.returncode, cmd=[command])
            return stdout
        except OSError as e:
            msg = e.strerror
            errcodes = 'error {}'.format(e.errno)
            if on_win() and isinstance(e, WindowsError):
                errcodes += ', win-error {}'.format(e.winerror)
                try:
                    import ctypes

                    msg = six_.text_type(ctypes.FormatError(e.winerror), _SystemInfo.get_locale()).encode('utf-8')
                except ImportError:
                    pass
            logger.debug('System command call {} failed [{}]: {}\n'.format(command, errcodes, msg))
            return None


class _CommonUtils:
    @classmethod
    def _get_arcadia_branch_or_tag(cls, url, keywords):
        parts = url.split('/')[3:]  # skip schema (2) and hostname (1) entries
        root_ind = [i for i, p in enumerate(parts) if p == 'arcadia']
        branch_ind = [i for i, p in enumerate(parts) if p in keywords]
        if root_ind and branch_ind and branch_ind[0] < root_ind[0]:
            return '/'.join(parts[branch_ind[0] + 1 : root_ind[0]])
        else:
            return 'trunk'

    @classmethod
    def _get_arcadia_branch(cls, url):
        return cls._get_arcadia_branch_or_tag(url, ('branches', 'tags'))

    @classmethod
    def _get_arcadia_tag(cls, url):
        tag = cls._get_arcadia_branch_or_tag(url, ('tags',))
        return tag if tag != 'trunk' else ''


class _ArcVersion(_CommonUtils):
    @classmethod
    def parse(cls, json_text, revision, patch_number, dirty):
        """Parses output of
        TZ='' arc info --json"""

        info = json.loads(json_text)
        if 'author' in info:
            info['commit_author'] = info['author']
            del info['author']

        t_pattern = '%Y-%m-%dT%H:%M:%SZ'
        try:
            date_utc = datetime.datetime.strptime(info['date'], t_pattern)
            info['timestamp'] = calendar.timegm(date_utc.timetuple())
            info['date'] = date_utc.strftime('%Y-%m-%dT%H:%M:%S.%fZ')
        except Exception:
            logger.debug('Incorrect date format {} for date {}'.format(t_pattern, info['date']))

        info['svn_commit_revision'] = int(revision)
        info['scm_text'] = cls._format_scm_data(info)
        info['vcs'] = 'arc'

        info['dirty'] = dirty
        info['patch_number'] = int(patch_number)
        logger.debug('Arc info:{}'.format(str(info)))
        return info

    @staticmethod
    def _format_scm_data(info):
        scm_data = "Arc info:\n"
        scm_data += INDENT + "Branch: " + info.get('branch', '') + "\n"
        scm_data += INDENT + "Commit: " + info.get('hash', '') + "\n"
        scm_data += INDENT + "Author: " + info.get('commit_author', '') + "\n"
        scm_data += INDENT + "Summary: " + info.get('summary', '') + "\n"
        if 'svn_commit_revision' in info:
            scm_data += INDENT + "Last Changed Rev: " + str(info['svn_commit_revision']) + "\n"
        if 'date' in info:
            scm_data += INDENT + "Last Changed Date: " + info['date'] + "\n"
        return scm_data

    @staticmethod
    def external_data(arc_root):
        env = os.environ.copy()
        env['TZ'] = ''

        arc_json_args = ['info', '--json']
        arc_json_out = svn_run_svn_tool('arc', arc_json_args, env=env, cwd=arc_root)
        revision, patch_number, dirty = _ArcVersion._get_last_svn_revision(arc_root)
        logger.debug(
            'Arc info:{}, last svn:{}, patch_number:{}, dirty:{}'.format(arc_json_out, revision, patch_number, dirty)
        )
        return [arc_json_out, revision, patch_number, dirty]

    @staticmethod
    def _get_last_svn_revision(arc_root):
        env = os.environ.copy()
        env['TZ'] = ''

        describe = svn_run_svn_tool(
            'arc', ['describe', '--svn', '--dirty', '--first-parent'], env=env, cwd=arc_root
        ).strip()

        info = describe.split('-')
        revision = info[0].replace('r', '') if len(info) > 0 else str(-1)
        patch_number = info[1] if len(info) > 1 and info[1].isdigit() else str(0)
        dirty = 'dirty' if info[-1] == 'dirty' else ''
        try:
            patch_number = str(int(patch_number) + int(revision)) if revision != '-1' else patch_number
        except ValueError:
            pass
        return revision.encode('utf-8'), patch_number.encode('utf-8'), dirty.encode('utf-8')


class _SvnVersion(_CommonUtils):
    @classmethod
    def parse(cls, svn_info_out):
        """Parses output of
        svn info --xml"""

        info = svn_parse_info(svn_info_out)
        info['branch'] = cls._get_arcadia_branch(info['url'])
        info['tag'] = cls._get_arcadia_tag(info['url'])
        info['revision'] = int(info['revision'])
        info['commit_revision'] = int(info['commit_revision'])

        info['scm_text'] = cls._format_scm_data(info)
        info['vcs'] = 'svn'
        return info

    @staticmethod
    def _format_scm_data(info):
        scm_data = "Svn info:\n"
        scm_data += INDENT + "URL: " + info['url'] + "\n"
        scm_data += INDENT + "Last Changed Rev: " + str(info['commit_revision']) + "\n"
        scm_data += INDENT + "Last Changed Author: " + info['commit_author'] + "\n"
        scm_data += INDENT + "Last Changed Date: " + info['commit_date'] + "\n"
        return scm_data

    @staticmethod
    def external_data(arc_root):
        svn_info_args = ['info', '--xml']
        xml = svn_run_svn_tool('svn', svn_info_args, cwd=arc_root)
        logger.debug('Svn xml:{}'.format(xml))
        return [xml]


class _HgVersion(_CommonUtils):
    @staticmethod
    def _hg_to_svn_date_fmt(date):
        # Wed Apr 24 11:19:30 2019 +0000
        t_pattern = '%a %b %d %H:%M:%S %Y +0000'
        date_utc = datetime.datetime.strptime(date, t_pattern)
        return date_utc.strftime('%Y-%m-%dT%H:%M:%S.%fZ'), calendar.timegm(date_utc.timetuple())

    @classmethod
    def parse(cls, hg_info):
        """Parses output of
        TZ='' ya tool hg -R $SOURCE_TREE --config alias.log=log --config defaults.log= log -r ."""

        def _get_hg_field(field, hg_info):
            match = re.search(field + ':\\s*(.*)\n', hg_info)
            if match:
                return match.group(1).strip()
            logger.debug('Unexpected format for field {} in {}'.format(field, hg_info))
            return ''

        info = {}
        info['branch'] = _get_hg_field('branch', hg_info)
        info['hash'] = _get_hg_field('changeset', hg_info)
        info['commit_author'] = _get_hg_field('user', hg_info)
        info['commit_date'] = _get_hg_field('date', hg_info)
        info['summary'] = _get_hg_field('summary', hg_info)

        try:
            svn_date, svn_timestamp = cls._hg_to_svn_date_fmt(info['commit_date'])
            info['commit_date'] = svn_date
            info['timestamp'] = svn_timestamp
        except Exception:
            logger.debug('Cannot convert to svn format date {}'.format(info['commit_date']))

        info['scm_text'] = cls._format_scm_data(info)
        info['vcs'] = 'hg'
        return info

    @staticmethod
    def _format_scm_data(info):
        scm_data = "Hg info:\n"
        scm_data += INDENT + "Branch: " + info.get('branch', '') + "\n"
        scm_data += INDENT + "Last Changed Rev: " + info.get('hash', '') + "\n"
        scm_data += INDENT + "Last Changed Author: " + info.get('commit_author', '') + "\n"
        scm_data += INDENT + "Last Changed Date: " + info.get('commit_date', '') + "\n"
        return scm_data

    @staticmethod
    def external_data(arc_root):
        env = os.environ.copy()
        env['TZ'] = ''

        hg_args = ['--config', 'alias.log=log', '--config', 'defaults.log=', 'log', '-r', '.']
        hg_out = svn_run_svn_tool('hg', hg_args, env=env, cwd=arc_root)
        logger.debug('Hg log:{}'.format(hg_out))
        return [hg_out]


class _GitVersion(_CommonUtils):
    @classmethod
    def parse(cls, commit_hash, author_info, summary_info, body_info, tag_info, branch_info, depth=None):
        r"""Parses output of
        git rev-parse HEAD
        git log -1 --format='format:%an <%ae>'
        git log -1 --format='format:%s'
        git log -1 --grep='^git-svn-id: ' --format='format:%b' or
        git log -1 --grep='^Revision: r?\d*' --format='format:%b
        git describe --exact-match --tags HEAD
        git describe --exact-match --all HEAD
        and depth as computed by _get_git_depth
        '"""

        info = {}
        info['hash'] = commit_hash
        info['commit_author'] = _SystemInfo._to_text(author_info)
        info['summary'] = _SystemInfo._to_text(summary_info)

        if 'svn_commit_revision' not in info:
            url = re.search("git?-svn?-id: (.*)@(\\d*).*", body_info)
            if url:
                info['svn_url'] = url.group(1)
                info['svn_commit_revision'] = int(url.group(2))

        if 'svn_commit_revision' not in info:
            rev = re.search('Revision: r?(\\d*).*', body_info)
            if rev:
                info['svn_commit_revision'] = int(rev.group(1))

        info['tag'] = tag_info
        info['branch'] = branch_info
        info['scm_text'] = cls._format_scm_data(info)
        info['vcs'] = 'git'

        if depth:
            info['patch_number'] = int(depth)
        return info

    @staticmethod
    def _format_scm_data(info):
        scm_data = "Git info:\n"
        scm_data += INDENT + "Commit: " + info['hash'] + "\n"
        scm_data += INDENT + "Branch: " + info['branch'] + "\n"
        scm_data += INDENT + "Author: " + info['commit_author'] + "\n"
        scm_data += INDENT + "Summary: " + info['summary'] + "\n"
        if 'svn_commit_revision' in info or 'svn_url' in info:
            scm_data += INDENT + "git-svn info:\n"
        if 'svn_url' in info:
            scm_data += INDENT + "URL: " + info['svn_url'] + "\n"
        if 'svn_commit_revision' in info:
            scm_data += INDENT + "Last Changed Rev: " + str(info['svn_commit_revision']) + "\n"
        return scm_data

    @staticmethod
    def external_data(arc_root):
        env = os.environ.copy()
        env['TZ'] = ''

        hash_args = ['rev-parse', 'HEAD']
        author_args = ['log', '-1', '--format=format:%an <%ae>']
        summary_args = ['log', '-1', '--format=format:%s']
        svn_args = ['log', '-1', '--grep=^git-svn-id: ', '--format=format:%b']
        svn_args_alt = ['log', '-1', '--grep=^Revision: r\\?\\d*', '--format=format:%b']
        tag_args = ['describe', '--exact-match', '--tags', 'HEAD']
        branch_args = ['describe', '--exact-match', '--all', 'HEAD']

        # using local 'Popen' wrapper
        commit = _SystemInfo._system_command_call(['git'] + hash_args, env=env, cwd=arc_root).rstrip()
        author = _SystemInfo._system_command_call(['git'] + author_args, env=env, cwd=arc_root)
        summary = _SystemInfo._system_command_call(['git'] + summary_args, env=env, cwd=arc_root)
        svn_id = _SystemInfo._system_command_call(['git'] + svn_args, env=env, cwd=arc_root)
        if not svn_id:
            svn_id = _SystemInfo._system_command_call(['git'] + svn_args_alt, env=env, cwd=arc_root)

        try:
            tag_info = _SystemInfo._system_command_call(['git'] + tag_args, env=env, cwd=arc_root).splitlines()
        except Exception:
            tag_info = [''.encode('utf-8')]

        try:
            branch_info = _SystemInfo._system_command_call(['git'] + branch_args, env=env, cwd=arc_root).splitlines()
        except Exception:
            branch_info = [''.encode('utf-8')]

        depth = six_.text_type(_GitVersion._get_git_depth(env, arc_root)).encode('utf-8')

        logger.debug('Git info commit:{}, author:{}, summary:{}, svn_id:{}'.format(commit, author, summary, svn_id))
        return [commit, author, summary, svn_id, tag_info[0], branch_info[0], depth]

    # YT's patch number.
    @staticmethod
    def _get_git_depth(env, arc_root):
        graph = {}
        full_history_args = ["log", "--full-history", "--format=%H %P", "HEAD"]
        history = _SystemInfo._system_command_call(['git'] + full_history_args, env=env, cwd=arc_root).decode('utf-8')

        head = None
        for line in history.splitlines():
            values = line.split()
            if values:
                if head is None:
                    head = values[0]
                graph[values[0]] = values[1:]

        assert head
        cache = {}
        stack = [(head, None, False)]
        while stack:
            commit, child, calculated = stack.pop()
            if commit in cache:
                calculated = True
            if calculated:
                if child is not None:
                    cache[child] = max(cache.get(child, 0), cache[commit] + 1)
            else:
                stack.append((commit, child, True))
                parents = graph[commit]
                if not parents:
                    cache[commit] = 0
                else:
                    for parent in parents:
                        stack.append((parent, commit, False))
        return cache[head]


class _TarVersion(_ArcVersion, _SvnVersion, _HgVersion):
    @classmethod
    def parse(cls, json_in):
        info = json.loads(json_in)
        vcs = info.get('repository_vcs', 'subversion')

        if vcs == 'mercurial':
            return cls._parse_as_hg(info)
        elif vcs == 'arc':
            return cls._parse_as_arc(info)

        return cls._parse_as_svn(info)

    @classmethod
    def _parse_as_hg(cls, info):
        svn_date = str(info['date'])
        try:
            svn_date, _ = _HgVersion._hg_to_svn_date_fmt(svn_date)
        except Exception:
            pass

        info = {
            'branch': str(info.get('branch')),
            'hash': str(info.get('hash')),
            'commit_author': str(info.get('author')),
            'date': svn_date,
        }
        info['scm_text'] = _HgVersion._format_scm_data(info)
        info['vcs'] = 'hg'
        info['vcs_ex'] = 'tar+hg'
        logger.debug('Tar + hg info:{}'.format(str(info)))
        return info

    @classmethod
    def _parse_as_arc(cls, info):
        svn_date = str(info['date'])
        try:
            svn_date, _ = _HgVersion._hg_to_svn_date_fmt(svn_date)
        except Exception:
            pass

        rev = info.get('last_revision')
        if rev is None or rev <= 0:
            rev = -1

        info = {
            'branch': str(info.get('branch')),
            'hash': str(info.get('hash')),
            'commit_author': str(info.get('author')),
            'date': svn_date,
            'revision': rev,
        }
        info['scm_text'] = _HgVersion._format_scm_data(info)
        info['vcs'] = 'arc'
        info['vcs_ex'] = 'tar+arc'
        logger.debug('Tar + arc info:{}'.format(str(info)))
        return info

    @classmethod
    def _parse_as_svn(cls, info):
        url = str(info.get('repository'))
        try:
            branch = cls._get_arcadia_branch(url) if url else None
        except Exception:
            branch = None
        try:
            tag = cls._get_arcadia_tag(url) if url else None
        except Exception:
            tag = None

        branch = branch or info.get('branch')
        tag = tag or info.get('tag')

        out = {
            'revision': int(info.get('revision')),
            'commit_author': str(info.get('author')),
            'commit_revision': int(info.get('last_revision')),
            'url': url,
            'commit_date': str(info.get('date')),
            'patch_number': info.get('patch_number', 0),
        }
        if branch is not None:
            out['branch'] = branch
        if tag is not None:
            out['tag'] = tag
        if info.get('hash') is not None:
            out['hash'] = info.get('hash')

        out['scm_text'] = _SvnVersion._format_scm_data(out)
        out['vcs'] = 'svn'
        out['vcs_ex'] = 'tar+svn'
        logger.debug('Tar + svn info:{}'.format(str(out)))
        return out

    @staticmethod
    def external_data(arc_root):
        return [open(os.path.join(arc_root, '__SVNVERSION__'), 'r').read().encode('utf-8')]


def _get_raw_data(vcs_type, vcs_root):
    lines = []
    if vcs_type == 'svn':
        lines = _SvnVersion.external_data(vcs_root)
    elif vcs_type == 'arc':
        lines = _ArcVersion.external_data(vcs_root)
    elif vcs_type == 'hg':
        lines = _HgVersion.external_data(vcs_root)
    elif vcs_type == 'git':
        lines = _GitVersion.external_data(vcs_root)
    elif vcs_type == 'tar':
        lines = _TarVersion.external_data(vcs_root)

    return [six_.ensure_str(line) for line in lines]


def _get_vcs_dictionary(vcs_type, *arg):
    if vcs_type == 'svn':
        return _SvnVersion.parse(*arg)
    elif vcs_type == 'arc':
        return _ArcVersion.parse(*arg)
    elif vcs_type == 'hg':
        return _HgVersion.parse(*arg)
    elif vcs_type == 'git':
        return _GitVersion.parse(*arg)
    elif vcs_type == 'tar':
        return _TarVersion.parse(*arg)
    else:
        raise Exception("Unknown VCS type {}".format(str(vcs_type)))


def _get_default_dictionary():
    return _ArcVersion.parse(
        *[
            '''{
        "repository": "arcadia",
        "summary":"No VCS",
        "date":"2015-03-14T06:05:35Z",
        "hash":"THIS_REVISION_IS_A_DUMMY",
        "branch":"unknown-vcs-branch",
        "remote":"users/plus/infinity",
        "author":"ordinal",
        "patch_number": 0
    }''',
            -1,
            0,
            '',
        ]
    )


def _get_default_json():
    return _get_default_dictionary(), ""


def _get_json(arc_root):
    arc_root = detect_root(arc_root)
    try:
        vcs_type, vcs_root, _ = detect([arc_root], check_tar=True)
        if vcs_root:
            vcs_root = arc_root
        else:
            raise Exception("Arcadia root '{}' is not subdir of vcs root {}".format(arc_root, vcs_root))
        info = _get_vcs_dictionary(vcs_type[0], *_get_raw_data(vcs_type[0], vcs_root))
        return info, vcs_root
    except Exception as e:
        if not getattr(e, 'tame', False):
            logger.debug('Cannot get vcs information {} [{}]: \n'.format(format_exc(), str(e)))
        return _get_default_json()


def repo_config(arc_root):
    info, _ = _get_json(arc_root)
    return info.get(
        'revision', info.get('commit_revision', info.get('svn_commit_revision', -1))
    ), get_svn_path_from_url(info.get('url', info.get('svn_url', '')))


def get_raw_version_info(arc_root, bld_root=None):
    info, _ = _get_json(arc_root)
    return info


def get_version_info(arc_root, bld_root, fake_data=False, fake_build_info=False, custom_version="", release_version=""):
    info, vcs_root = _get_default_json() if fake_data else _get_json(arc_root)
    return _dump_json(
        vcs_root,
        info,
        other_data=_SystemInfo.get_other_data(
            src_dir=vcs_root,
            build_dir=bld_root,
            fake_build_info=fake_build_info,
        ),
        build_user=_SystemInfo.get_user(fake_build_info=fake_build_info),
        build_host=_SystemInfo.get_hostname(fake_build_info=fake_build_info),
        build_date=_SystemInfo.get_date(0 if fake_build_info else None),
        build_timestamp=_SystemInfo.get_timestamp(fake_build_info=fake_build_info),
        custom_version=custom_version,
        release_version=release_version,
    )


if __name__ == '__main__':
    sys.stdout.write(get_version_info(sys.argv[1], sys.argv[2]) + '\n')
