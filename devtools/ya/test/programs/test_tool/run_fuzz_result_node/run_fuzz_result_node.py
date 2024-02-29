# coding: utf-8

import os
import json
import logging
import argparse

import exts.fs
import yalibrary.vcs as vcs

from test.util import shared, tools
from devtools.ya.yalibrary.vcs import arc

try:
    import yalibrary.svn as svn

    svn_available = True
except ImportError:
    svn_available = False


logger = logging.getLogger(__name__)


SVN_CHOOSE_POLICY = svn.SvnChoosePolicy.prefer_local_svn_but_not_16() if svn_available else None


def get_options():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root")
    parser.add_argument("--build-root")
    parser.add_argument(
        "--corpus-description", dest="corpus_descriptions", help="Files to upload", action="append", default=[]
    )
    parser.add_argument("--output", help="Output filename")
    parser.add_argument("--log-path")
    parser.add_argument("--write-results-inplace", action='store_true')
    parser.add_argument(
        "--log-level",
        dest="log_level",
        help="logging level",
        action='store',
        default="INFO",
        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
    )
    return parser.parse_args()


def _vcs_add(source_root, target):
    vcs_type = vcs.detect_vcs_type(cwd=source_root)
    if vcs_type == "svn" and svn_available:
        if not svn.is_under_control(target, svn_choose_policy=SVN_CHOOSE_POLICY):
            logger.debug("%s is not under svn - going to add", target)
            svn.svn_add(target, parents=True, svn_choose_policy=SVN_CHOOSE_POLICY)
            logger.info("Corpus was successfully updated, run 'svn diff %s' to see changes", target)
    elif vcs_type == "arc":
        arc_vcs = arc.Arc(source_root)
        arc_vcs.add(paths=[target])
        logger.info("Corpus was successfully updated, run 'arc diff HEAD %s' to see changes", target)
    else:
        logger.error("%s is not available, corpus was not added to vcs stage", vcs_type)


def main():
    params = get_options()

    if params.log_path:
        shared.setup_logging(params.log_level, params.log_path)

    updated_projects = []
    skipped_projects = []
    for filename in params.corpus_descriptions:
        with open(filename) as afile:
            data = json.load(afile)
        logger.debug("Description: %s", json.dumps(data, indent=4, sort_keys=True))

        project_path = data["project_path"]
        resource_id = data.get("resource_id")
        if not resource_id:
            logger.debug("Mined data wasn't uploaded: %s", project_path)
            skipped_projects.append(project_path)
            continue

        filename = tools.get_corpus_data_path(project_path, params.source_root)
        if os.path.islink(filename):
            os.unlink(filename)
        if os.path.exists(filename) and not data.get("minimized", False):
            with open(filename) as afile:
                corpus_data = json.load(afile)
            if "corpus_parts" not in corpus_data:
                corpus_data["corpus_parts"] = []
        else:
            corpus_data = {"corpus_parts": []}

        if resource_id not in corpus_data["corpus_parts"]:
            corpus_data["corpus_parts"].append(resource_id)

        updated_projects.append(
            {
                "project_path": project_path,
                "resource_id": resource_id,
                "corpus_data": corpus_data,
            }
        )

    # create node's output files for skipped files
    for project_path in skipped_projects:
        filename = tools.get_corpus_data_path(project_path, params.build_root)
        exts.fs.ensure_dir(os.path.dirname(filename))
        if not os.path.exists(filename):
            with open(filename, "w") as afile:
                afile.write("{}")

    for entry in updated_projects:
        target = tools.get_corpus_data_path(entry["project_path"])

        if params.write_results_inplace:
            abs_target = os.path.join(params.source_root, target)

            exts.fs.ensure_dir(os.path.dirname(abs_target))
            with open(abs_target, "w") as afile:
                json.dump(entry["corpus_data"], afile, indent=4, sort_keys=True)

            _vcs_add(params.source_root, abs_target)

        # always dump output corpus to the build_root in the name of testability
        filename = os.path.join(params.build_root, target)
        exts.fs.ensure_dir(os.path.dirname(filename))
        with open(filename, "w") as afile:
            json.dump(entry["corpus_data"], afile, indent=4, sort_keys=True)

    with open(params.output, "w") as afile:
        json.dump(updated_projects, afile)


if __name__ == '__main__':
    main()
