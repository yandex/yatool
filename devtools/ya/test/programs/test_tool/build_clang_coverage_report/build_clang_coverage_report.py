# coding: utf-8

import argparse
import logging
import os
import subprocess
import json

import six

import devtools.ya.core.resource
import exts.fs
import exts.archive
import devtools.ya.test.programs.test_tool.lib.coverage as lib_coverage
from devtools.common import libmagic
from library.python.testing import coverage_utils as coverage_utils_library
from devtools.ya.test.util import shared

logger = logging.getLogger(__name__)


def get_style():
    return devtools.ya.core.resource.try_get_resource("res/style.html")


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--source-root", required=True)
    parser.add_argument("--llvm-profdata-tool", required=True)
    parser.add_argument("--llvm-cov-tool", required=True)
    parser.add_argument("--coverage-path", dest="coverage_paths", action="append", default=[])
    parser.add_argument("--target-binary", dest="target_binaries", action="append", default=[])
    parser.add_argument("--log-path")
    parser.add_argument(
        "--log-level",
        dest="log_level",
        help="logging level",
        action='store',
        default="INFO",
        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
    )
    parser.add_argument("--prefix-filter")
    parser.add_argument("--exclude-regexp")
    parser.add_argument("--include-generated", action="store_true")
    parser.add_argument("--test-mode", action="store_true")

    parser.add_argument("--mcdc-coverage", action="store_true")

    parser.add_argument("--branch-coverage", action="store_true")
    parser.add_argument("--branch-coverage-type", default="count", choices=("percent", "count"))

    args = parser.parse_args()
    return args


def get_file_stats(
    llvm_cov_bin,
    indexed_profile,
    binaries,
    source_root,
    prefix_filter=None,
    exclude_regexp=None,
    mcdc=False,
    branches=False,
    include_generated=False,
):
    cmd = [
        llvm_cov_bin,
        "export",
        "-format",
        "text",
        "-instr-profile",
        indexed_profile,
        "-summary-only",
    ] + lib_coverage.util.get_default_llvm_export_args(include_generated)

    for binary in binaries:
        cmd += ["-object", binary]

    if mcdc:
        cmd += ["--show-mcdc-summary"]

    if branches:
        cmd += ["--show-branch-summary"]

    cov_files = {}
    cov_total = {
        "project": {
            "branches": {"covered": 0, "count": 0, "percent": 0},
            "functions": {"covered": 0, "count": 0, "percent": 0},
            "instantiations": {"covered": 0, "count": 0, "percent": 0},
            "lines": {"covered": 0, "count": 0, "percent": 0},
            "regions": {"covered": 0, "count": 0, "percent": 0},
            "mcdc": {"covered": 0, "count": 0, "percent": 0},
        }
    }

    file_filter = coverage_utils_library.make_filter(prefix_filter, exclude_regexp)

    def process_block(covtype, filename, data):
        if covtype != "files" or lib_coverage.util.should_skip(filename, source_root, include_generated):
            return

        relname = filename[len(source_root) :].strip("/")
        if not file_filter(relname):
            logger.debug("Filtered %s", relname)
            return

        item = json.loads(data)

        cov_files[relname] = item["summary"]
        for type_coverage in item["summary"]:
            cov_total["project"][type_coverage]["covered"] += item["summary"][type_coverage]["covered"]
            cov_total["project"][type_coverage]["count"] += item["summary"][type_coverage]["count"]

    lib_coverage.export.export_llvm_coverage(cmd, process_block)

    for type_coverage in cov_total["project"]:
        percentage = 0
        if cov_total["project"][type_coverage]["count"] != 0:
            percentage = float(cov_total["project"][type_coverage]["covered"])
            percentage /= float(cov_total["project"][type_coverage]["count"])
            percentage *= 100
        cov_total["project"][type_coverage]["percent"] = percentage

    return cov_total, cov_files


def main():
    # noinspection PyUnresolvedReferences
    import app_ctx

    args = parse_args()
    if args.log_path:
        shared.setup_logging(args.log_level, args.log_path)

    tmpdir = "tmp"
    exts.fs.create_dirs(tmpdir)
    cov_profiles = []
    for index, coverage_tar in enumerate(args.coverage_paths):
        dst = os.path.join(tmpdir, str(index))
        exts.archive.extract_from_tar(coverage_tar, dst)
        for filename in os.listdir(dst):
            cov_profiles.append(os.path.join(dst, filename))

    output_dir = os.path.abspath("coverage")
    exts.fs.create_dirs(output_dir)

    app_ctx.display.emit_status("Indexing profiles ({})".format(len(cov_profiles)))
    indexed_profile = os.path.join(output_dir, "coverage.profdata")

    # test might be skipped by filter and there will be no coverage data
    if not cov_profiles:
        logger.debug("No cov_profiles available")
        exts.archive.create_tar([], args.output)
        return

    cmd = [args.llvm_profdata_tool, "merge", "-sparse", "-o", indexed_profile] + cov_profiles
    logger.debug("Executing %s", cmd)
    subprocess.check_call(cmd)

    app_ctx.display.emit_status("Generating report")

    cov_files_map = lib_coverage.util.get_coverage_profiles_map(cov_profiles)
    logger.debug("Coverage profile files map: %s", cov_files_map)

    binaries = []
    for binary in args.target_binaries:
        if not libmagic.is_elf(six.ensure_binary(binary)):
            logger.warning("Skipping non-executable: %s", binary)
        elif not cov_files_map.get(os.path.basename(binary)):
            logger.warning("No valid coverage profiles found for %s", binary)
        else:
            binaries.append(binary)

    cov_total, cov_files = get_file_stats(
        args.llvm_cov_tool,
        indexed_profile,
        binaries,
        args.source_root,
        args.prefix_filter,
        args.exclude_regexp,
        args.mcdc_coverage,
        args.branch_coverage,
        args.include_generated,
    )

    cmd = [
        args.llvm_cov_tool,
        "show",
        "-format",
        "html",
        "-instr-profile",
        indexed_profile,
    ] + lib_coverage.util.get_default_llvm_export_args(args.include_generated)

    for binary in binaries:
        cmd += ["-object", binary]

    if args.mcdc_coverage:
        cmd += ["--show-mcdc", "--show-mcdc-summary"]

    if args.branch_coverage:
        cmd += [f"--show-branches={args.branch_coverage_type}", "--show-branch-summary"]

    file_filter = coverage_utils_library.make_filter(args.prefix_filter, args.exclude_regexp)

    files = []
    logger.debug("Executing %s", cmd)
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, errors='ignore', **({'text': True} if six.PY3 else {}))
    try:
        for filename, document in iter_documents(proc.stdout):
            if not filename.startswith(args.source_root):
                continue

            filename = filename[len(args.source_root) :].strip("/")
            if not file_filter(filename):
                continue

            files.append(filename)

            filename = os.path.join(output_dir, filename)
            exts.fs.ensure_dir(os.path.dirname(filename))
            with open(filename + ".html", "w") as afile:
                afile.write(document)
    except Exception:
        proc.kill()
        raise
    finally:
        assert proc.wait() == 0, proc.returncode

    with open(os.path.join(output_dir, "index.html"), "w") as afile:
        afile.write("<html><body>")
        afile.write("<h3>Overall coverage</h3>")
        afile.write(generate_table_report(cov_total, do_links=False))

        afile.write("<h4>Coverage breakdown</h4>")
        afile.write(generate_table_report(cov_files))
        afile.write(six.ensure_str(get_style()))
        # afile.write(STYLE_SETTINGS)
        afile.write("</body></html>")

    exts.archive.create_tar(output_dir, args.output)


def iter_documents(stream):
    buff_size = 32 * 1024
    doc_start_pattern = "<!doctype html><html>"
    last_pos = 0
    buff = ""

    while True:
        data = stream.read(buff_size)
        if not data:
            break

        buff += data

        while buff:
            start = buff.find(doc_start_pattern, last_pos)
            if start == -1:
                last_pos = len(buff)
                break
            else:
                document, buff = buff[:start], buff[start:]
                last_pos = len(doc_start_pattern)
                if not document:
                    continue

                name = get_doc_name(document)
                assert name
                yield name, document

    if buff:
        name = get_doc_name(buff)
        assert name
        yield name, buff


def get_doc_name(data):
    prefix = "<div class='source-name-title'><pre>"
    suffix = "</pre>"
    doc_name_start = data.find(prefix)
    if doc_name_start != -1:
        doc_name_end = data.find(suffix, doc_name_start + len(prefix))
        if doc_name_end != -1:
            return data[doc_name_start + len(prefix) : doc_name_end]


def generate_table_report(cov_data, do_links=True):
    res = """
        <table>
            <tr>
                <th>
                    File
                </th>
                <th>
                    Lines, %
                </th>
                <th>
                    Branches, %
                </th>
                <th>
                    Functions, %
                </th>
                <th>
                    Instantiations, %
                </th>
                <th>
                    Regions, %
                </th>
                <th>
                    MC/DC, %
                </th>
            </tr>
        """
    for filename in sorted(cov_data.keys()):
        res += '<tr bordercolor = "black">'
        if do_links:
            res += '<td><div><a href="{0}.html">{0}</div></td>'.format(filename)
        else:
            res += '<td><div>{0}</div></td>'.format(filename)

        for item in ["lines", "branches", "functions", "instantiations", "regions", "mcdc"]:
            res += generate_table_cell(cov_data, filename, item)
        res += "</tr>"
    return res + "</table>"


def generate_table_cell(cov_data, filename, type_of_item):
    if round(cov_data[filename][type_of_item]["percent"], 2) < 50:
        color = "rgb(255, 92, 73)"
    elif round(cov_data[filename][type_of_item]["percent"], 2) < 80:
        color = "rgb(214, 170, 0)"
    elif cov_data[filename][type_of_item]["percent"] != 0:
        color = "rgb(92, 184, 92)"
    else:
        color = "rgb(0, 0, 0)"
    if cov_data[filename][type_of_item]["count"] == 0:
        res = '<td align="center"><div style="color: {3}";>' '—' '</div></td>'
    else:
        res = (
            '<td align="center"><div class="tooltip" style="color: {3}";>'
            '{2}%'
            '<span class="tooltiptext">({1}/{0})</span>'
            '</div></td>'.format(
                cov_data[filename][type_of_item]["count"],
                cov_data[filename][type_of_item]["covered"],
                round(cov_data[filename][type_of_item]["percent"], 2),
                color,
            )
        )
    return res


if __name__ == "__main__":
    exit(main())
