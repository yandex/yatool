import collections
import logging
import typing as tp  # noqa: F401
from itertools import groupby
import six

import app_config
from devtools.ya.core.yarg.consumers import Consumer  # noqa: F401
from devtools.ya.core.yarg.consumers import ArgConsumer, get_consumer
from devtools.ya.core.yarg.excs import BaseOptsFrameworkException
from devtools.ya.core.yarg.help_level import HelpLevel
from devtools.ya.core.yarg.hooks import UpdateValueHook, SetValueHook
from devtools.ya.core.yarg.groups import Group  # noqa: F401
from devtools.ya.core.yarg.groups import OPERATIONAL_CONTROL_GROUP, UNCATEGORIZED_GROUP
from devtools.ya.core.yarg.options import Options  # noqa: F401


class ShowHelpException(BaseOptsFrameworkException):
    def __init__(self, help_level=HelpLevel.BASIC, help_search=None):
        super(ShowHelpException, self).__init__()
        self.help_level = HelpLevel(help_level)
        self.help_search = help_search


def format_usage(opt):
    result = ['[OPTION]...']
    parts = get_consumer(opt).parts
    free_parts = [x for x in parts if x.free]
    result.extend(['[{0}]...'.format(x.help) for x in free_parts])
    return ' '.join(result)


def iterate_options(options):
    # type: (Options) -> tp.Iterable[Group, list[Consumer]]
    consumer = get_consumer(options)
    sorted_parts = sorted(
        [p for p in consumer.parts if p.visible.value < HelpLevel.NONE.value],
        key=lambda x: 1 << 32 if x.group is None else x.group.index,
    )
    grouped = groupby(sorted_parts, lambda x: x.group)

    for group, args in grouped:
        yield group, list(args)


def iterate_subgroups(options):
    sorted_parts = sorted(
        [p for p in options], key=lambda x: x.subgroup.index if getattr(x, 'subgroup', None) else 1 << 32
    )
    grouped = groupby(sorted_parts, lambda x: x.subgroup)

    for subgroup, args in grouped:
        yield subgroup, list(args)


def format_help(options, help_level=HelpLevel.BASIC, search_query=None):
    # type: (Options, HelpLevel, str) -> str
    result = ['Options:']

    for group, args in iterate_options(options):
        if not args:
            continue

        if search_query:
            if group:
                show_full_group = search_query in group.name.lower()
            else:
                show_full_group = search_query in UNCATEGORIZED_GROUP.name.lower()
        else:
            show_full_group = True

        group_result = []

        for subgroup, args in iterate_subgroups(args):
            subgroup_result = []

            show_full_subgroup = False
            if subgroup is not None and search_query:
                show_full_subgroup = search_query in subgroup.name.lower()

            level_groups = collections.defaultdict(list)
            for arg in args:
                if arg.visible.value <= help_level.value:
                    formatted_info = arg.formatted(options)
                    if formatted_info:
                        if show_full_group or show_full_subgroup or search_query in formatted_info.lower():
                            level_groups[arg.visible].append(formatted_info)

            for vis_level, descs in sorted(level_groups.items(), key=lambda x: x[0].value):
                if vis_level != HelpLevel.BASIC:
                    subgroup_result.append('    [[unimp]]{} options[[rst]]'.format(vis_level.name.lower().capitalize()))
                subgroup_result.extend(descs)

            if subgroup_result:
                if subgroup is not None:
                    subgroup_result.insert(0, '   [[alt1]]' + subgroup.name + '[[rst]]')
                    if subgroup.desc and app_config.in_house:
                        subgroup_result.insert(1, '    [[c:dark-cyan]]' + subgroup.desc + '[[rst]]')

                group_result.extend(subgroup_result)

        if group_result:
            if group is None:
                group = UNCATEGORIZED_GROUP

            group_result.insert(0, '  [[imp]]' + group.name + '[[rst]]')

            result.extend(group_result)
        elif group:
            logging.debug("Group '%s' was hidden - no visible options at %s level", group.name, help_level)

    return '\n'.join(result)


class ShowHelpOptions(Options):
    def __init__(self):
        self._print_help_level = 0
        self._print_help_search = None

    @staticmethod
    def consumer():
        return [
            ArgConsumer(
                ['-h', '--help'],
                help='Print help. Use -hh for more options and -hhh for even more.',
                hook=UpdateValueHook('_print_help_level', lambda x: x + 1),
                group=OPERATIONAL_CONTROL_GROUP,
                visible=HelpLevel.BASIC,
            ),
            ArgConsumer(
                ['--help-search'],
                help='Search and print help for specific term. Higher visibility levels (-hh/-hhh) may yield more results.',
                hook=SetValueHook(name='_print_help_search'),
                group=OPERATIONAL_CONTROL_GROUP,
                visible=HelpLevel.BASIC,
            ),
        ]

    def postprocess(self):
        if self._print_help_search:
            self._print_help_search = self._print_help_search.lower()

        if self._print_help_search and self._print_help_level == 0:
            self._print_help_level = 1
        if self._print_help_level:
            raise ShowHelpException(self._print_help_level, self._print_help_search)


class UsageExample(object):
    def __init__(self, cmd, description):
        self.cmd = cmd
        self.description = description


def format_examples(examples):
    tbl = []
    for k, v in six.iteritems(examples):
        for x in v:
            tbl.append(
                (
                    x.cmd.format(prefix=' '.join(k)),
                    x,
                )
            )

    if len(tbl) == 0:
        return ''

    max_len = max([len(x[0]) for x in tbl])
    result = '[[imp]]Examples:[[rst]]\n'

    for k, v in tbl:
        result += '  ' + k + (' ' * (max_len + 1 - len(k))) + ' ' + v.description + '\n'

    return result
