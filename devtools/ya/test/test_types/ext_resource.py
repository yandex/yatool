from devtools.ya.test.test_types import py_test
import devtools.ya.test.util.shared as util_shared


VALIDATE_RESOURCE_TEST_TYPE = "validate_resource"
VALIDATE_DATA_SBR_TEST_TYPE = "validate_data_sbr"


class ExtResourceAbstractSuite(py_test.PyLintTestSuite):
    @classmethod
    def get_ci_type_name(cls):
        return "test"

    def get_type(self):
        return VALIDATE_RESOURCE_TEST_TYPE

    @property
    def name(self):
        return VALIDATE_RESOURCE_TEST_TYPE

    @classmethod
    def is_batch(cls):
        return True

    @property
    def default_requirements(self):
        req = super(ExtResourceAbstractSuite, self).default_requirements
        req['network'] = 'full'
        return req

    def get_computed_test_names(self, opts):
        return ["{}::{}".format(self.test_project_filename, self.get_type())]

    def batch_name(self):
        return self.test_project_filename

    def get_test_dependencies(self):
        return []

    def get_checker(self, opts, dist_build, out_path):
        raise NotImplementedError()


class CheckResourceTestSuite(ExtResourceAbstractSuite):
    def get_sandbox_uid_extension(self):
        return self.meta.sbr_uid_ext

    def get_checker(self, opts, dist_build, out_path):
        cmd = ["check_resource"]

        if dist_build:
            cmd.append('--verbose')

        cmd += util_shared.get_oauth_token_options(opts)

        return ' '.join(cmd)

    def setup_environment(self, env, opts):
        env.clear()
        env.clean_mandatory()


class CheckDataSbrTestSuite(CheckResourceTestSuite):
    def get_type(self):
        return VALIDATE_DATA_SBR_TEST_TYPE

    @property
    def name(self):
        return VALIDATE_DATA_SBR_TEST_TYPE
