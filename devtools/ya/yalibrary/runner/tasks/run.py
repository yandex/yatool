import errno
import logging
import math
import os
import subprocess
import threading
import time
import typing as tp

import exts.process
import exts.shlex2
import exts.windows
import six

import devtools.ya.test.const as const
import six.moves.queue as Queue
import yalibrary.runner
import yalibrary.worker_threads as worker_threads

from devtools.libs.parse_number.python import parse_number
from exts import strings
from exts import hashing
from exts.fs import create_dirs, ensure_removed, hardlink_tree, remove_tree_with_perm_update
from yalibrary import formatter
from yalibrary.runner.build_root import BuildRootError

if tp.TYPE_CHECKING:
    from yalibrary.runner.build_root import BuildRoot  # noqa
from yalibrary.runner.tasks.enums import WorkerPoolType
from yalibrary.runner.timeline_store import DetailedTimelineStore, DetailedStages


logger = logging.getLogger(__name__)


INTERNAL_ERROR_EXIT_CODE = 3

_MISSING_INPUTS = [  # TODO: fix all
    '$S/contrib/tools/python/src/Include/longintrepr.h',
    '$S/contrib/tools/flex-old/FlexLexer.h',  # for kernel/remorph/tokenlogic
    '$S/contrib/tools/bison/data',
    '$S/contrib/tools/python/src/Lib',
    '$S/contrib/python/lxml/lxml',
    '$S/contrib/tools/cython',
    '$S/build',
]

STOP_SUFFIXES = (
    ' has no symbols',
    'define global symbols)',
    'or duplicate input files)',
    "mkstemp'",
    'where xiar is needed)',
    "xiar: executing 'ar'",
    "xiar: executing 'x86_64-k1om-linux-ar'",
    "xiar: executing 'k1om-mpss-linux-ar'",
    'Copyright (C) Microsoft Corporation.  All rights reserved.',
)

STOP_PREFIXES = ('Microsoft (R) Library Manager Version',)


def parse_cpu_requirement(cpu_requirement):
    """
    parse cpu_requirement in https://en.wikipedia.org/wiki/Metric_prefix#List_of_SI_prefixes format
    """
    cpu = float(parse_number.parse_human_readable_number(str(cpu_requirement)))
    return math.ceil(cpu)


def fmt_node(node, tags=None, status=None):
    from yalibrary.status_view.helpers import fmt_node as _fmt_node

    return _fmt_node(node.inputs, node.outputs, node.kv, tags, status)


def create_strict_inputs_root(base_patterns, patterns, inputs):
    fake_source_root = os.path.join(patterns.build_root(), 'source_root')
    create_dirs(fake_source_root)
    patterns['SOURCE_ROOT'] = fake_source_root

    for inp in set(inputs + _MISSING_INPUTS):
        fixed_inp = inp.replace('$S', '$(SOURCE_ROOT)')  # XXX, fix ymake
        input_abs_path = base_patterns.fill(fixed_inp)
        if input_abs_path.startswith(base_patterns.source_root()):
            fake_input_abs_path = patterns.fill(fixed_inp)
            if os.path.exists(input_abs_path) and input_abs_path != fake_input_abs_path:
                ensure_removed(fake_input_abs_path)
                create_dirs(os.path.dirname(fake_input_abs_path))
                hardlink_tree(input_abs_path, fake_input_abs_path)


def _fix_output(out, rmap, mask_roots=False):
    source_root = rmap.source_root()
    build_root = rmap.build_root()
    atd_root = rmap.atd_root()
    tool_root = rmap.tool_root()
    resource_root = rmap.resource_root()

    def valid_line(line):
        if any(line.endswith(suffix) for suffix in STOP_SUFFIXES):
            return False
        if any(line.startswith(prefix) for prefix in STOP_PREFIXES):
            return False
        return True

    def transform_line(line):
        if mask_roots:
            line = line.replace(source_root, '$(SOURCE_ROOT)')
            line = line.replace(resource_root, '$(RESOURCE_ROOT)')
            line = line.replace(build_root, '$(BUILD_ROOT)')
            line = line.replace(atd_root, '$(TESTS_DATA_ROOT)')
            line = line.replace(tool_root, '$(TOOL_ROOT)')

        return line

    out = formatter.ansi_codes_to_markup(out)
    return '\n'.join(
        transform_line(six.ensure_str(fl)) for fl in (line for line in out.splitlines() if valid_line(line))
    )


def log_for_text_file_busy(bin_path: str):
    my_pid = os.getpid()
    my_pgid = os.getpgid(my_pid)
    logger.debug('my_pid=%s, my_pgid=%s', my_pid, my_pgid)
    found_proc = None

    for f, proc in exts.process.find_opened_file_across_all_procs(bin_path):
        logger.debug(
            'Text file busy details: %s (with mode %s) is busy by %s with pid %s', f.path, f.mode, proc.name(), proc.pid
        )

        if proc.pid != my_pid:
            found_proc = proc
            break

    # see https://a.yandex-team.ru/arcadia/devtools/ya/cpp/lib/pgroup.cpp?rev=r16256072#L20
    # In local scenario this check will probably resolve to true due to the fact that both processes are in the same group (probably terminal)
    is_tty = any([os.isatty(fd) for fd in (0, 1, 2)])
    if not is_tty and found_proc:
        logger.debug(
            'found process in same process group: %s', exts.process.is_process_in_subtree(found_proc.pid, my_pgid)
        )


class TextFileBusyError(Exception):
    mute = True


class ExecutorBase(object):
    text_file_busy_retries = 10
    text_file_busy_retry_delay = 0.1

    prefix = "##"
    status_prefix = "##status##"
    append_prefix = '##append_tag##'
    prefix_len = len(prefix)
    status_prefix_len = len(status_prefix)
    append_prefix_len = len(append_prefix)

    def __init__(self, state, display_func, set_status_func, append_tag_func):
        self._state = state
        self._display_func = display_func
        self._set_status_func = set_status_func
        self._append_tag_func = append_tag_func

    def run(self, **kwargs):
        raise NotImplementedError()


class PopenExecutor(ExecutorBase):
    _close_fds = not exts.windows.on_win()

    def run(self, **kwargs):
        retries = self.text_file_busy_retries
        stderr = ""
        exit_code = 1
        while retries > 0:
            try:
                stderr, exit_code = self._run_process(**kwargs)
                return stderr, exit_code
            except OSError as e:
                retries -= 1
                if e.errno != errno.ETXTBSY or not retries:
                    raise

                if retries == 0:
                    log_for_text_file_busy(kwargs['args'][0])
                    break
                else:
                    logger.warning("Text file busy, retrying...")
                    time.sleep(self.text_file_busy_retry_delay)

        raise TextFileBusyError(stderr)

    def _run_process(self, args, stdout, env, cwd, nice, **kwargs):
        proc = None

        def set_nice():
            try:
                if not exts.windows.on_win():
                    return os.nice(nice)
            except OSError:
                pass

        def cancel_cb():
            if proc:
                logger.debug('Terminating %s', proc.pid)
                proc.terminate()
                logger.debug('Waiting %s', proc.pid)
                proc.wait()
                logger.debug('Cancelled %s', proc.pid)

        def readline(f, queue):
            while True:
                line = six.ensure_str(f.readline())

                queue.put(line)

                if not line:
                    break

            f.close()

        with self._state.with_finalizer(cancel_cb):
            proc = exts.process.popen(
                args,
                stderr=subprocess.PIPE,
                stdout=stdout,
                env=env,
                cwd=cwd,
                close_fds=self._close_fds,
                preexec_fn=set_nice,
            )
            stderr = ""

            queue = Queue.Queue()
            read_thread = threading.Thread(target=readline, args=(proc.stderr, queue))
            read_thread.start()

            while True:
                if queue is not None:
                    try:
                        line = queue.get(True, 1)

                        if not line:
                            queue = None

                        if line.startswith(self.status_prefix):
                            status = line[self.status_prefix_len :].rstrip(os.linesep)
                            self._set_status_func(status)
                        elif line.startswith(self.append_prefix):
                            self._append_tag_func(line[self.append_prefix_len :].strip())
                        elif line.startswith(self.prefix):
                            self._display_func(line[self.prefix_len :].replace("|n", "\n"))
                        else:
                            stderr += line
                    except Queue.Empty:
                        pass
                elif proc.poll() is not None:
                    break

                self._state.check_cancel_state()

            self._state.check_cancel_state()

            return stderr, proc.returncode


class LocalExecutor(ExecutorBase):
    def run(self, **kwargs):
        retries = self.text_file_busy_retries
        stderr = ""
        exit_code = 1

        while retries > 0:
            stderr, exit_code = self._run_process(**kwargs)

            if exit_code != 0 and stderr.startswith("Process was not created: Text file busy"):
                retries -= 1

                if retries == 0:
                    log_for_text_file_busy(kwargs['args'][0])
                    break
                else:
                    logger.warning("Text file busy, retrying...")
                    time.sleep(0.1)
                    continue

            return stderr, exit_code
        raise TextFileBusyError(stderr)

    def _run_process(self, args, stdout, env, cwd, executor_address, requirements, nice, **kwargs):
        from devtools.executor.python import executor

        stderr = ""
        try:
            nice_arg = dict()
            if nice is not None:
                nice_arg = dict(nice=nice)

            with executor.run_external_process(
                executor_address, args, stdout.name, cwd, env, requirements=requirements, **nice_arg
            ) as res:
                for line in res.iter_stderr():
                    if line.startswith(self.status_prefix):
                        status = line[self.status_prefix_len :].rstrip(os.linesep)
                        self._set_status_func(status)
                    elif line.startswith(self.append_prefix):
                        self._append_tag_func(line[self.append_prefix_len :].strip())
                    elif line.startswith(self.prefix):
                        self._display_func(line[self.prefix_len :].replace("|n", "\n"))
                    else:
                        stderr += line
                return stderr, res.returncode
        except executor.ShutdownException:
            return stderr, 1


class RunNodeTask(object):
    node_type = 'RunNode'
    worker_pool_type = WorkerPoolType.BASE

    def __init__(
        self,
        node,
        build_root,  # type: BuildRoot
        ctx,
        threads,
        test_threads,
        execution_log,
        build_errors,
        display,
        preexec_fn,
        callback,
        cache,
        dist_cache,
        fuse_manager,
    ):
        self._node = node
        self._build_root = build_root
        self._ctx = ctx
        self._threads = threads
        self._test_threads = test_threads
        self._execution_log = execution_log
        self._build_errors = build_errors
        self._display = display
        self._preexec_fn = preexec_fn
        self._callback = callback
        self._cache = cache
        self._dist_cache = dist_cache

        self._detailed_timings = DetailedTimelineStore()
        self._stderr = None
        self.raw_stderr = None
        self._tags = self._node.tags[:]
        self._seen_tags = set()
        self._status = None
        self._patterns = ctx.patterns.sub()
        self._patterns['BUILD_ROOT'] = self._build_root.path
        self._exit_code = 0
        self._fuse_manager = fuse_manager
        self._executor = ctx.executor_type(
            self._ctx.state, self._display.emit_message, self.set_status, self.append_tag
        )
        self._self_uid_support = node.has_self_uid_support and not ctx.opts.clear_build and ctx.content_uids

    @property
    def build_root(self):
        return self._build_root

    @property
    def exit_code(self):
        return self._exit_code

    @property
    def uid(self):
        return self._node.uid

    def set_status(self, status):
        self._status = status

    def append_tag(self, tag):
        if tag not in self._seen_tags:
            self._tags.append(tag)
            self._seen_tags.add(tag)

    def execute(self):
        logging.debug('Run node %s in build root %s', self._node.uid, self._build_root.path)

        if 'func' in self._node.args:
            return self.execute_func()

        return self.execute_command()

    def execute_func(self):
        try:
            self._node.args['func'](self._patterns)
            return '', 0
        except yalibrary.runner.ExpectedNodeException as e:
            return str(e), e.exit_code
        except Exception:
            from traceback import format_exc

            self.raw_stderr = format_exc()
            raise

    def execute_command(self):
        errs = []
        exit_code = 0
        for cmd in self._node.commands(self._build_root.path):
            args = cmd['cmd_args']
            stdout = open(os.devnull, "w") if cmd['stdout'] is None else open(cmd['stdout'], 'w')

            env = cmd.get('env', {}) or {}

            this_env = os.environ.copy()
            if env:
                env = strings.ensure_str_deep(env)
                this_env.update(env)

            if self._ctx.opts.be_verbose:
                env_vars = ['{}={}'.format(k, v) for k, v in env.items()]
                full_cmd = ' '.join(map(six.ensure_str, list(map(exts.shlex2.quote, env_vars + args))))
                errs.append(full_cmd)  # TODO: not the best way to show command

            def prepare_tmp_dir():
                tmp_dir = create_dirs(os.path.join(self._build_root.path, 'r3tmp'))
                this_env['TMPDIR'] = tmp_dir
                this_env['TEMP'] = tmp_dir
                this_env['TMP'] = tmp_dir
                return tmp_dir

            tmp_dir = None
            cwd = cmd['cwd'] or self._build_root.path
            try:
                requirements = None
                if self._ctx.opts.private_net_ns:
                    requirements = {"network": self._node.requirements.get("network")}

                tmp_dir = prepare_tmp_dir()
                full_cmd = None
                if self._ctx.opts.detailed_args:
                    full_cmd = ' '.join(map(six.ensure_str, args))
                self._detailed_timings.start_stage(DetailedStages.EXECUTE_COMMAND, time.time(), cmd=full_cmd)
                stderr, exit_code = self._executor.run(
                    args=args,
                    stdout=stdout,
                    env=this_env,
                    cwd=cwd,
                    nice=self._ctx.opts.set_nice_value,
                    executor_address=self._ctx.executor_address,
                    requirements=requirements,
                )
                self._detailed_timings.start_stage(DetailedStages.POSTPROCESSING_COMMAND, time.time())

            except OSError as e:
                stderr = 'Process run failed: {}'.format(e)
                exit_code = 1
            finally:
                stdout.close()
                if tmp_dir and (not self._ctx.opts.keep_temps and exit_code == 0):
                    self._remove_dir(tmp_dir)

            if exit_code:
                c = ' '.join(map(six.ensure_str, args))
                errs.append('command {} failed with exit code [[imp]]{}[[rst]] in {}'.format(c, exit_code, cwd))

            if stderr:
                self.raw_stderr = stderr
                errs.append(stderr)
            if cmd.get('stderr'):
                with open(cmd['stderr'], 'w') as f:
                    f.write(stderr)

            if exit_code:
                break

        return '\n'.join(map(six.ensure_str, (strings.encode(s) for s in errs))), exit_code

    def _remove_dir(self, dir):
        remove_tree_with_perm_update(dir)

    def run(self):
        if self._ctx.opts.strict_inputs and not self._ctx.opts.sandboxing:
            create_strict_inputs_root(self._patterns, self._patterns, self._node.inputs)

        for output in self._node.outputs:
            dirname = os.path.dirname(self._patterns.fill(output))
            assert dirname, 'Wrong output ({}) from node with {} uid: {}'.format(
                output, self._node.uid, self._node.commands('')
            )
            create_dirs(dirname)

        start_time = time.time()
        full_stderr, exit_code = self.execute()
        finish_time = time.time()
        self._detailed_timings.start_stage(DetailedStages.EVALUATE_NODE_RESULTS, finish_time)

        if self._ctx.opts.show_timings:
            self._tags.append('%4.2f' % (finish_time - start_time) + 's elapsed')

        if exit_code:
            self._tags.append('[[bad]]FAILED[[rst]]')

        full_stderr = (
            _fix_output(full_stderr.strip(), self._patterns, mask_roots=self._ctx.opts.mask_roots)
            if not self._ctx.opts.do_not_output_stderrs
            else ''
        )

        if self._callback:
            self._callback(
                {
                    'uid': self._node.uid,
                    'status': exit_code,
                    'stderrs': [full_stderr],
                    'build_root': self._build_root.path,
                    'files': list(map(self._patterns.fill, self._node.outputs)),
                }
            )

        try:
            if not exit_code:
                self._build_root.validate()
        except BuildRootError as e:
            exit_code = INTERNAL_ERROR_EXIT_CODE
            last_cmd = self._node.commands(self._build_root.path)[-1]['cmd_args']
            full_stderr = "Command failed to pass build integrity check: {} in {}\n{}\nNode stderr tail:\n{}".format(
                ' '.join(last_cmd),
                self._build_root.path,
                str(e),
                full_stderr[-8 * 1024 :],
            )
            self._tags.append('[[bad]]FAILED[[rst]]')

        if self._node.dir_outputs:
            if self._ctx.opts.dir_outputs_test_mode:
                if not exit_code:
                    if self._node.stable_dir_outputs:
                        self._build_root.validate_dir_outputs()
                    self._build_root.extract_dir_outputs()
                if self._ctx.opts.runner_dir_outputs:
                    self._build_root.propagate_dir_outputs()
            else:
                for dir_output in self._node.dir_outputs:
                    dir_output = self._patterns.fill(dir_output)
                    for root, dirs, files in os.walk(dir_output):
                        root = root.replace(self._build_root.path, "$(BUILD_ROOT)")
                        for f in files:
                            self._build_root.add_output(os.path.join(root, f))

        if self._supports_build_time_cache():
            self._ctx.build_time_cache.touch(self._node.static_uid, id=int(finish_time - start_time))

        return six.ensure_str(full_stderr, errors='replace'), exit_code, (start_time, finish_time)

    def __call__(self, deps):
        self._detailed_timings.start_stage(DetailedStages.SETUP_NODE)
        self._build_root.create()

        root = self._build_root.path

        have_broken = False
        err_msg = None

        if self._ctx.opts.clear_build:
            self._ctx.clear_uid(self._node.uid)

        if any(self._node.unresolved_patterns()):
            have_broken = True
            err_msg = 'unresolved patterns: ' + ', '.join(map(six.ensure_str, self._node.unresolved_patterns()))

        cached_by_content_uid = False
        if not have_broken:
            if self._self_uid_support:
                uids_hashes = [self._node.self_uid]
                all_deps_have_outputs_uid = True

                for dep in deps:
                    if not hasattr(dep, '_node'):  # Because it can have not node object
                        continue
                    output_digests = dep._node.output_digests
                    if output_digests is not None:
                        uids_hashes.append(output_digests.outputs_uid)
                    else:
                        all_deps_have_outputs_uid = False
                        break

                if all_deps_have_outputs_uid:
                    self._node.content_uid = hashing.sum_hashes(uids_hashes)
                    start_time = time.time()
                    if self._cache.try_restore(self._node.content_uid, self._build_root.path):
                        cached_by_content_uid = True
                    elif self._dist_cache and self._dist_cache.has(self._node.content_uid):
                        cached_by_content_uid = self._dist_cache.try_restore(
                            self._node.content_uid, self._build_root.path
                        )
                    finish_time = time.time()

            if not cached_by_content_uid:
                builded_deps_uids = set()

                for x in deps:
                    if hasattr(x, 'build_root') and x.build_root.ok:
                        assert x.build_root.steal(root), "Broken build_root.steal for uid {!s}, deps {!s}".format(
                            self._node.uid, [px.uid if hasattr(px, 'uid') else str(px) for px in deps]
                        )
                        builded_deps_uids.add(x.uid)

                if builded_deps_uids != set(self._node.deps) and not self._node.ignore_broken_dependencies:
                    have_broken = True
                    err_msg = 'can not build one or more deps: ' + ', '.join(
                        map(six.ensure_str, set(self._node.deps) - builded_deps_uids)
                    )

        self._execution_log[self._node.uid] = {}

        if have_broken:
            self._tags.append('BROKEN_BY_DEPS')
            self._stderr, self._exit_code, timing = six.ensure_str(err_msg), 1, None
        elif cached_by_content_uid:
            self._build_root.validate()
            self._stderr, self._exit_code, timing = '', 0, (start_time, finish_time)
            self._execution_log[self._node.uid]['dynamically_resolved_cache'] = True
        else:
            with self._fuse_manager.manage(self._node, self._patterns):
                self._stderr, self._exit_code, timing = self.run()

        self._detailed_timings.start_stage(DetailedStages.FINALIZE_NODE)

        if not self._exit_code and self._ctx.content_uids:
            # Read if from '.content_hash.md5' file or calculate it
            self._node.output_digests = self._build_root.read_output_digests(write_if_absent=True)

        self._execution_log[self._node.uid]['timing'] = timing
        if self._exit_code and not have_broken:
            self._build_errors[self._node.uid] = self._stderr

        if self._exit_code:
            self._ctx.fast_fail()
        elif self._node.cacheable:
            # Account for local cache
            self._build_root.inc()
            # Account for dist cache
            self._build_root.inc()
            self._ctx.runq.add(
                self._ctx.write_through_caches(self._node, self._build_root),
                deps=[],
                inplace_execution=self._ctx.opts.eager_execution,
            )

        self._ctx.eager_result(self)

        if timing:
            self._detailed_timings.finish_stage()

        self._execution_log[self._node.uid]['detailed_timings'] = self._detailed_timings.dump()

    def __str__(self):
        return 'Run({})'.format(str(self._node))

    def advanced_timings(self):
        return self._detailed_timings.dump()

    def prio(self):
        return 0

    @property
    def max_dist(self):
        return self._node.max_dist

    def res(self):
        p = self._node.kv.get('p')
        if p in (
            'AR',
            'LD',
        ):
            return worker_threads.ResInfo(io=1)
        if p in (
            'SB',
            'XT',
            'MD',
            'NP',
        ):
            return worker_threads.ResInfo(download=1)
        if const.TestSize.is_test_shorthand(p) or p == "YT":
            cpu = self._node.requirements.get('cpu', 1)
            if cpu == 'all':
                cpu = self._test_threads
            else:
                cpu = parse_cpu_requirement(cpu)
                cpu = min(max(int(cpu), 1), self._threads)
            return worker_threads.ResInfo(test=1, cpu=cpu)
        return worker_threads.ResInfo(cpu=1)

    def status(self):
        return fmt_node(self._node, self._tags, self._status)

    def body(self):
        return self._stderr

    def short_name(self):
        return self._node.kv.get('p', '??')

    def _supports_build_time_cache(self, other=None):
        node = other or self._node
        return (
            self._ctx.build_time_cache
            and node.static_uid is not None
            and not const.TestSize.is_test_shorthand(node.kv.get('p'))
        )

    @property
    def deps(self):
        return self._node.dep_nodes()

    @property
    def build_time(self):
        if self._supports_build_time_cache():
            _, value = self._ctx.build_time_cache.last_usage(self._node.static_uid)
            return value or 0
        return 0

    @property
    def build_time_with_deps(self):
        if self._supports_build_time_cache():
            sum_time = self.build_time
            for d in self.deps:
                if self._supports_build_time_cache(other=d):
                    sum_time += self._ctx.build_time_cache.last_usage(d.static_uid)[1] or 0
            return sum_time
        return 0
