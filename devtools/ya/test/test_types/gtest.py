from test.test_types import library_ut


GTEST_TYPE = "gtest"


class GUnitTestSuite(library_ut.UnitTestSuite):
    """
    GTEST fully supports UNITTESTS's interface.
    """

    @classmethod
    def get_type_name(cls):
        return GTEST_TYPE

    def get_type(self):
        return GTEST_TYPE

    @property
    def name(self):
        return GTEST_TYPE
