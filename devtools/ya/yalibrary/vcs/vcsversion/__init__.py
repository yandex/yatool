# coding: utf-8
import abc
import builtins
import calendar
import dataclasses
import datetime
import functools
import json
import locale
import logging
import os
import re
import six
import socket
import subprocess
import sys
import time
import typing

import exts.process

import yalibrary.find_root
import yalibrary.tools
import yalibrary.vcs


logger = logging.getLogger(__name__)

DEFAULT_VCS_REVISION: int = -1
DEFAULT_VCS_PATCH_NUMBER: int = 0
INDENT: str = ' ' * 4


try:
    from exts.windows import on_win
except ImportError:

    def on_win():
        return os.name == 'nt'


try:
    import yalibrary.svn

except ImportError:
    logger.info('Svn library is not available in this run, skipping import')
    # if we can't import this modules, we aren't use methdos depends on SVN


WindowsError: type[OSError] = getattr(builtins, 'WindowsError', OSError)
ParsedVcsInfo = dict[str, str | int]


class VcsDetectError(Exception):
    pass


class UnknownVcsTypeError(Exception):
    pass


@dataclasses.dataclass
class ArcRevisionInfo:
    revision: str
    patch_number: str
    dirty: bool

    def __str__(self) -> str:
        return f'last svn:{self.revision}, patch_number:{self.patch_number}, dirty:{self.dirty}'


@dataclasses.dataclass
class SvnRevisionInfo:
    vcs_info: ParsedVcsInfo
    root: str


@dataclasses.dataclass
class VCSData:
    VCS_TOOL_NAME: typing.ClassVar[str | None] = None

    @abc.abstractmethod
    def parse(self) -> ParsedVcsInfo:
        pass

    @classmethod
    @abc.abstractmethod
    def from_repository_root(cls, vcs_root: str) -> typing.Self:
        pass

    @classmethod
    def from_repository_root_fast(cls, vcs_root: str, timeout: int | None = None) -> typing.Self:
        return cls.from_repository_root(vcs_root)

    @classmethod
    @functools.cache
    def _vcs_tool_path(cls):
        return yalibrary.tools.tool(cls.VCS_TOOL_NAME) if cls.VCS_TOOL_NAME else None

    @classmethod
    def _execute_command(cls, args: list[str], timeout: int | None = None, **kwargs: typing.Any) -> typing.Any:
        logger.debug(
            'Run %s %s in directory %s with env %s',
            cls.VCS_TOOL_NAME,
            ' '.join(args),
            kwargs.get('cwd', ''),
            kwargs.get('env', ''),
        )
        tool_path = cls._vcs_tool_path()
        if tool_path is None:
            logger.warning('%s does not have a command module configured', cls.__name__)
            return '{}'

        cmd = [tool_path] + list(args)
        proc = exts.process.popen(
            cmd, **kwargs, creationflags=0, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE
        )
        result, error = exts.process.wait_for_proc(proc, timeout=timeout)
        if error:
            logger.debug('Got an error while executing %s: %s', cmd, error)
        return result


@dataclasses.dataclass
class ArcInfo(VCSData):
    VCS_TOOL_NAME: typing.ClassVar[str] = 'arc'
    _arc_info_cache: typing.ClassVar[dict[str, str | None]] = {}

    info_output_string: str = ''
    revision: int = DEFAULT_VCS_REVISION
    patch_number: int = DEFAULT_VCS_PATCH_NUMBER
    dirty: bool = False

    @classmethod
    def from_repository_root(cls, vcs_root: str) -> typing.Self:
        arc_json_out = cls._get_arc_info(vcs_root)
        revision_info = cls._get_last_svn_revision(vcs_root)
        logger.debug('Arc info: %s, %s', arc_json_out, str(revision_info))

        return cls.from_revision_info(arc_info_string=arc_json_out, revision_info=revision_info)

    @classmethod
    def from_revision_info(cls, arc_info_string: str, revision_info: ArcRevisionInfo) -> typing.Self:
        return cls(
            info_output_string=arc_info_string,
            revision=int(revision_info.revision),
            patch_number=int(revision_info.patch_number),
            dirty=revision_info.dirty,
        )

    def parse(self) -> ParsedVcsInfo:
        info = json.loads(self.info_output_string)
        if 'author' in info:
            info['commit_author'] = info['author']
            del info['author']

        t_pattern = '%Y-%m-%dT%H:%M:%SZ'
        try:
            date_utc = datetime.datetime.strptime(info['date'], t_pattern)
            info['timestamp'] = calendar.timegm(date_utc.timetuple())
            info['date'] = date_utc.strftime('%Y-%m-%dT%H:%M:%S.%fZ')
        except Exception:
            logger.debug('Incorrect date format %s for date %s', t_pattern, info['date'])

        info['svn_commit_revision'] = int(self.revision)
        info['scm_text'] = self._format_scm_data(info)
        info['vcs'] = 'arc'

        info['dirty'] = 'dirty' if self.dirty else ''
        info['patch_number'] = int(self.patch_number)
        logger.debug('Arc info: %s', info)

        return info

    @staticmethod
    def _format_scm_data(info: dict[str, str]) -> str:
        scm_lines = [
            'Arc info:',
            f'{INDENT}Branch: {info.get('branch', '')}',
            f'{INDENT}Commit: {info.get('hash', '')}',
            f'{INDENT}Author: {info.get('commit_author', '')}',
            f'{INDENT}Summary: {info.get('summary', '')}',
        ]

        if 'svn_commit_revision' in info:
            scm_lines.append(f'{INDENT}Last Changed Rev: {str(info['svn_commit_revision'])}')
        if 'date' in info:
            scm_lines.append(f'{INDENT}Last Changed Date: {info['date']}')

        return '\n'.join(scm_lines)

    @classmethod
    def from_repository_root_fast(cls, vcs_root: str, timeout: int | None = None) -> typing.Self:
        arc_json_out = cls._get_arc_info(vcs_root, timeout=timeout)

        return cls(
            info_output_string=arc_json_out,
            revision=DEFAULT_VCS_REVISION,
            patch_number=DEFAULT_VCS_PATCH_NUMBER,
            dirty=False,
        )

    @classmethod
    def _get_arc_info(cls, arc_root: str, *, timeout: int | None = None) -> str:
        key = arc_root
        if key in cls._arc_info_cache:
            return cls._arc_info_cache[key]

        env = os.environ.copy()
        env['TZ'] = ''
        arc_json_args = ['info', '--json']
        arc_json_out = cls._execute_command(arc_json_args, env=env, cwd=arc_root, timeout=timeout)

        logger.debug('Arc info: %s', arc_json_out)
        cls._arc_info_cache[key] = arc_json_out

        return arc_json_out

    @classmethod
    def _get_last_svn_revision(cls, arc_root: str) -> ArcRevisionInfo:
        env = os.environ.copy()
        env['TZ'] = ''
        describe = cls._execute_command(
            ['describe', '--svn', '--dirty', '--first-parent'], env=env, cwd=arc_root
        ).strip()

        info = describe.split('-')
        revision = info[0].replace('r', '') if info else str(DEFAULT_VCS_REVISION)
        patch_number = info[1] if len(info) > 1 and info[1].isdigit() else str(DEFAULT_VCS_PATCH_NUMBER)
        dirty = info[-1] == 'dirty'
        try:
            patch_number = (
                str(int(patch_number) + int(revision)) if revision != str(DEFAULT_VCS_REVISION) else patch_number
            )
        except ValueError:
            pass

        return ArcRevisionInfo(
            revision=revision.encode('utf-8'),
            patch_number=patch_number.encode('utf-8'),
            dirty=dirty,
        )


@dataclasses.dataclass
class SvnInfo(VCSData):
    VCS_TOOL_NAME: typing.ClassVar[str] = 'svn'
    xml: str

    @classmethod
    def from_repository_root(cls, vcs_root: str) -> typing.Self:
        svn_info_args = ['info', '--xml']
        xml = cls._execute_command(svn_info_args, cwd=vcs_root)
        logger.debug('Svn xml: %s', xml)
        return cls(xml=xml)

    def parse(self) -> ParsedVcsInfo:
        svn_info_out = self.xml.encode('utf-8') if isinstance(self.xml, str) else self.xml
        info = self.svn_parse_info(svn_info_out)
        info['branch'] = get_branch(info['url'])
        info['tag'] = get_tag(info['url'])
        info['revision'] = int(info['revision'])
        info['commit_revision'] = int(info['commit_revision'])

        info['scm_text'] = self._format_scm_data(info)
        info['vcs'] = 'svn'
        return info

    # this method is similar to vcs.svn.run.parse_info
    @staticmethod
    def svn_parse_info(data: str) -> dict[str, str]:
        import xml.etree.ElementTree

        xml_root = xml.etree.ElementTree.fromstring(data)
        entry = xml_root.find('entry')
        if entry is None:
            return {}

        info = {
            'revision': entry.attrib['revision'],
            'url': entry.find('url').text,
            'repository_root': entry.find('repository').find('root').text,
            'depth': None,
        }

        commit = entry.find('commit')
        if commit is not None:
            date = commit.find('date').text
            date_utc = datetime.datetime.strptime(date, '%Y-%m-%dT%H:%M:%S.%fZ')
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

    @staticmethod
    def _format_scm_data(info: ParsedVcsInfo) -> str:
        return '\n'.join(
            [
                'Svn info:',
                f'{INDENT}URL: {info["url"]}',
                f'{INDENT}Last Changed Rev: {info["commit_revision"]}',
                f'{INDENT}Last Changed Author: {info["commit_author"]}',
                f'{INDENT}Last Changed Date: {info["commit_date"]}',
            ],
        )


@dataclasses.dataclass
class HgInfo(VCSData):
    VCS_TOOL_NAME: typing.ClassVar[str] = 'hg'
    output: str

    @classmethod
    def from_repository_root(cls, vcs_root: str) -> typing.Self:
        env = os.environ.copy()
        env['TZ'] = ''

        hg_args = ['--config', 'alias.log=log', '--config', 'defaults.log=', 'log', '-r', '.']
        output = cls._execute_command(hg_args, env=env, cwd=vcs_root)
        logger.debug('Hg log: %s', output)

        return cls(output=output)

    @staticmethod
    def _hg_to_svn_date_fmt(date: str) -> tuple[str, int]:
        # Wed Apr 24 11:19:30 2019 +0000
        t_pattern = '%a %b %d %H:%M:%S %Y +0000'
        date_utc = datetime.datetime.strptime(date, t_pattern)
        return date_utc.strftime('%Y-%m-%dT%H:%M:%S.%fZ'), calendar.timegm(date_utc.timetuple())

    def parse(self) -> ParsedVcsInfo:
        info = {
            'branch': self._get_hg_field('branch'),
            'hash': self._get_hg_field('changeset'),
            'commit_author': self._get_hg_field('user'),
            'commit_date': self._get_hg_field('date'),
            'summary': self._get_hg_field('summary'),
        }

        try:
            svn_date, svn_timestamp = self._hg_to_svn_date_fmt(info['commit_date'])
            info['commit_date'] = svn_date
            info['timestamp'] = svn_timestamp
        except Exception:
            logger.debug('Cannot convert to svn format date %s', info['commit_date'])

        info['scm_text'] = self._format_scm_data(info)
        info['vcs'] = 'hg'
        return info

    @staticmethod
    def _format_scm_data(info: ParsedVcsInfo) -> str:
        scm_data = '\n'.join(
            [
                'Hg info:',
                f'{INDENT}Branch: {info.get("branch", "")}',
                f'{INDENT}Last Changed Rev: {info.get("hash", "")}',
                f'{INDENT}Last Changed Author: {info.get("commit_author", "")}',
                f'{INDENT}Last Changed Date: {info.get("commit_date", "")}',
            ],
        )
        return scm_data

    def _get_hg_field(self, field: str) -> str:
        match = re.search(field + ':\\s*(.*)\n', self.output)
        if match:
            return match.group(1).strip()
        logger.debug('Unexpected format for field %s in %s', field, self.output)
        return ''


@dataclasses.dataclass
class GitInfo(VCSData):
    commit: str
    author: str
    summary: str
    svn_id: str
    tag_info: str
    branch_info: str
    depth: str

    @classmethod
    def from_repository_root(cls, vcs_root: str) -> typing.Self:
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
        commit = _SystemInfo._system_command_call(['git'] + hash_args, env=env, cwd=vcs_root).rstrip()
        author = _SystemInfo._system_command_call(['git'] + author_args, env=env, cwd=vcs_root)
        summary = _SystemInfo._system_command_call(['git'] + summary_args, env=env, cwd=vcs_root)
        svn_id = _SystemInfo._system_command_call(['git'] + svn_args, env=env, cwd=vcs_root)
        if not svn_id:
            svn_id = _SystemInfo._system_command_call(['git'] + svn_args_alt, env=env, cwd=vcs_root)

        try:
            tag_info = _SystemInfo._system_command_call(['git'] + tag_args, env=env, cwd=vcs_root).splitlines()
        except Exception:
            tag_info = [''.encode('utf-8')]

        try:
            branch_info = _SystemInfo._system_command_call(['git'] + branch_args, env=env, cwd=vcs_root).splitlines()
        except Exception:
            branch_info = [b'']

        depth = str(cls._get_git_depth(env, vcs_root))

        logger.debug('Git info commit:%s, author:%s, summary:%s, svn_id:%s', commit, author, summary, svn_id)

        return cls(
            commit=six.ensure_str(commit),
            author=six.ensure_str(author),
            summary=six.ensure_str(summary),
            svn_id=six.ensure_str(svn_id),
            tag_info=six.ensure_str(tag_info[0]),
            branch_info=six.ensure_str(branch_info[0]),
            depth=depth,
        )

    def parse(self) -> ParsedVcsInfo:
        info = {}
        info['hash'] = self.commit
        info['commit_author'] = self.author
        info['summary'] = self.summary

        if 'svn_commit_revision' not in info:
            url = re.search('git?-svn?-id: (.*)@(\\d*).*', self.svn_id)
            if url:
                info['svn_url'] = url.group(1)
                info['svn_commit_revision'] = int(url.group(2))

        if 'svn_commit_revision' not in info:
            rev = re.search('Revision: r?(\\d*).*', self.svn_id)
            if rev:
                info['svn_commit_revision'] = int(rev.group(1))

        info['tag'] = self.tag_info
        info['branch'] = self.branch_info
        info['scm_text'] = self._format_scm_data(info)
        info['vcs'] = 'git'

        if self.depth:
            info['patch_number'] = int(self.depth)
        return info

    @staticmethod
    def _format_scm_data(info: ParsedVcsInfo) -> str:
        scm_lines = [
            'Git info:',
            f'{INDENT}Commit: {info["hash"]}',
            f'{INDENT}Branch: {info["branch"]}',
            f'{INDENT}Author: {info["commit_author"]}',
            f'{INDENT}Summary: {info["summary"]}',
        ]
        if 'svn_commit_revision' in info or 'svn_url' in info:
            scm_lines.append(f'{INDENT}git-svn info:')
        if 'svn_url' in info:
            scm_lines.append(f'{INDENT}URL: {info['svn_url']}')
        if 'svn_commit_revision' in info:
            scm_lines.append(f'{INDENT}Last Changed Rev: {str(info['svn_commit_revision'])}')
        return '\n'.join(scm_lines)

    @staticmethod
    def _get_git_depth(env: dict, vcs_root: str) -> int:
        graph = {}
        full_history_args = ['log', '--full-history', '--format=%H %P', 'HEAD']
        history = _SystemInfo._system_command_call(['git'] + full_history_args, env=env, cwd=vcs_root).decode('utf-8')

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


@dataclasses.dataclass
class TarInfo(VCSData):
    content: str

    @classmethod
    def from_repository_root(cls, vcs_root: str) -> typing.Self:
        with open(os.path.join(vcs_root, '__SVNVERSION__'), 'r') as f:
            content = f.read()

        return cls(content=content)

    def parse(self) -> dict[str, str]:
        json_in = self.content
        info = json.loads(json_in)
        vcs = info.get('repository_vcs', 'subversion')

        if vcs == 'mercurial':
            return self._parse_as_hg(info)
        elif vcs == 'arc':
            return self._parse_as_arc(info)

        return self._parse_as_svn(info)

    @staticmethod
    def _parse_as_hg(info: dict[str, str]) -> dict[str, str]:
        svn_date = str(info['date'])
        try:
            svn_date, _ = HgInfo._hg_to_svn_date_fmt(svn_date)
        except Exception:
            pass

        info = {
            'branch': str(info.get('branch')),
            'hash': str(info.get('hash')),
            'commit_author': str(info.get('author')),
            'date': svn_date,
        }
        info['scm_text'] = HgInfo._format_scm_data(info)
        info['vcs'] = 'hg'
        info['vcs_ex'] = 'tar+hg'
        logger.debug('Tar + hg info: %s', str(info))
        return info

    @staticmethod
    def _parse_as_arc(info: dict[str, str]) -> dict[str, str]:
        svn_date = str(info['date'])
        try:
            svn_date, _ = HgInfo._hg_to_svn_date_fmt(svn_date)
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
        info['scm_text'] = HgInfo._format_scm_data(info)
        info['vcs'] = 'arc'
        info['vcs_ex'] = 'tar+arc'
        logger.debug('Tar + arc info: %s', str(info))
        return info

    @staticmethod
    def _parse_as_svn(info: dict[str, str]) -> dict[str, str]:
        url = str(info.get('repository'))
        try:
            branch = get_branch(url) if url else None
        except Exception:
            branch = None
        try:
            tag = get_tag(url) if url else None
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
            'patch_number': info.get('patch_number', DEFAULT_VCS_PATCH_NUMBER),
        }
        if branch is not None:
            out['branch'] = branch
        if tag is not None:
            out['tag'] = tag
        if info.get('hash') is not None:
            out['hash'] = info.get('hash')

        out['scm_text'] = SvnInfo._format_scm_data(out)
        out['vcs'] = 'svn'
        out['vcs_ex'] = 'tar+svn'
        logger.debug('Tar + svn info: %s', str(out))
        return out


def _dump_json(
    arc_root: str,
    info: ParsedVcsInfo,
    other_data: str | None = None,
    build_user: str | bytes | None = None,
    build_host: str | None = None,
    build_date: str | None = None,
    build_timestamp: int = 0,
    custom_version: str = '',
    release_version: str = '',
) -> str:
    j = {}
    j['PROGRAM_VERSION'] = str(info['scm_text']) + '\n' + str(six.ensure_str(other_data, errors='replace'))
    j['CUSTOM_VERSION'] = str(six.ensure_str(custom_version, errors='replace')) if custom_version else ''
    j['RELEASE_VERSION'] = str(six.ensure_str(release_version, errors='replace')) if release_version else ''
    j['SCM_DATA'] = info['scm_text']
    j['ARCADIA_SOURCE_PATH'] = six.ensure_str(arc_root, errors='replace')
    j['ARCADIA_SOURCE_URL'] = info.get('url', info.get('svn_url', ''))
    j['ARCADIA_SOURCE_REVISION'] = info.get('revision', DEFAULT_VCS_REVISION)
    j['ARCADIA_SOURCE_HG_HASH'] = info.get('hash', '')
    j['ARCADIA_SOURCE_LAST_CHANGE'] = info.get('commit_revision', info.get('svn_commit_revision', -1))
    j['ARCADIA_SOURCE_LAST_AUTHOR'] = info.get('commit_author', '')
    j['ARCADIA_PATCH_NUMBER'] = info.get('patch_number', DEFAULT_VCS_PATCH_NUMBER)
    j['BUILD_USER'] = six.ensure_str(build_user, errors='replace')
    j['BUILD_HOST'] = six.ensure_str(build_host, errors='replace')
    j['VCS'] = info.get('vcs', '')
    j['REPOSITORY'] = info.get('repository', '')
    j['BRANCH'] = info.get('branch', '')
    j['ARCADIA_TAG'] = info.get('tag', '')
    j['DIRTY'] = info.get('dirty', '')

    if 'url' in info or 'svn_url' in info:
        j['SVN_REVISION'] = info.get('svn_commit_revision', info.get('revision', DEFAULT_VCS_REVISION))
        j['SVN_ARCROOT'] = info.get('url', info.get('svn_url', ''))
        j['SVN_TIME'] = info.get('commit_date', info.get('svn_commit_date', ''))

    j['BUILD_DATE'] = build_date
    j['BUILD_TIMESTAMP'] = build_timestamp

    return json.dumps(j, sort_keys=True, indent=4, separators=(',', ': '))


def _get_user_locale() -> list[str]:
    try:
        return [locale.getencoding()]
    except Exception:
        return []


class _SystemInfo:  # TODO: (nkh) this is not class, it's a module (fix coming next commit)
    DEFAULT_LOCALE: str = 'utf-8'
    LOCALE_LIST: list[str] = _get_user_locale() + [sys.getfilesystemencoding(), DEFAULT_LOCALE]

    @staticmethod
    def get_user(fake_build_info: bool = False) -> str:
        if fake_build_info:
            return 'unknown'
        sys_user = os.environ.get('USER')
        if not sys_user:
            sys_user = os.environ.get('USERNAME')
        if not sys_user:
            sys_user = os.environ.get('LOGNAME')
        if not sys_user:
            sys_user = 'Unknown user'
        return sys_user

    @staticmethod
    def get_hostname(fake_build_info: bool = False) -> str:
        if fake_build_info:
            return 'localhost'
        hostname = socket.gethostname()
        if not hostname:
            hostname = 'No host information'
        return hostname

    @staticmethod
    def get_date(stamp: float | None = None) -> str:
        return time.strftime('%Y-%m-%dT%H:%M:%S.000000Z', time.gmtime(stamp))

    @staticmethod
    def get_timestamp(fake_build_info: bool = False) -> int:
        if fake_build_info:
            return 0
        # Unix timestamp.
        return int(time.time())

    @staticmethod
    def get_other_data(
        src_dir: str,
        build_dir: str,
        data_file: str = 'local.ymake',
        fake_build_info: bool = False,
    ) -> str:
        other_data = '\n'.join(
            [
                'Other info:',
                f'{INDENT}Build by: {_SystemInfo.get_user(fake_build_info)}',
                f'{INDENT}Top src dir: {os.curdir if fake_build_info else src_dir}',
                f'{INDENT}Top build dir: {os.curdir if fake_build_info else build_dir}',
                f'{INDENT}Hostname: {_SystemInfo.get_hostname(fake_build_info)}'
                f'{INDENT}Host information: \n{_SystemInfo._get_host_info(fake_build_info)}',
                f'{INDENT} {_SystemInfo._get_local_data(src_dir, data_file)}',
            ],
        )
        logger.debug('Other data: %s', other_data)

        return other_data

    @staticmethod
    def _get_local_data(src_dir: str, data_file: str) -> str:
        ymake_lines = []
        fymake = os.path.join(src_dir, data_file)
        if os.path.exists(fymake):
            with open(fymake, 'r') as local_ymake_file:
                ymake_lines.append(f'{INDENT}{data_file}:')
                for line in local_ymake_file:
                    ymake_lines.append(f'{INDENT}{INDENT}{line}')
        return '\n'.join(ymake_lines)

    @staticmethod
    def _get_host_info(fake_build_info: bool = False) -> str:
        host_info: str | bytes | None
        if fake_build_info:
            host_info = '*sys localhost 1.0.0 #dummy information '
        elif not on_win():
            host_info = ' '.join(os.uname())
        else:
            host_info = _SystemInfo._system_command_call('VER')  # XXX: check shell from cygwin to call VER this way!

        if not host_info:
            return ''

        text_host_info = six.ensure_str(host_info, errors='replace')

        return f'{INDENT}{INDENT}{text_host_info.strip()}\n'

    @staticmethod
    def _system_command_call(command: str | list[str], **kwargs: typing.Any) -> str | bytes | None:
        if isinstance(command, list):
            command = subprocess.list2cmdline(command)
        try:
            process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True, **kwargs)
            stdout, stderr = process.communicate()
            if process.returncode != 0:
                logger.debug('%s\nRunning %s failed with exit code %s\n', stderr, command, process.returncode)

                from yalibrary.svn import SvnRuntimeError

                raise SvnRuntimeError(stdout=stdout, stderr=stderr, rc=process.returncode, cmd=[command])
            return stdout
        except OSError as e:
            msg = e.strerror
            errcodes = f'error {e.errno}'
            if on_win() and isinstance(e, WindowsError):
                errcodes += f', win-error {e.winerror}'
                try:
                    import ctypes

                    msg = ctypes.FormatError(e.winerror).encode('utf-8', errors='replace')
                except ImportError:
                    pass
            logger.debug('System command call %s failed [%s]: %s\n', command, errcodes, msg)
            return None


def get_branch_or_tag(url: str, keywords: list[str]) -> str:
    parts = url.split('/')[3:]  # skip schema (2) and hostname (1)
    root_indices = [i for i, p in enumerate(parts) if p == 'arcadia']
    keyword_indices = [i for i, p in enumerate(parts) if p in keywords]
    if root_indices and keyword_indices and keyword_indices[0] < root_indices[0]:
        return '/'.join(parts[keyword_indices[0] + 1 : root_indices[0]])

    return 'trunk'


def get_branch(url: str) -> str:
    return get_branch_or_tag(url, ['branches', 'tags'])


def get_tag(url: str) -> str:
    tag = get_branch_or_tag(url, ['tags'])
    return tag if tag != 'trunk' else ''


VCS_TO_HANDLER: dict[str, type[VCSData]] = {
    'arc': ArcInfo,
    'svn': SvnInfo,
    'hg': HgInfo,
    'git': GitInfo,
    'tar': TarInfo,
}


def get_vcs_handler(vcs_type: str) -> type[VCSData]:
    try:
        return VCS_TO_HANDLER[vcs_type]
    except KeyError:
        raise UnknownVcsTypeError(f'Unknown vcs type: {vcs_type}')


def _get_default_dictionary() -> ParsedVcsInfo:
    return ArcInfo.from_revision_info(
        arc_info_string='''{
        "repository": "arcadia",
        "summary":"No VCS",
        "date":"2015-03-14T06:05:35Z",
        "hash":"THIS_REVISION_IS_A_DUMMY",
        "branch":"unknown-vcs-branch",
        "remote":"users/plus/infinity",
        "author":"ordinal",
        "patch_number": 0
    }''',
        revision_info=ArcRevisionInfo(
            revision=DEFAULT_VCS_REVISION,
            patch_number=DEFAULT_VCS_PATCH_NUMBER,
            dirty=False,
        ),
    ).parse()


def _get_default_json() -> SvnRevisionInfo:
    return SvnRevisionInfo(vcs_info=_get_default_dictionary(), root='')


def _get_json(arc_root: str | None) -> SvnRevisionInfo:
    arc_root: str | None = yalibrary.find_root.detect_root(arc_root)
    try:
        if arc_root is None:
            raise VcsDetectError('VCS root is None, can\'t proceed')
        vcs_type, vcs_root, _ = yalibrary.vcs.detect([arc_root], check_tar=True)
        if vcs_root:
            vcs_root = arc_root
        else:
            raise VcsDetectError(f'Arcadia root \'{arc_root}\' is not subdir of vcs root {vcs_root}')
        handler = get_vcs_handler(vcs_type[0])
        info = handler.from_repository_root(vcs_root).parse()

        return SvnRevisionInfo(vcs_info=info, root=vcs_root)
    except Exception as e:
        logger.debug('Cannot get vcs information', exc_info=e)
        return _get_default_json()


def repo_config(arc_root: str | None) -> tuple[int, str]:
    info = _get_json(arc_root).vcs_info
    return info.get(
        'revision', info.get('commit_revision', info.get('svn_commit_revision', -1))
    ), yalibrary.svn.get_svn_path_from_url(info.get('url', info.get('svn_url', '')))


def get_raw_version_info(arc_root: str | None, bld_root=None) -> ParsedVcsInfo:
    info = _get_json(arc_root).vcs_info
    return info


def get_fast_version_info(arc_root: str, timeout: int | None = None) -> dict[str, str | int]:
    try:
        if arc_root is None:
            raise VcsDetectError('VCS root is None, can\'t proceed')
        vcs_type, vcs_root, _ = yalibrary.vcs.detect([arc_root], check_tar=True)
        if vcs_root:
            vcs_root = arc_root
        else:
            raise VcsDetectError(f'Arcadia root \'{arc_root}\' is not subdir of vcs root {vcs_root}')
        handler = get_vcs_handler(vcs_type[0])
        vcs_data = handler.from_repository_root_fast(vcs_root, timeout=timeout)
        info = handler.parse(vcs_data)

        return info
    except Exception as e:
        logger.debug('Cannot get vcs information', exc_info=e)
        return _get_default_json().vcs_info


def get_version_info(
    arc_root: str | None,
    bld_root: str | None,
    fake_data: bool = False,
    fake_build_info: bool = False,
    custom_version: str = '',
    release_version: str = '',
) -> str:
    vcs_info = _get_default_json() if fake_data else _get_json(arc_root)
    return _dump_json(
        vcs_info.root,
        vcs_info.vcs_info,
        other_data=_SystemInfo.get_other_data(
            src_dir=vcs_info.root,
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
