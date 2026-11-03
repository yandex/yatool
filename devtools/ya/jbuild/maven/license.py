import json
import logging
import os
import pathlib
import re
import subprocess
import sys

from library.python import tmp
import yalibrary.tools as tools

logger = logging.getLogger(__name__)


def enrich_with_licenses(meta):
    for data in meta:
        if "pom_file" not in data:
            continue

        licenses = data.get("licenses", [])
        if not licenses:
            contrib_dir = os.path.dirname(data.get("pom_file"))
            licenses = load_license_info(contrib_dir)

        data["licenses"] = data["extra_options"].license_transform(licenses)

    return meta


def load_license_info(contrib_dir):
    """
    Scan contrib_dir for licenses via license analyzer
    """
    with tmp.temp_dir() as td:
        td = pathlib.Path(td)
        lics_file = str(td / "licenses")
        cwd = str(td)
        license_analyzer = tools.tool('license_analyzer')

        subprocess.check_call(
            [
                license_analyzer,
                "scan",
                "--summary",
                "--dump-found-licenses",
                lics_file,
            ]
            + [contrib_dir],
            cwd=cwd,
        )

        with open(lics_file) as lf:
            found_licenses = set(json.loads(lf.read()))

        for lic in found_licenses:
            logger.info("New license: {}".format(lic))

    found_licenses = fix_licenses(found_licenses)
    return found_licenses


def fix_licenses(lics):
    """
    Remove OR, whitespace, double quotes and parenthesis.

    For correct processing, for example "(MIT OR BSD-3-Clause)"
    """
    result = set()
    for lic in lics:
        parts = re.split(r"\s+OR\s+", lic)
        if len(parts) > 1:
            result.update({i.strip('"()').strip() for i in parts})
        else:
            result.add(lic.strip('"()').strip())
    return result


UNKNOWN_LICENSE = 'Unknown'
LICENSE_CONFIGS = ('build/conf/license.json', 'build/internal/conf/license.json')

STRING_OR_COMMENT = re.compile(r'"(?:[^"\\]|\\.)*"|//[^\n]*')


def normalize_license_name(name):
    return ' '.join(name.split())


def load_known_license_ids(arc_root):
    """
    License ids known to ymake: both license.json files, '//' comments stripped
    """
    known = set()
    for cfg in LICENSE_CONFIGS:
        path = os.path.join(arc_root, cfg)
        if not os.path.exists(path):
            continue
        with open(path) as f:
            text = STRING_OR_COMMENT.sub(lambda m: '' if m.group().startswith('//') else m.group(), f.read())
        try:
            config = json.loads(text)
        except Exception as e:
            logger.error("Can't load license ids from {}: {}".format(path, e))
            continue
        for group, value in config.items():
            if group == 'restricted':
                known.update(value)
            else:
                for ids in value.values():
                    known.update(ids)
    return known


def is_valid_spdx(expr, known):
    """
    Same grammar as ymake: expr := term (OR term)*; term := factor (AND factor)*;
    factor := '(' expr ')' | id [WITH id], where 'id WITH id' is looked up as one id
    """
    tokens = re.findall(r'[()]|[^\s()]+', expr)
    pos = [0]

    def peek():
        return tokens[pos[0]] if pos[0] < len(tokens) else None

    def take():
        pos[0] += 1
        return tokens[pos[0] - 1]

    def factor():
        if peek() == '(':
            take()
            expression()
            if take() != ')':
                raise ValueError
            return
        lic = take()
        if lic in ('AND', 'OR', 'WITH', ')'):
            raise ValueError
        if peek() == 'WITH':
            take()
            lic += ' WITH ' + take()
        if lic not in known:
            raise ValueError

    def term():
        factor()
        while peek() == 'AND':
            take()
            factor()

    def expression():
        term()
        while peek() == 'OR':
            take()
            term()

    try:
        expression()
    except (ValueError, IndexError):
        return False
    return pos[0] == len(tokens)


def license_macro_value(lic, licenses_count):
    """
    LICENSE() joins values with AND and quotes only values wrapped in parentheses as a whole
    """
    if '(' in lic or (' OR ' in lic and licenses_count > 1):
        return '({})'.format(lic)
    return lic


def build_licenses_aliases(license_aliases_file, meta, arc_root):
    """Use known SPDX expressions or explicit aliases; other names become Unknown."""
    license_aliases = {}
    if license_aliases_file and os.path.exists(license_aliases_file):
        try:
            with open(license_aliases_file) as f:
                license_aliases = json.loads(f.read())
        except Exception as e:
            logger.error("Can't load license aliases file: {} {}".format(license_aliases_file, e))
    else:
        logger.error("License aliases is not available: file {} not found".format(license_aliases_file))

    known = load_known_license_ids(arc_root)
    names = {normalize_license_name(i) for data in meta for i in data.get('licenses', []) if i and i.strip()}
    result = {}
    for name in names:
        if is_valid_spdx(name, known):
            result[name] = name
        elif name in license_aliases:
            result[name] = license_aliases[name]
        else:
            result[name] = UNKNOWN_LICENSE
            logger.warning(
                "License '{}' could not be resolved; LICENSE({}) is written, consumers with "
                "LICENSE_RESTRICTION(ALLOW_ONLY ...) cannot depend on it until an alias is added to {}".format(
                    name, UNKNOWN_LICENSE, license_aliases_file
                )
            )
    return result


def collect_licenses_spdx_map(licenses):
    if not (sys.platform.startswith('linux') or sys.platform.startswith('darwin')):
        return {}
    if not licenses:
        return {}
    with tmp.temp_dir() as td:
        input_file = os.path.join(td, 'licenses.list')
        output_file = os.path.join(td, 'aliases')
        with open(input_file, 'w') as lics_file:
            lics_file.write('\n'.join(licenses))
        license_analyzer = tools.tool('license_analyzer')
        subprocess.call([license_analyzer, 'canonize', '--input-file', input_file, '--output-file', output_file])
        if not os.path.exists(output_file):
            return {}
        try:
            with open(output_file) as out:
                return json.loads(out.read())
        except Exception as e:
            logger.warning("Can't get licenses spdx ids: {}".format(e))
    return {}
