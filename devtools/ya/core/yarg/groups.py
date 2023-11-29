import logging


class Group(object):
    registry = []  # type: list[Group]

    def __init__(self, name, index, alias_name=None, desc=None):
        type(self).registry.append(self)

        self.name = name
        self.index = index
        self.alias_name = alias_name
        self.desc = desc

    @classmethod
    def search_in_registry(cls, value):
        if isinstance(value, Group):
            return value

        if value is None:
            return DEFAULT_ALIAS_GROUP

        for group in cls.registry:
            if group.name == value or group.index == value or group.alias_name == value:
                return group

        logging.warning("Unknown group: %s", value)

        return None


OPERATIONAL_CONTROL_GROUP = Group("Ya operation control", 30, 'OPERATIONAL_CONTROL_GROUP')
CHECKOUT_ONLY_GROUP = Group("Selective checkout", 40, 'CHECKOUT_ONLY_GROUP')
OUTPUT_CONTROL_GROUP = Group("Build output", 50, 'OUTPUT_CONTROL_GROUP')
PRINT_CONTROL_GROUP = Group("Printing", 60, 'PRINT_CONTROL_GROUP')
PLATFORM_CONFIGURATION_GROUP = Group("Platform/build configuration", 70, "PLATFORM_CONFIGURATION_GROUP")
CACHE_CONTROL_GROUP = Group("Local cache", 80, "CACHE_CONTROL_GROUP")
YT_CACHE_CONTROL_GROUP = Group("YT cache", 90, "YT_CACHE_CONTROL_GROUP")
YT_CACHE_PUT_CONTROL_GROUP = Group("YT cache put", 100, "YT_CACHE_PUT_CONTROL_GROUP")

AUTOCHECK_GROUP = Group('Autocheck-only', 101, "AUTOCHECK_GROUP")
BUILD_GRAPH_CACHE_GROUP = Group('Build graph cache', 102, "BUILD_GRAPH_CACHE_GROUP")
GRAPH_GENERATION_GROUP = Group('Graph generation', 103, "GRAPH_GENERATION_GROUP")
CODENAV_GROUP = Group("Codenavigation", 104, "CODENAV_GROUP")
FEATURES_GROUP = Group("Feature flags", 105, "FEATURES_GROUP")

BULLET_PROOF_OPT_GROUP = Group('Bullet-proof options', 1100, 'BULLET_PROOF_OPT_GROUP')
FILTERS_OPT_GROUP = Group('Filters', 1105, 'FILTERS_OPT_GROUP')
TESTING_OPT_GROUP = Group('Testing', 1110, 'TESTING_OPT_GROUP')
PACKAGE_OPT_GROUP = Group('Packaging', 1120, 'PACKAGE_OPT_GROUP')
ADVANCED_OPT_GROUP = Group('Advanced', 1130, 'ADVANCED_OPT_GROUP')
DEVELOPERS_OPT_GROUP = Group('For Ya developers', 1140, 'DEVELOPERS_OPT_GROUP')
JAVA_BUILD_OPT_GROUP = Group('Java-specific', 1160, 'JAVA_BUILD_OPT_GROUP')
MAVEN_OPT_GROUP = Group('Maven import', 1164, 'MAVEN_OPT_GROUP')
COMMON_UPLOAD_OPT_GROUP = Group("Upload", 1168, 'COMMON_UPLOAD_OPT_GROUP')
SANDBOX_UPLOAD_OPT_GROUP = Group('Upload to sandbox', 1170, 'SANDBOX_UPLOAD_OPT_GROUP')
MDS_UPLOAD_OPT_GROUP = Group('Upload to mds', 1180, 'MDS_UPLOAD_OPT_GROUP')
AUTH_OPT_GROUP = Group('Authorization', 1190, 'AUTH_OPT_GROUP')

DISTBUILD_OPT_GROUP = Group('Distbuild', 1200, 'DISTBUILD_OPT_GROUP')

DEFAULT_ALIAS_GROUP = Group("Aliases", 2000, 'DEFAULT_ALIAS_GROUP')

UNCATEGORIZED_GROUP = Group("Uncategorized", 10001, "UNCATEGORIZED_GROUP")
