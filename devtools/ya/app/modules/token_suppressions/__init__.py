import devtools.ya.core.sec as sec

from yalibrary import loggers
from devtools.ya.core.report import set_suppression_filter


def configure(app_ctx):
    replacements = set(sec.mine_suppression_filter(app_ctx.params.__dict__))

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
