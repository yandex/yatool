import os
import sys
import json
import base64
import random
import string
import asyncio
import hashlib
import tempfile
import subprocess

from urllib.request import urlopen
from urllib.request import Request


SOURCE_ROOT = sys.argv[1]
BUILD_ROOT = sys.argv[2]

URL = "https://devtools-registry.s3.yandex.net/5316269301"
SHA256 = "ed48695fb5e27afe515a5a4e2eada4201eca1e603506c03d20c536df865c7d30"


_ssl_is_tuned = False


def _tune_ssl():
    global _ssl_is_tuned
    if _ssl_is_tuned:
        return
    try:
        import ssl

        ssl._create_default_https_context = ssl._create_unverified_context
    except AttributeError:
        pass

    try:
        import urllib3

        urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)
    except (AttributeError, ImportError):
        pass
    _ssl_is_tuned = True


def uniq(size=6):
    return ''.join(random.choice(string.ascii_lowercase + string.digits) for _ in range(size))


def _fetch(url, into):
    _tune_ssl()
    request = Request(str(url))
    request.add_header('User-Agent', 'ya-bootstrap')

    sha256 = hashlib.sha256()
    sys.stderr.write('Downloading %s ' % url)
    sys.stderr.flush()
    conn = urlopen(request, timeout=10)
    sys.stderr.write('[')
    sys.stderr.flush()
    try:
        with open(into, 'wb') as f:
            while True:
                block = conn.read(1024 * 1024)
                sys.stderr.write('.')
                sys.stderr.flush()
                if block:
                    sha256.update(block)
                    f.write(block)
                else:
                    break
        return sha256.hexdigest()

    finally:
        sys.stderr.write('] ')
        sys.stderr.flush()


def _atomic_fetch(url, into, sha256):
    tmp_dest = into + '.' + uniq()
    try:
        real_sha = _fetch(url, tmp_dest)
        if real_sha != sha256:
            raise Exception('SHA256 mismatched: %s differs from %s' % (real_sha, sha256))
        os.rename(tmp_dest, into)
        sys.stderr.write('OK\n')
    except Exception as e:
        sys.stderr.write('ERROR: ' + str(e) + '\n')
        raise
    finally:
        try:
            os.remove(tmp_dest)
        except OSError:
            pass


def _get(url, shasum):
    RETRIES = 5
    dest_path = tempfile.mktemp()
    if not os.path.exists(dest_path):
        for iter in range(RETRIES):
            try:
                _atomic_fetch(url, dest_path, shasum)
                break
            except Exception:
                if iter + 1 == RETRIES:
                    raise
                else:
                    import time

                    time.sleep(iter)

    return dest_path


try:
    os.makedirs(BUILD_ROOT)
except Exception:
    pass

try:
    file = sys.argv[4]
except IndexError:
    file = None


with open(file or _get(URL, SHA256), 'r') as f:
    graph = json.loads(f.read())

by_platform = {}

for r in graph['conf']['resources']:
    by_platform[r['pattern']] = r

vcj = BUILD_ROOT + '/vcs.json'

if not os.path.isfile(vcj):
    with open(vcj, 'w') as f:
        js = by_platform['VCS']['resource']
        f.write(base64.b64decode(list(js.split(':'))[-1]).decode())

by_uid = {}

for n in graph['graph']:
    by_uid[n['uid']] = n


REMOVAL_CANDIDATES = ["--ya-start-command-file", "--ya-end-command-file"]


PATH_SUBSTITUTES = {
    '$(DEFAULT_LINUX_X86_64)/bin/': '',
    '$(PYTHON)/python': 'python3',
    '$(ANTLR4-sbr:1861632725)': BUILD_ROOT,
    '$(YMAKE_PYTHON3-1415908779)/python3': 'python3',
    '$(BINUTILS_ROOT-sbr:360916612)/bin/': '',
    '$(RESOURCE_ROOT)': BUILD_ROOT,
    '$(SOURCE_ROOT)': SOURCE_ROOT,
    '$(BUILD_ROOT)': f'{BUILD_ROOT}/@uid@',
    '$(TOOL_ROOT)': BUILD_ROOT,
    '$(VCS)': BUILD_ROOT,
    '$ORIGIN': SOURCE_ROOT,
}


def substitute_path(x):
    for k, v in PATH_SUBSTITUTES.items():
        x = x.replace(k, v)

    return x


def subst(x, kind):
    if x.endswith('/bin/java'):
        yield 'java'

        return

    for p in ('--sysroot=', '-B$(', '-fuse-ld'):
        if x.startswith(p):
            return

    if 'a' in kind:
        if x.endswith('fetch_from_sandbox.py'):
            yield f'{BUILD_ROOT}/fetch.py'

            return

    if x not in REMOVAL_CANDIDATES:
        yield substitute_path(x)


def iter_list(lst, kind):
    for x in lst:
        for r in subst(x, kind):
            yield check(r)


def fix_list(lst, kind):
    return list(iter_list(lst, kind))


def check(x):
    if '$' in x:
        raise Exception(f'bad path {x}')

    return x


def descr(n):
    return ' '.join(x[x.index('/') + 1 :] for x in n['outputs'])


def execute(n):
    try:
        execute_impl(n)
    except Exception as e:
        print(f'Exception caught {e}, {n}')
        raise
        # os._exit(1)


def substitute_uid(u, v):
    return v.replace('@uid@', f'{u[:2]}/{u}')


def iter_lst(node, lst):
    for x in lst:
        yield substitute_uid(node['uid'], x)


def iter_in(node):
    return iter_lst(node, fix_list(node['inputs'], 'i'))


def iter_out(node):
    return iter_lst(node, fix_list(node['outputs'], 'o'))


def iter_links(n):
    for d in n['deps']:
        dn = by_uid[d]

        for o in dn['outputs']:
            so = substitute_path(o)

            yield substitute_uid(d, so), substitute_uid(n['uid'], so)


def subst_n(n, v):
    return substitute_uid(n['uid'], substitute_path(v))


def execute_impl(n):
    for fr, to in iter_links(n):
        try:
            os.makedirs(os.path.dirname(to))
        except OSError:
            pass

        os.symlink(fr, to)

    for i in iter_in(n):
        if not os.path.isfile(i):
            raise Exception(f'missing {i}')

    outs = list(iter_out(n))

    for o in outs:
        try:
            os.makedirs(os.path.dirname(o))
        except OSError:
            pass

    for cmd in n['cmds']:
        args = iter_lst(n, fix_list(cmd['cmd_args'], 'a'))
        cwd = check(subst_n(n, cmd.get('cwd', BUILD_ROOT)))

        env = dict(**os.environ)
        env['ARCADIA_ROOT_DISTBUILD'] = SOURCE_ROOT

        out = subprocess.check_output(args, env=env, cwd=cwd)

        if stdout := cmd.get('stdout'):
            with open(check(subst_n(n, stdout)), 'wb') as f:
                f.write(out)

    for o in outs:
        if not os.path.isfile(o):
            raise Exception(f'no out {o}')

        os.chmod(o, 0o777)


async def gather(it):
    return await asyncio.gather(*list(it))


class Executor:
    def __init__(self, threads):
        self.s = set()
        self.d = {}
        self.jobs = asyncio.Semaphore(threads)
        self.c = 0

    def descr(self, n):
        u = n['uid']

        if u not in self.d:
            self.d[u] = {
                'l': asyncio.Lock(),
                'v': False,
            }

        return self.d[u]

    async def visit(self, res):
        await gather(self.visit_node(by_uid[r]) for r in res)

    async def visit_node(self, n):
        d = self.descr(n)

        async with d['l']:
            if not d['v']:
                await self.do_visit(n)
                assert not d['v']
                d['v'] = True

    async def do_visit(self, n):
        u = n['uid']

        if u not in self.s:
            self.s.add(u)
            await self.exec_node(n)

    async def exec_node(self, n):
        await self.visit(n['deps'])

        async with self.jobs:
            self.progress(n)
            await asyncio.to_thread(execute, n)

    def progress(self, n):
        self.c += 1

        c = self.c
        a = len(by_uid)
        d = descr(n)

        print(f'[{c}/{a}] {d}', file=sys.stderr)


def run_node(result, jobs):
    # asyncio.run fails somehow
    asyncio.get_event_loop().run_until_complete(Executor(jobs).visit(result))


run_node(graph['result'], int(sys.argv[3]))
