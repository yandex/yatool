# coding: utf-8

"""
Print out test list
"""
import os
import json
import logging
import optparse

import app_config

from devtools.ya.yalibrary.yandex.sandbox.misc import fix_logging

if app_config.in_house:
    from devtools.ya.test.dependency import sandbox_storage

from devtools.ya.test.dependency import mds_storage

import exts.archive

import devtools.ya.test.canon.data
import devtools.ya.test.const
import devtools.ya.test.filter
import devtools.ya.test.reports
import devtools.ya.test.result
import devtools.ya.test.test_types.common
import devtools.ya.test.util.shared

logger = logging.getLogger(__name__)

SANDBOX_DEFAULT_GROUP = "CANONICAL_TEST"


def get_options():
    parser = optparse.OptionParser()
    parser.disable_interspersed_args()
    parser.add_option("--source-root", dest="source_root", help="source route", action='store')
    parser.add_option(
        "--custom-canondata-path",
        dest="custom_canondata_path",
        help="Custom path to store canondata. Need for tests",
        action='store',
        default=None,
    )
    parser.add_option("--dir-outputs", dest="dir_outputs", action='store_true', default=False)
    parser.add_option("--build-root", dest="build_root", help="build route", action='store')
    parser.add_option(
        "--backend",
        dest="backend",
        help="backend_key replacer. Need for support multiple storages for canonization",
        action='store',
    )

    # Sandbox options
    parser.add_option("--sandbox", dest="sandbox_url", help="uploaded resource owner", action='store')
    parser.add_option(
        "--owner", dest="owner", help="uploaded resource owner", action='store', default=SANDBOX_DEFAULT_GROUP
    )
    parser.add_option("--key", dest="key", help="uploaded resource user key to authorize", action='append', default=[])
    parser.add_option("--user", dest="user", help="uploaded resource user name to authorize", action='store')
    parser.add_option(
        "--custom-fetcher", dest="custom_fetcher", help="custom fetcher for sandbox storage", action='store'
    )
    parser.add_option("--token", dest="token", help="uploaded resource owner token", action='store')
    parser.add_option("--token-path", dest="token_path", help="path to uploaded resource owner token", action='store')
    parser.add_option("--transport", dest="transport", help="uploading transport", action='store', default=None)

    # MDS options
    parser.add_option("--mds", dest="mds", help="upload resources to MDS", action='store_true')

    parser.add_option(
        "--resource-ttl", dest="ttl", type=int, help="uploaded from test result resource TTL (days)", action='store'
    )
    parser.add_option(
        "--max-file-size",
        dest="max_file_size",
        type=int,
        help="max file size to store locally (0 - no limit)",
        action='store',
    )
    parser.add_option("--output", dest="output", help="Test suite output", action='store')
    parser.add_option("--input", dest="inputs", help="Test chunks output", action='append')
    parser.add_option("--log-path", dest="log_path", help="log file path", action='store')
    parser.add_option("--result-path", help="file with canonization result", action='store')
    parser.add_option(
        "--save-old-canondata",
        help="don't delete any canondata. Even if we have no info about test",
        action='store_true',
        default=False,
    )
    parser.add_option(
        "--log-level",
        dest="log_level",
        help="logging level",
        action='store',
        default="INFO",
        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
    )
    parser.add_option("--sub-path", help="Subpath for results", action='store')
    parser.add_option(
        "--no-src-changes", dest="no_src_changes", help="Don't apply changes to repo", action='store_true'
    )
    return parser.parse_args()


def get_task_state_printer():
    progress_logger = logging.getLogger("yalibrary.yandex.sandbox.progress")

    def show(id, status, t):
        progress_logger.debug("[{} s] task #{}: {}".format(t, id, status))

    def printer(time_, task):
        status = task['status']
        show(task['id'], status, round(time_, 2))

    return printer


def main():
    options, _ = get_options()
    devtools.ya.test.util.shared.setup_logging(
        options.log_level, options.log_path, ["yalibrary.upload.uploader.progress", "yalibrary.yandex.sandbox.progress"]
    )
    fix_logging.fix_logging()

    oauth_token = devtools.ya.test.util.shared.get_oauth_token(options)

    resources_root = options.build_root
    if app_config.in_house:
        storage = sandbox_storage.SandboxStorage(
            resources_root, custom_fetcher=options.custom_fetcher, oauth_token=oauth_token
        )
        mdsstorage = mds_storage.MdsStorage(resources_root, use_cached_only=False)
    else:
        # Neither Sandbox nor MDS won't be available in Opensource, but we need to use MdsStorage to get resource info.
        # This will work as storages are used only in ResultsComparer only when expected resource is sandbox or http
        storage = None
        mdsstorage = mds_storage.MdsStorage(resources_root, use_cached_only=True)

    canonical_data = devtools.ya.test.canon.data.CanonicalData(
        arc_path=options.custom_canondata_path or options.source_root,
        sandbox_url=options.sandbox_url,
        sandbox_token=oauth_token,
        sandbox_storage=storage,
        resource_owner=options.owner,
        ssh_keys=options.key,
        ssh_user=options.user,
        upload_transport=options.transport,
        resource_ttl=options.ttl,
        max_file_size=options.max_file_size,
        skynet_upload_task_state_printer=get_task_state_printer(),
        mds=options.mds,
        sub_path=options.sub_path,
        mds_storage=mdsstorage,
        oauth_token=oauth_token,
        no_src_changes=options.no_src_changes,
        backend=options.backend,
    )

    ok = True
    result = {}
    replacements = [
        ("$(BUILD_ROOT)", options.build_root),
        ("$(SOURCE_ROOT)", options.source_root),
    ]
    resolver = devtools.ya.test.reports.TextTransformer(replacements)

    suite = devtools.ya.test.result.load_suite_from_output(options.output, resolver)
    if options.save_old_canondata:
        suite.save_old_canondata = True
    if suite.tests:
        result["tests_count"] = len(suite.tests)
        if not options.dir_outputs:
            for input_dir in options.inputs:
                # extract testing_output_stuff to get canonical output
                exts.archive.extract_from_tar(
                    os.path.join(input_dir, devtools.ya.test.const.TESTING_OUT_TAR_NAME),
                    os.path.join(input_dir, devtools.ya.test.const.TESTING_OUT_DIR_NAME),
                )

    if canonical_data.canonize(suite):
        canonical_data_path = os.path.join(
            options.source_root, suite.project_path, devtools.ya.test.const.CANON_DATA_DIR_NAME
        )
        if canonical_data.repo().name:
            helper_command = "run '{} st {}'".format(canonical_data.repo().name, canonical_data_path)
        else:
            helper_command = "check {}".format(canonical_data_path)
        logger.info("%s canonized successfully, %s to see changed files", suite, helper_command)
    else:
        ok = False

    result["status"] = ok
    with open(options.result_path, "w") as res_file:
        json.dump(result, res_file)


if __name__ == '__main__':
    main()
