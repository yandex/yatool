import exts.yjson as json
import re
import os
import six

import devtools.ya.core.config

import devtools.ya.build.ymake2

from yalibrary.vcs import vcsversion
from devtools.ya.build.build_facade import gen_conf
from exts.strtobool import strtobool


class _Markdown:
    header = '#'
    # Source-link prefix. Same URL host works with both SVN revision and commit hash
    # as the `?rev=` value.
    alink = 'https://a.yandex-team.ru/arcadia/'
    # Section header level (## Multimodules etc.).
    scount = 2
    # Entity header level (### NAME).
    dcount = 3

    internal_pattern = re.compile(r'^\s*[#@]?\s*internal[.]?\s*$', flags=re.IGNORECASE)
    deprecated_pattern = re.compile(r'^\s*[#@]?\s*deprecated[.]?\s*$', flags=re.IGNORECASE)
    # 1-6 leading hashes followed by space (= ATX header). Body lines like "# internal"
    # would otherwise be rendered as an h1 inside the entity section.
    atx_header_pattern = re.compile(r'^(#{1,6})\s')
    h_patterns = {
        'internal': re.compile(r'#\s*internal\b', flags=re.IGNORECASE),
        'deprecated': re.compile(r'#\s*deprecated\b', flags=re.IGNORECASE),
    }
    example_pattern = re.compile(r'^\s*@example\b\s*$', flags=re.IGNORECASE)
    # "Word:" at the start of a line: turns into "**Word:**".
    bold_label_pattern = re.compile(r'^(\w+):(?=\s|$)')
    # "@word:" anywhere: strip @, bold (otherwise YFM/GitHub treats @word as a user mention).
    bold_at_label_pattern = re.compile(r'(?:^|(?<=\s))@(\w+):(?=\s|$)')

    type_singular = {
        'multimodules': 'Multimodule',
        'modules': 'Module',
        'macros': 'Macro',
        'properties': 'Property',
        'variables': 'Variable',
        'unknowns': 'Unknown',
    }

    section_order = ['multimodules', 'modules', 'macros', 'properties', 'variables', 'unknowns']

    def __init__(self, arc_root, dump_all_descs, use_svn, replacements):
        self.descs = {k: {} for k in self.section_order}
        self.replacements = replacements
        # `rev` is a string used as the `?rev=` query value: SVN revision (numeric string)
        # when available, otherwise the commit hash. Empty when VCS info is unavailable or
        # explicitly disabled (`-DNO_SVN_DEPENDS=yes`).
        self.rev = self._resolve_revision(arc_root) if use_svn else ''
        self.dump_all_descs = dump_all_descs

    @staticmethod
    def _resolve_revision(arc_root):
        try:
            info = vcsversion.VcsInfo(arc_root).get_info()
        except Exception:
            return ''
        if not info:
            return ''
        for key in ('revision', 'commit_revision', 'svn_commit_revision'):
            value = info.get(key)
            if value and int(value) > 0:
                return str(int(value))
        commit_hash = info.get('hash')
        return str(commit_hash) if commit_hash else ''

    def process_entry(self, file, entry):
        if 'kind' not in entry or entry['kind'] != 'node':
            return
        self._add_entry_optionally(file, entry)

    def dump(self):
        res = self._dump_toc()

        for type in self.section_order:
            if self.descs[type]:
                res += self._format_section(type)
                for name in sorted(self.descs[type]):
                    res += six.ensure_str(self.descs[type][name]['text'])

        undoc = self._collect_undocumented()
        if undoc:
            res += self._format_undocumented(undoc)

        for type in self.section_order:
            if self.descs[type]:
                for name in sorted(self.descs[type]):
                    res += six.ensure_str(self.descs[type][name]['link'])

        if self.replacements:
            res = self.apply_replacements(res)
        return res

    @staticmethod
    def _plural(t):
        if t == 'property':
            return 'properties'
        return t + 's'

    def _add_entry_optionally(self, file, entry):
        props = entry['properties']
        doc = {'name': props['name'], 'type': props['type'], 'file': file, 'line': entry['range']['line']}
        if 'comment' in props:
            doc['long desc'] = props['comment']
        if 'usage' in props:
            doc['usage'] = props['usage']

        body, special_tags, undocumented = self._parse_desc(doc.get('long desc', ''))

        is_internal = doc['name'].startswith('_') or 'internal' in special_tags
        is_deprecated = 'deprecated' in special_tags

        usage = (doc.get('usage') or '').strip()
        if usage:
            if self.h_patterns['internal'].search(usage):
                is_internal = True
            if self.h_patterns['deprecated'].search(usage):
                is_deprecated = True
            usage = re.sub(r'\s*#\s*(internal|deprecated)\b\.?', '', usage, flags=re.IGNORECASE).strip()

        if not self.dump_all_descs and is_internal:
            return

        doc['undocumented'] = undocumented
        doc['internal'] = is_internal
        doc['deprecated'] = is_deprecated

        rendered = self._render_entity(doc, body, usage, is_deprecated, is_internal, undocumented)
        link = self._format_link(doc)

        bucket = (
            self.descs[self._plural(doc['type'])]
            if doc['type'] in ('macro', 'module', 'multimodule', 'property', 'variable')
            else self.descs['unknowns']
        )
        bucket[doc['name']] = {'text': rendered, 'link': link, 'src_data': doc}

    def _parse_desc(self, raw):
        """Parse a raw doc-comment into a structured body, special-tag list, and undocumented flag.

        Recognised constructs (everything else falls through to body['lines']):
        - "@see ..." / "See: ..." line -> body['see_also']
        - "@example" marker followed by an indented block -> body['examples']
        - bare "internal" / "@internal" / "# internal" line (same for deprecated) -> special_tags
        """
        body = {'lines': [], 'examples': [], 'see_also': []}
        special_tags = []

        if not raw or not raw.strip():
            return body, special_tags, True

        lines = self._strip_blanks(raw.rstrip().splitlines())

        i = 0
        while i < len(lines):
            line = lines[i]

            see = self._extract_see(line)
            if see is not None:
                if see:
                    body['see_also'].append(see)
                i += 1
                continue

            if self.example_pattern.match(line):
                block, i = self._consume_example_block(lines, i + 1)
                if block:
                    body['examples'].append(block)
                continue

            tag = self._match_tag(line)
            if tag:
                special_tags.append(tag)
                i += 1
                continue

            body['lines'].append(line)
            i += 1

        while body['lines'] and not body['lines'][-1].strip():
            body['lines'].pop()

        is_undocumented = not body['lines'] and not body['examples'] and not body['see_also']
        return body, special_tags, is_undocumented

    @staticmethod
    def _extract_see(line):
        """If `line` is a "@see ..." or "See: ..." reference, return its content (may be empty).
        Otherwise return None so the caller can fall through to other matchers."""
        stripped = line.lstrip()
        lower = stripped.lower()
        if lower.startswith('@see'):
            rest = stripped[4:]
        elif lower.startswith('see:'):
            rest = stripped[4:]
        else:
            return None
        rest = rest.lstrip()
        if rest.startswith(':'):
            rest = rest[1:].lstrip()
        rest = rest.rstrip().rstrip('.').rstrip()
        return rest

    @classmethod
    def _consume_example_block(cls, lines, i):
        """Read an indented block (4-space or tab) starting at `i`, the line after @example.

        A blank line continues the block only if it precedes another indented line —
        otherwise it terminates the block. Returns (block_without_indent, index_past_block).
        """
        while i < len(lines) and not lines[i].strip():
            i += 1
        block = []
        while i < len(lines):
            ln = lines[i]
            if ln.startswith('    '):
                block.append(ln[4:])
            elif ln.startswith('\t'):
                block.append(ln[1:])
            elif not ln.strip():
                if not cls._next_nonblank_is_indented(lines, i + 1):
                    break
                block.append('')
            else:
                break
            i += 1
        while block and not block[-1].strip():
            block.pop()
        return block, i

    @staticmethod
    def _next_nonblank_is_indented(lines, j):
        while j < len(lines) and not lines[j].strip():
            j += 1
        return j < len(lines) and (lines[j].startswith('    ') or lines[j].startswith('\t'))

    @classmethod
    def _match_tag(cls, line):
        if cls.internal_pattern.match(line):
            return 'internal'
        if cls.deprecated_pattern.match(line):
            return 'deprecated'
        return None

    @staticmethod
    def _toc_letter(name):
        """Pick a TOC bucket letter for `name`.

        Internal-style macros (leading '_') would all collide into a single bucket
        in --dump-all output; route them by their second character so '_SRC_yasm'
        lives under 'S' alongside 'SRCS', '_TS_BASE_UNIT' under 'T', etc.
        Non-alphabetic leading chars fall back to '#'.
        """
        if name.startswith('_') and len(name) > 1 and name[1].isalpha():
            return name[1].upper()
        if name and name[0].isalpha():
            return name[0].upper()
        return '#'

    def _render_entity(self, doc, body, usage, is_deprecated, is_internal, undocumented):
        out = []
        name = doc['name']
        type_word = self.type_singular.get(self._plural(doc['type']), 'Unknown')
        src_url = self._source_url(doc)
        tags = []
        if is_deprecated:
            tags.append('deprecated')
        if is_internal:
            tags.append('internal')
        tags_suffix = ' _({})_'.format(', '.join(tags)) if tags else ''
        anchor = '<a name="{atype}_{aname}"></a>'.format(atype=doc['type'], aname=name)

        out.append(
            '{markup} {type_word} [{name}]({url}){tags} {anchor}'.format(
                markup=self.header * self.dcount,
                type_word=type_word,
                name=name,
                url=src_url,
                tags=tags_suffix,
                anchor=anchor,
            )
        )
        out.append('')

        # Only emit a signature block when we actually have args (parens).
        if usage and '(' in usage:
            out.append('```ya.make')
            out.append(usage)
            out.append('```')
            out.append('')

        if undocumented:
            out.append('_Not documented yet._')
            out.append('')
        else:
            if body['lines']:
                out.extend(self._bold_inline_labels(body['lines']))
                out.append('')
            for ex in body['examples']:
                out.append('**Example:**')
                out.append('')
                out.append('```ya.make')
                out.extend(ex)
                out.append('```')
                out.append('')
            if body['see_also']:
                out.append('**See also:** ' + '; '.join(body['see_also']))
                out.append('')

        return '\n'.join(out) + '\n'

    @classmethod
    def _bold_inline_labels(cls, lines):
        out = []
        for line in lines:
            # Escape stray ATX-style headers so they don't break entity scoping.
            if cls.atx_header_pattern.match(line):
                line = '\\' + line
            new = cls.bold_at_label_pattern.sub(r'**\1:**', line)
            new = cls.bold_label_pattern.sub(r'**\1:**', new)
            out.append(new)
        return out

    def _source_url(self, doc):
        return '{baselink}{file}{rev}#L{line}'.format(
            baselink=self.alink,
            file=doc['file'],
            rev='?rev=' + self.rev if self.rev else '',
            line=doc['line'],
        )

    def _format_section(self, type):
        return '{markup} {text} <a name="{anchor}"></a>\n\n'.format(
            markup=self.header * self.scount,
            text=type.capitalize(),
            anchor=type,
        )

    def _dump_toc(self):
        flag = ' --dump-all' if self.dump_all_descs else ''
        res = '*Do not edit, this file is generated from comments to macros definitions using `ya dump conf-docs{}`.*\n\n'.format(
            flag
        )
        scope = 'and core.conf ' if self.dump_all_descs else ''
        res += '{markup} ya.make {scope}commands\n\n'.format(markup=self.header, scope=scope)
        res += 'General info: [How to write ya.make files](https://docs.yandex-team.ru/ya-make/manual/)\n\n'
        res += '{markup} Table of contents\n\n'.format(markup=self.header * 2)

        for type in self.section_order:
            if not self.descs[type]:
                continue
            res += self._format_toc_section(type)
            if type == 'macros':
                res += self._format_toc_macros()
            else:
                for name in sorted(self.descs[type]):
                    res += self._format_toc_entry(self.descs[type][name]['src_data'])

        return res + '\n'

    @classmethod
    def _format_toc_section(cls, type):
        return '   * [{section}](#{anchor})\n'.format(section=type.capitalize(), anchor=type)

    @classmethod
    def _format_toc_entry(cls, doc):
        qual = cls.type_singular.get(cls._plural(doc['type']), 'Unknown')
        return '       - {qual} [{name}](#{type}_{name})\n'.format(
            qual=qual,
            name=doc['name'],
            type=doc['type'],
        )

    def _format_toc_macros(self):
        # Macros are nested under the "* [Macros](#macros)" bullet (column 3, content at column 5).
        # The <details> blocks must be indented to col 5 to be parsed as part of the list item.
        groups = {}
        for name in sorted(self.descs['macros']):
            groups.setdefault(self._toc_letter(name), []).append(name)

        out = ['']
        indent = '     '  # 5 spaces — matches `* ` bullet content position
        for letter in sorted(groups):
            names = groups[letter]
            out.append(
                '{i}<details><summary><b>{letter}</b> &nbsp; <i>({n} {noun})</i></summary>'.format(
                    i=indent,
                    letter=letter,
                    n=len(names),
                    noun='macro' if len(names) == 1 else 'macros',
                )
            )
            out.append('')
            for nm in names:
                out.append('{i}- Macro [{name}](#macro_{name})'.format(i=indent, name=nm))
            out.append('')
            out.append('{i}</details>'.format(i=indent))
            out.append('')
        return '\n'.join(out)

    def _format_link(self, doc):
        return ' [{tag_name}]: {url}\n'.format(
            tag_name=self._escape_link_label(doc['name']),
            url=self._source_url(doc),
        )

    def _collect_undocumented(self):
        items = []
        for type in self.section_order:
            for name in sorted(self.descs[type]):
                src = self.descs[type][name]['src_data']
                if src.get('undocumented'):
                    items.append(src)
        return items

    def _format_undocumented(self, items):
        out = []
        out.append('{markup} Undocumented <a name="undocumented"></a>'.format(markup=self.header * self.scount))
        out.append('')
        out.append(
            '<details><summary>{n} {noun} without a description comment in the source. Patches welcome.</summary>'.format(
                n=len(items),
                noun='entity' if len(items) == 1 else 'entities',
            )
        )
        out.append('')
        out.append('| Name | Type | Source |')
        out.append('| --- | --- | --- |')
        for src in items:
            qual = self.type_singular.get(self._plural(src['type']), 'Unknown')
            link = self._source_url(src)
            out.append(
                '| [`{name}`](#{type}_{name}) | {qual} | [{file}:{line}]({link}) |'.format(
                    name=src['name'],
                    type=src['type'],
                    qual=qual,
                    file=src['file'],
                    line=src['line'],
                    link=link,
                )
            )
        out.append('')
        out.append('</details>')
        out.append('')
        return '\n'.join(out) + '\n'

    @staticmethod
    def _strip_blanks(lines):
        first = 0
        for line in lines:
            if not line.strip():
                first += 1
            else:
                break
        last = 0
        for line in reversed(lines):
            if not line.strip():
                last += 1
            else:
                break
        lines = lines[first : len(lines) - last]
        lines = [x.rstrip().expandtabs(4) for x in lines]
        indent = 10000
        for line in lines:
            if line:
                indent = min(indent, len(line) - len(line.lstrip()))
        if 0 < indent < 10000:
            lines = [x.replace(' ' * indent, '', 1) for x in lines]
        return lines

    @staticmethod
    def _escape_link_label(x):
        return x.replace('_', r'\_').replace('*', r'\*')

    def apply_replacements(self, res):
        for old, new in self.replacements.items():
            res = res.replace(old, new)
        return res


def _gen(
    custom_build_directory,
    build_type,
    build_targets,
    debug_options,
    flags=None,
    warn_mode=None,
    ymake_bin=None,
    platform=None,
    host_platform=None,
    target_platforms=None,
    **kwargs
):
    generation_conf = gen_conf(
        build_root=custom_build_directory,
        build_type=build_type,
        build_targets=build_targets,
        flags=flags,
        host_platform=host_platform,
        target_platforms=target_platforms,
    )
    res, evlog_dump = devtools.ya.build.ymake2.ymake_dump(
        custom_build_directory=custom_build_directory,
        build_type=build_type,
        abs_targets=build_targets,
        debug_options=debug_options,
        warn_mode=warn_mode,
        flags=flags,
        ymake_bin=ymake_bin,
        platform=platform,
        grab_stderr=True,
        custom_conf=generation_conf,
        **kwargs
    )
    return res


def dump_mmm_docs(
    build_root,
    build_type,
    build_targets,
    debug_options,
    flags,
    dump_all_conf_docs=None,
    conf_docs_json=None,
    ymake_bin=None,
    platform=None,
    host_platform=None,
    target_platforms=None,
    replacements=False,
):
    json_dump_name = os.path.join(build_root, 'ymake.dump.ydx.json')
    arc_root = devtools.ya.core.config.find_root_from(build_targets)
    null_ya_make = os.path.join(arc_root, 'build', 'docs', 'empty')

    if not conf_docs_json:
        if not os.path.exists(null_ya_make):
            raise "Empty project not found, dump conf-docs may work too long"

    res = _gen(
        custom_build_directory=build_root,
        build_type=build_type,
        # Override target
        build_targets=[null_ya_make] if not conf_docs_json else build_targets,
        debug_options=debug_options,
        flags=flags,
        ymake_bin=ymake_bin,
        platform=platform,
        host_platform=host_platform,
        target_platforms=target_platforms,
        yndex_file=json_dump_name,
    )

    if conf_docs_json:
        with open(json_dump_name) as jfile:
            res.stdout += jfile.read()
    else:
        with open(json_dump_name) as jfile:
            contents = jfile.read()
            jdata = json.loads(contents)
        no_svn = True if 'NO_SVN_DEPENDS' in flags and strtobool(flags['NO_SVN_DEPENDS']) else False
        doc = _Markdown(arc_root, dump_all_conf_docs, not no_svn, replacements)
        for efile in jdata:
            for entry in jdata[efile]:
                doc.process_entry(efile, entry)
        res.stdout += doc.dump()

    return res
