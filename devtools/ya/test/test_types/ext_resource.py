from test.test_types import py_test


VALIDATE_RESOURCE_TEST_TYPE = "validate_resource"
VALIDATE_DATA_SBR_TEST_TYPE = "validate_data_sbr"


class ExtResourceAbstractSuite(py_test.PyLintTestSuite):
    @classmethod
    def get_ci_type_name(cls):
        return "test"

    @classmethod
    def get_type_name(cls):
        return VALIDATE_RESOURCE_TEST_TYPE

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

    def _need_auth(self):
        return False

    def get_checker(self, opts, dist_build, out_path):
        raise NotImplementedError()


class CheckResourceTestSuite(ExtResourceAbstractSuite):
    def get_sandbox_uid_extension(self):
        return self.dart_info.get('SBR-UID-EXT')

    def get_checker(self, opts, dist_build, out_path):
        cmd = ["check_resource"]

        if dist_build:
            cmd.append('--verbose')

        return ' '.join(cmd)


class CheckDataSbrTestSuite(CheckResourceTestSuite):
    @classmethod
    def get_type_name(cls):
        return VALIDATE_DATA_SBR_TEST_TYPE

    def get_type(self):
        return VALIDATE_DATA_SBR_TEST_TYPE

    @property
    def name(self):
        return VALIDATE_DATA_SBR_TEST_TYPE


class CheckMDSTestSuite(ExtResourceAbstractSuite):
    def get_checker(self, opts, dist_build, out_path):
        return "check_mds"


class CheckExternalTestSuite(ExtResourceAbstractSuite):
    def get_checker(self, opts, dist_build, out_path):
        return "check_external --source-root $(SOURCE_ROOT) --project-path " + self.project_path

    def get_test_related_paths(self, source_root, opts):
        return ["{}/{}/{}".format(source_root, self.project_path, x) for x in self._get_files()]
