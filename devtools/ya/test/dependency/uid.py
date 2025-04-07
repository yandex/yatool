import os
import base64
import logging

import exts.func
import exts.tmp
import exts.uniq_id
from . import testdeps
from devtools.ya.core.imprint import imprint
import devtools.ya.test.const as const

logger = logging.getLogger(__name__)

ARCADIA_TESTS_DATA_REPOS = {}
ATD_BASE_REV_PREFIX = '# Trunk revision:'


class NoATDInfoException(Exception):
    pass


class TestUidGenerator(object):
    """
    Gets uid for test
    """

    Arcadia_Branch_Url = None
    Arcadia_Revision = None

    Path_To_Rev = None
    ATD_Base_Rev = None

    @classmethod
    def get(cls, test, graph, arc_root, opts):
        """
        Get test uid
        :param test: test to get the uid for
        :param graph: the build graph to take the test dependencies uids from
        :return: uid
        """
        deps = list(test.get_build_dep_uids())
        try:
            test_paths_hashes = test.test_paths_hashes

        except AttributeError:
            paths = testdeps.get_test_related_paths(test, arc_root, opts)
            paths.extend(
                [
                    os.path.join(arc_root, "ya"),
                ]
            )
            paths = testdeps.remove_redundant_paths(paths)
            test_paths_hashes = imprint.generate_path_imprint(paths)

        affecting_tags = sorted(t for t in test.tags if t.startswith(("ya:", "sb:")))

        imprint_parts = (
            [
                test.name,
                test.project_path,
                test_paths_hashes,
                test.timeout,
                test.get_fork_mode(),
                test.get_split_factor(opts),
            ]
            + deps
            + sorted(test._original_requirements.items())
            + affecting_tags
        )

        imprint_parts.append("{}={}".format("sbr_uid_ext", test.get_sandbox_uid_extension()))

        prepare_cmds, prepare_inputs = test.get_prepare_test_cmds()
        imprint_parts.extend(x for cmd in prepare_cmds for x in cmd['cmd_args'])
        imprint_parts.extend(cmd.get('cwd', '') for cmd in prepare_cmds)

        sandbox_resources = testdeps.get_test_sandbox_resources(test)
        imprint_parts += ["sandbox-resource-{}-ro".format(resource.get_id()) for resource in sandbox_resources]

        imprint_parts += [
            "sandbox-resource-ext-{}-ro".format(resource)
            for resource in testdeps.get_test_ext_sbr_resources(test, arc_root)
        ]

        if test.recipes:
            imprint_parts.append(test.recipes)

        return imprint.combine_imprints(*imprint_parts)


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
