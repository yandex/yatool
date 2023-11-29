import os
import base64
import logging
import socket

import exts.func
import exts.tmp
import exts.uniq_id
from . import testdeps
from core.imprint import imprint
import test.const as const

logger = logging.getLogger(__name__)

ARCADIA_TESTS_DATA_REPOS = {}
ATD_BASE_REV_PREFIX = '# Trunk revision:'


class NoATDInfoException(Exception):
    pass


def get_arcadia_tests_data_repo(config):
    if 'repos' not in config:
        return None

    global ARCADIA_TESTS_DATA_REPOS
    key = id(config)
    if key not in ARCADIA_TESTS_DATA_REPOS:
        data = [repo for repo in config['repos'] if repo["pattern"] == "$(TESTS_DATA_ROOT)"]
        if data:
            ARCADIA_TESTS_DATA_REPOS[key] = data[0]
        else:
            ARCADIA_TESTS_DATA_REPOS[key] = None
            logger.warn("Cannot get info about path and revision in %s", config)
    return ARCADIA_TESTS_DATA_REPOS[key]


class TestUidGenerator(object):
    """
    Gets uid for test
    """

    Arcadia_Branch_Url = None
    Arcadia_Revision = None

    Path_To_Rev = None
    ATD_Base_Rev = None

    @classmethod
    def _get_atd_revisions(cls, arc_root, use_atd_revs, base_rev_only=False):
        if cls.ATD_Base_Rev is not None and cls.Path_To_Rev is not None:
            return cls.ATD_Base_Rev, cls.Path_To_Rev

        if not base_rev_only:
            cls.Path_To_Rev = {}
        if not use_atd_revs:
            logger.debug("Using SVN access as arcadia_tests_data imprints source")
            return None, cls.Path_To_Rev

        try:
            rev_file = os.path.join(arc_root, "build", "yandex_specific", "atd", "revisions.txt")
            with open(rev_file, "r") as f:
                logger.debug("Using %s as arcadia_tests_data imprints source", rev_file)
                for line in f.readlines():
                    if not line.startswith("#"):
                        if not cls.ATD_Base_Rev:
                            raise Exception("No revision in header")
                        if base_rev_only:
                            break

                        key, rev = line.strip().split(" ")[:2]
                        cls.Path_To_Rev[key] = rev
                    elif line.startswith(ATD_BASE_REV_PREFIX):
                        cls.ATD_Base_Rev = line[len(ATD_BASE_REV_PREFIX):].strip(" \n")
                        logger.debug("Will use r{} for arcadia_tests_data".format(cls.ATD_Base_Rev))
            if not base_rev_only and not cls.Path_To_Rev:
                raise NoATDInfoException("PathToRev is empty")
        except Exception:
            logger.warn("Cannot use saved info for arcadia_tests_data will use svn access")

        if not base_rev_only:
            logger.debug("Loaded {} atd revisions".format(len(cls.Path_To_Rev)))
        return cls.ATD_Base_Rev, cls.Path_To_Rev

    @classmethod
    def get(cls, test, graph, arc_root, use_atd_revs, opts):
        """
        Get test uid
        :param test: test to get the uid for
        :param graph: the build graph to take the test dependencies uids from
        :return: uid
        """

        # XXX extra debug for a specific project, for more info see https://st.yandex-team.ru/DEVTOOLS-7588
        logging_needed = test.project_path.startswith("web/app_host/conf/graph_generator/tests/light") and test._modulo_index == 0

        def debug(*args):
            if logging_needed:
                logger.debug(*args)

        deps = list(test.get_build_dep_uids())
        paths = []
        try:
            test_paths_hashes = test.test_paths_hashes

        except AttributeError:
            paths = testdeps.get_test_related_paths(test, arc_root, opts)
            paths.extend([
                os.path.join(arc_root, "ya"),
            ])
            paths = testdeps.remove_redundant_paths(paths)
            test_paths_hashes = imprint.generate_path_imprint(paths, log=logging_needed)
            if logging_needed:
                logger.info("!!!! Create clean Imprint")
                test_paths_hashes_clean = None
                try:
                    from core.imprint import Imprint
                    imprint_clean = Imprint()
                    test_paths_hashes_clean = imprint_clean.generate_path_imprint(paths, log=logging_needed)
                except Exception as e:
                    logger.warning("!! Error while calculate clean imprint: %s", e)
                logger.debug("!! Default test_path_hashes: %s", test_paths_hashes)
                logger.debug("!! Clean   test_path_hashes: %s", test_paths_hashes_clean)

        affecting_tags = sorted([t for t in test.tags if t.startswith(("ya:", "sb:"))])

        imprint_parts = [
            test.name,
            test.project_path,
            test_paths_hashes,
            test.timeout,
            test.get_fork_mode(),
            test.get_split_factor(opts),
        ] + deps + sorted(test._original_requirements.items()) + affecting_tags

        imprint_parts.append("{}={}".format("sbr_uid_ext", test.get_sandbox_uid_extension()))

        prepare_cmds, prepare_inputs = test.get_prepare_test_cmds()
        imprint_parts.extend(x for cmd in prepare_cmds for x in cmd['cmd_args'])
        imprint_parts.extend(cmd.get('cwd', '') for cmd in prepare_cmds)

        sandbox_resources = testdeps.get_test_sandbox_resources(test)
        imprint_parts += ["sandbox-resource-{}-ro".format(resource.get_id()) for resource in sandbox_resources]

        imprint_parts += ["sandbox-resource-ext-{}-ro".format(resource) for resource in testdeps.get_test_ext_sbr_resources(test, arc_root)]

        tests_paths = testdeps.get_test_data_paths(test, data_root="", abs_path=False)
        if tests_paths:
            from core.imprint.atd import ArcadiaTestData
            import yalibrary.svn as svn

            _, path_to_rev = cls._get_atd_revisions(arc_root, use_atd_revs)
            if path_to_rev:
                tests_paths = testdeps.get_test_data_paths(test, data_root="", abs_path=False)
                for path in tests_paths:
                    imprint_parts.append(ArcadiaTestData.generate_saved_imprint(path, path_to_rev))
            else:
                logger.warn("Saved info for arcadia_tests_data is unavailable, will use svn access")
                arcadia_tests_data_repo = get_arcadia_tests_data_repo(graph.get_conf())
                if arcadia_tests_data_repo:
                    revision = arcadia_tests_data_repo["revision"]
                    user = "robot-canons-dloader"
                    user_key_file = get_robot_canons_dloader_key_path(arc_root)
                    branch_url = cls._cut_off_arcadia_from_url(arcadia_tests_data_repo["repository"]).replace("svn://", svn.ARCADIA_SVN_HOST.rstrip("/") + "/")
                    svn_tests_paths = testdeps.get_test_data_svn_paths(test, branch_url)

                    for path in svn_tests_paths:
                        imprint_parts.append(ArcadiaTestData.generate_svn_imprint(path, revision, user, user_key_file))

        if test.recipes:
            imprint_parts.append(test.recipes)

        debug("%s paths: %s, hashes: %s, imprints: %s (%s)", test, paths, test_paths_hashes, imprint_parts, socket.gethostname())
        return imprint.combine_imprints(*imprint_parts)

    @classmethod
    def _cut_off_arcadia_from_url(cls, arcadia_svn_url):
        if arcadia_svn_url.endswith("/"):
            arcadia_svn_url = arcadia_svn_url[:-1]
        return arcadia_svn_url.rsplit("/", 1)[0]


@exts.func.memoize()
def get_robot_canons_dloader_key_path(arc_root):
    key = "ROBOT_CANONS_DLOADER_SSH_KEY"
    if key in os.environ:
        logging.info("Using key from env['%s']", key)
        path = exts.tmp.create_temp_file()
        with open(path, "w") as f:
            f.write(base64.b64decode(os.environ[key].strip()).strip())
        os.chmod(path, 0o644)
        return path
    logging.info('Could not find private key in env')
    return None


def get_test_result_uids(suites):
    test_uids = []
    for suite in suites:
        test_uids += suite.result_uids
    return test_uids


def get_uid(deps, prefix=None):
    # type: (list[str], None | str) -> str
    u = imprint.combine_imprints(*sorted(deps))
    if prefix:
        return prefix + const.UID_PREFIX_DELIMITER + u
    return u


def get_test_node_uid(params, prefix=None):
    u = imprint.combine_imprints(*params)
    if prefix:
        return prefix + const.UID_PREFIX_DELIMITER + u
    return u


def get_random_uid(prefix=None):
    return const.UID_PREFIX_DELIMITER.join([_f for _f in ["rnd", prefix, exts.uniq_id.gen16()] if _f])
