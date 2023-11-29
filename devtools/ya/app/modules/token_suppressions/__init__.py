import six
import os

import devtools.ya.core.sec as sec

from yalibrary import loggers
from core.report import set_suppression_filter


def configure(app_ctx):
    replacements = set()

    for k, v in six.iteritems(app_ctx.params.__dict__):
        if isinstance(v, (tuple, list, set)):
            for sub_v in v:
                if sec.may_be_token(k, sub_v):
                    replacements.add(sub_v)
        elif isinstance(v, dict):
            for sub_k, sub_v in six.iteritems(v):
                if sec.may_be_token(k, sub_v) or sec.may_be_token(sub_k, sub_v):
                    replacements.add(sub_v)
        elif sec.may_be_token(k, v):
            replacements.add(v)

    for k, v in six.iteritems(os.environ):
        if sec.may_be_token(k, v):
            replacements.add(v)

    additional_replacements = set()
    for replacement in replacements:
        # YA-1248, in logs we see \\n if there was \n in token value
        new_replacement = replacement.replace("\\n", "\\\\n")
        if new_replacement != replacement:
            additional_replacements.add(new_replacement)
        additional_replacements.add(replacement)

    replacements_list = sorted(list(additional_replacements))
    loggers.filter_logging(replacements_list)
    set_suppression_filter(replacements_list)

    yield replacements_list
