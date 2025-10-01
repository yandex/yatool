import base64
import six
import shlex

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from devtools.ya.test.test_types.common import AbstractTestSuite  # noqa

MERGE_FIELDS = ["TEST-FILES"]
HASH_FIELDS = ["SCRIPT-REL-PATH", "SOURCE-FOLDER-PATH", "TEST-NAME"]


def parse_dart(dart_content):
    def to_stream(data):
        dart_entry = {}
        for line in data:
            if line.startswith('==='):
                if dart_entry:
                    yield dart_entry
                    dart_entry = {}
            elif line:
                key, value = line.split(': ', 1)
                key = key.strip()
                value = value.strip()
                if value.startswith('"') and value.endswith('"'):
                    value = value.strip('"').split(';')
                dart_entry[key] = value

    result = list(to_stream(dart_content))
    return result


def get_dart_id(dart_info):
    return tuple(dart_info[v] for v in HASH_FIELDS)


def get_suite_id(suite: "AbstractTestSuite") -> tuple[str, ...]:
    return (suite.name, type(suite).__name__, suite.project_path, suite.salt)


def merge_darts(darts):
    darts_map = {}
    for dart_info in darts:
        dart_uid = get_dart_id(dart_info)
        if dart_uid not in darts_map:
            darts_map[dart_uid] = dart_info
        else:
            for field in MERGE_FIELDS:
                if field in darts_map[dart_uid] and field in dart_info:
                    darts_map[dart_uid][field].extend(dart_info[field])
                else:
                    raise AssertionError(
                        "field {} not found in dartinfo. old dart: {} new dart: {}".format(
                            field, darts_map[dart_uid], dart_info
                        )
                    )
    for dart_uid, dart_info in six.iteritems(darts_map):
        for field in MERGE_FIELDS:
            if field in dart_info:
                dart_info[field] = sorted(list(set(dart_info[field])))

    return [darts_map[key] for key in sorted(darts_map.keys())]


def decode_recipe_cmdline(raw):
    recipes_string = six.ensure_str(base64.b64decode(raw))
    recipes_cmds = []
    for line in recipes_string.splitlines():
        line = line.strip()
        if line:
            args = shlex.split(line)
            recipes_cmds.append(args)
    return recipes_cmds
