import datetime
import logging
import subprocess
import sys
import threading

import package
import exts.retry
import yalibrary.term.console as term_console

import six

logger = logging.getLogger(__name__)


def _run_process(command, args, cwd=None, env=None, add_line_timestamps=False, tee=False):
    # TODO: extend exts/process.py
    logger.debug('Running command: %s %s', command, args)
    package.display.emit_message('Running command: [[imp]]\'{}\'[[rst]] with args [[imp]]{}'.format(command, args))

    if add_line_timestamps or tee:
        stop_ev = threading.Event()

        def strip_ascii_codes(data):
            return term_console.ecma_48_sgr_regex().sub("", data)

        def stream(istream, lstore, ostream, transform):
            while True:
                line = istream.readline()
                if line:
                    if transform:
                        line = transform(line)
                    if ostream:
                        ostream.write(line)
                    lstore.append(strip_ascii_codes(line))
                elif stop_ev.is_set():
                    break

        def start_thread(*args):
            th = threading.Thread(target=stream, args=args)
            th.daemon = True
            th.start()
            return th

        def add_timestamp(line):
            escape_codes_prefix = ''
            while True:
                m = term_console.ecma_48_sgr_regex().match(line)
                if m:
                    escape_codes_prefix += m.group(0)
                    line = line[m.end() :]
                else:
                    break

            return "{}{} {}".format(
                escape_codes_prefix,
                # ya log's time format
                datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S,%f")[:-3],
                line,
            )

        def run():
            terr, tout = None, None
            err_lines, out_lines = [], []

            proc = subprocess.Popen(
                [command] + args,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                cwd=cwd,
                env=env,
                stdin=subprocess.DEVNULL,
                **({'text': True} if six.PY3 else {})
            )  # stdin=subprocess.DEVNULL, see explanation bellow
            try:
                transform = add_timestamp if add_line_timestamps else None
                terr = start_thread(
                    proc.stderr,
                    err_lines,
                    sys.stderr if tee else None,
                    transform,
                )
                tout = start_thread(
                    proc.stdout,
                    out_lines,
                    sys.stderr if tee else None,
                    transform,
                )
                proc.wait()
            finally:
                proc.terminate()
                proc.wait()
                stop_ev.set()
                if terr:
                    terr.join()
                if tout:
                    tout.join()
            return proc, "".join(out_lines), "".join(err_lines)

    else:

        def run():
            # some tools may expect some interaction from the user, for example some versions of "dch" may say:
            # ```
            # dch warning: building email address from username and FQDN
            # dch: Did you see those 2 warnings?  Press RETURN to continue...
            # ```
            # and subprocess waits for input indefinitely
            # stdin=subprocess.DEVNULL fix this problem
            proc = subprocess.Popen(
                [command] + args,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                cwd=cwd,
                env=env,
                stdin=subprocess.DEVNULL,
                **({'text': True} if six.PY3 else {})
            )
            out, err = proc.communicate()
            return proc, out, err

    process, out, err = run()
    rc = process.returncode
    if rc:
        raise package.packager.YaPackageException(
            'Command {} failed\nstdout:\n{}\nstderr:\n{}\nexit code:{}\n'.format(command, out, err, rc)
        )
    logger.debug("rc = %s,\nstdout:\n%s\nstderr:\n%s", rc, out, err)
    return out, err


def run_process(command, args, cwd=None, max_retry_times=1, env=None, add_line_timestamps=False, tee=False):
    @exts.retry.retrying(max_times=max_retry_times)
    def _run():
        return _run_process(command, args, cwd, env, add_line_timestamps, tee)

    return _run()
