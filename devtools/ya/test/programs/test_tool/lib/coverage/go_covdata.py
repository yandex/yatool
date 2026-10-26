"""Helpers around 'go tool covdata': binary coverage data packed into go.coverage.tar."""

import logging
import os
import re
import subprocess
import typing
from collections.abc import Callable, Iterable, Iterator

import exts.fs
import exts.process

logger = logging.getLogger(__name__)

# <module path>/<file>:<start_line>.<start_col>,<end_line>.<end_col> <num_statements> <count>
PROFILE_LINE_RE = re.compile(r"^[^/]+/(?P<file>[^:]+):(\d+)\.(\d+),(\d+)\.(\d+)\s+(\d+)\s+(\d+)$")
MODE_LINE = "mode: set"


class Block(typing.NamedTuple):
    filename: str  # module-relative path
    segment: list[int]  # unified segment [start_line, start_col, end_line, end_col, covered], 0-based lines
    num_statements: int


def collect_coverage_data(roots: Iterable[str], dest: str) -> str | None:
    """Gather covmeta/covcounters files from unpacked go.coverage.tar roots into one flat directory.

    One directory keeps covdata's '-i' argument short for any number of suites and runs. Equal covmeta files
    of a binary are kept once; a counters file clashing by name gets its timestamp component bumped.

    Returns:
        dest, or None if no coverage data was found.
    """
    exts.fs.create_dirs(dest)
    names: set[str] = set()
    for root in roots:
        for path, _, filenames in os.walk(root):
            for name in filenames:
                if not name.startswith(("covmeta.", "covcounters.")):
                    continue
                if name.startswith("covmeta.") and name in names:
                    continue
                target = name
                while target in names:
                    base, timestamp = target.rsplit(".", 1)
                    target = f"{base}.{int(timestamp) + 1}"
                names.add(target)
                exts.fs.hardlink_or_copy(os.path.join(path, name), os.path.join(dest, target))
    return dest if names else None


def resolve_go_tool(gotools_path: str, binname: str) -> str:
    path = os.path.join(gotools_path, "pkg/tool")
    names = os.listdir(path)
    # go toolchain contains only one platform
    assert len(names) == 1, names
    return os.path.join(path, names[0], binname)


def run_go_tool(gotools_path: str, tool: str, *args: str) -> str:
    """Run a tool from the toolchain's pkg/tool directory and return its stdout."""
    cmd = [resolve_go_tool(gotools_path, tool), *args]
    logger.debug("run %s", cmd)
    # utf-8 rather than exts.process.run_process: Go tool output may carry non-ASCII paths and identifiers
    proc = exts.process.popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, encoding="utf-8", errors="replace")
    stdout, stderr = proc.communicate()
    if proc.returncode:
        logger.error("%s failed with code %d:\n%s", cmd, proc.returncode, stderr)
        raise subprocess.CalledProcessError(proc.returncode, cmd, stdout, stderr)
    return stdout


def write_text_profile(gotools_path: str, coverage_dir: str | None, output: str) -> None:
    """Convert binary coverage data to the legacy text profile; counters of all runs are merged by covdata."""
    if coverage_dir:
        run_go_tool(gotools_path, "covdata", "textfmt", "-i=" + coverage_dir, "-o=" + output)
    else:
        with open(output, "w") as afile:
            afile.write(MODE_LINE + "\n")


def filter_profile(lines: Iterable[str], keep: Callable[[str], bool]) -> Iterator[str]:
    """Yield the mode line and blocks of accepted files with the module path stripped, e.g. for 'cover -html'."""
    yield MODE_LINE
    for line in lines:
        match = PROFILE_LINE_RE.match(line.strip())
        if match and keep(match["file"]):
            yield line[match.start("file") :].rstrip("\n")


def parse_profile(lines: Iterable[str], keep: Callable[[str], bool]) -> Iterator[Block]:
    """Yield blocks of accepted files from text profile lines (mode line included)."""
    for line in lines:
        match = PROFILE_LINE_RE.match(line.strip())
        if not match or not keep(match["file"]):
            continue
        sline, scol, eline, ecol, num_statements, count = (int(group) for group in match.groups()[1:])
        yield Block(match["file"], [sline - 1, scol, eline - 1, ecol, int(bool(count))], num_statements)
