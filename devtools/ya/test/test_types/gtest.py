from test.test_types import library_ut


class GUnitTestSuite(library_ut.UnitTestSuite):
    """
        GTEST fully supports UNITTESTS's interface.
    """
    @classmethod
    def get_type_name(cls):
        return "gtest"

    @property
    def name(self):
        return "gtest"
