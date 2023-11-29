# coding=utf-8

import re
import logging
import traceback

from yalibrary import formatter
from . import transformer
import yalibrary.term.console as term_console
from library.python import strings

logger = logging.getLogger(__name__)


TAG_NAMES = ["error", "traceback", "diff", "actual", "expected", "a"]
OPENING_BRACER = "((({})))"
CLOSING_BRACER = "(((/{})))"


def strip(comment):
    return _substitute(comment, "", "")


def replace_bracers(comment, opening_bracer, closing_bracer):
    return _substitute(comment, opening_bracer, closing_bracer)


def replace(comment, tag_map, opening_bracer="{}", closing_bracer="{}"):
    tags = {tag: None for tag in TAG_NAMES}
    tags.update(tag_map)
    for tag in tag_map.keys():
        if tag not in TAG_NAMES:
            raise AssertionError("Unknown tag '{}'".format(tag))

    return _substitute(comment, opening_bracer, closing_bracer, tags)


def get_tag_content(comment, tag):
    assert tag in TAG_NAMES
    pattern = r"{}(.*?){}".format(re.escape(OPENING_BRACER.format(tag)), re.escape(CLOSING_BRACER.format(tag)))
    match = re.search(pattern, comment, flags=re.DOTALL)
    if match:
        return match.group(1)
    return ""


def _substitute(comment, opening_bracer, closing_bracer, tag_map=None):
    if not _is_valid_comment(comment):
        return comment
    tag_map = tag_map or {tag: tag for tag in TAG_NAMES}
    tag_names = tag_map.keys()
    replacements = []

    for tag in tag_names:
        value = tag_map[tag]
        # tag is unwanted if value is None - it will be removed
        opening_re = r"\(\(\(" + tag + r"(?P<tag_attrs>\s+.*?|)\)\)\)"
        closing_re = r"\(\(\(" + "/" + tag + r"\)\)\)"
        if value is None:
            replacements.append((opening_re, ""))
            replacements.append((closing_re, ""))
        else:
            replacements.append((opening_re, opening_bracer.format(value)))
            replacements.append((closing_re, closing_bracer.format(value)))
    return transformer.TextTransformer(replacements, use_re=True).substitute(comment)


def _is_valid_comment(comment):
    '''
    Check that every opening tag got a closing pair in right order.
    This check is needed when the output of the test contains keywords (tags) used in trace files
    to prevent tracefile corruption
    '''
    # (pos, type, tag_name)
    tags = []
    for tag_type, pattern in [
        ("opening", re.escape(OPENING_BRACER).replace(r"\{\}", r"(\w+)(.*?)")),
        ("closing", re.escape(CLOSING_BRACER).replace(r"\{\}", r"(\w+)"))
    ]:
        for match in re.finditer(pattern, comment):
            # add only known tags
            if match.group(1) in TAG_NAMES:
                tags.append((match.start(), tag_type, match.group(1)))

    # sort by appearance
    tags = sorted(tags, key=lambda x: x[0])

    stack = []
    for _, tag_type, tag in tags:
        if tag_type == "opening":
            stack.append(tag)
        else:
            if not stack:
                logger.error("Got close tag '{}', while there were no opening pair".format(tag))
                return False
            last = stack.pop()
            if last != tag:
                logger.error("Got close tag '{}', while the expected is '{}'".format(tag, last))
                return False
    if stack:
        logger.error("Got tags without a closing pair: '{}'".format(",".join(stack)))
        return False
    return True


def truncate_comment(comment, limit):
    ellipsis = "\n..[snippet truncated]..\n"
    try:
        # XXX
        rich_ellipsis = "[[rst]]{}[[bad]]".format(ellipsis)
        comment = formatter.truncate_middle(comment, limit, ellipsis=rich_ellipsis)
    except Exception:
        logger.error("Error while truncating rich snippet: %s", traceback.format_exc())

    if len(comment) > limit:
        # remove tags to avoid not closed tags
        snippet = term_console.ecma_48_sgr_regex().sub("", comment)
        comment = strings.truncate(snippet, limit, whence=strings.Whence.Middle, msg=ellipsis)
    return comment
