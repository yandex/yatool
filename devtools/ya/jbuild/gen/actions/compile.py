import os
import logging

import jbuild.commands as commands
from . import parse
import jbuild.gen.makelist_parser2 as mp
import jbuild.gen.consts as consts

logger = logging.getLogger(__name__)


def make_build_file(lst, sep, path, max_cmd_len=8000, max_path_prefix=100):
    cmds = [commands.mkdir(os.path.dirname(path)), commands.append(path, '')]

    content, content_length = [], 0
    for cmd in lst:
        cmd_real_length = len(cmd) + max_path_prefix
        if cmd_real_length > max_cmd_len:
            logger.error("Can't create build file. String %s is too long", cmd)
            raise Exception("%s is too long" % cmd)
        if content_length + cmd_real_length > max_cmd_len:
            content = sep.join(content)
            content += sep
            cmds.append(commands.append(path, content))
            content, content_length = [], 0
        content.append(cmd)
        content_length += cmd_real_length + len(sep)
    if content:
        cmds.append(commands.append(path, sep.join(content)))
    return cmds


def prepare_path_to_manifest(path):
    return path[13:].lstrip('/').lstrip('\\') if path.startswith('$(BUILD_ROOT)') else path


def iter_processors(plain):
    for ws in plain.get(consts.ANNOTATION_PROCESSOR, []):
        for w in ws:
            yield w


def parse_words(words):
    is_resource, ws = parse.extract_word(words, consts.J_RESOURCE)
    srcdir, ws = parse.extract_word(words, consts.J_SRCDIR)
    pp, ws = parse.extract_word(ws, consts.J_PACKAGE_PREFIX)
    e, ws = parse.extract_word(ws, consts.J_EXTERNAL)
    kv = parse.extract_words(
        ws,
        {
            consts.J_EXCLUDE,
        },
    )

    return is_resource, srcdir, pp, e, kv[None], kv[consts.J_EXCLUDE]


def get_ya_make_flags(plain, flags_name):
    res = {}
    key = None
    if flags_name in plain:
        raw = sum(plain.get(flags_name, []), [])
        for item in raw:
            if not item.startswith('-') and key is None:
                raise mp.ParseError(
                    'Error parsing {}: {} not match (-key (value)?) pattern'.format(flags_name, ' '.join(raw))
                )
            if item.startswith('-'):
                key = item[1:]
                res[key] = None
            else:
                res[key] = item
                key = None
    return res
