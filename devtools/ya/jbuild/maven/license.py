import json
import logging
import os
import pathlib
import re
import subprocess
import sys

import exts.tmp as tmp
import yalibrary.tools as tools

logger = logging.getLogger(__name__)


def enrich_with_licenses(meta):
    for data in meta:
        if "pom_file" not in data:
            continue

        contrib_dir = os.path.dirname(data.get("pom_file"))
        data["licenses"] = load_license_info(contrib_dir)
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


def build_licenses_aliases(license_aliases_file, meta, canonize_licenses):
    """
    Build license aliases map with known license and new presented in contrib
    """
    license_aliases = {}
    if license_aliases_file and os.path.exists(license_aliases_file):
        with open(license_aliases_file) as f:
            try:
                license_aliases = json.loads(f.read())
            except Exception as e:
                logger.error("Can't load license aliases file: {} {}".format(license_aliases_file, e))
    else:
        logger.error("License aliases is not available: file {} not found".format(license_aliases_file))

    if canonize_licenses:
        all_licenses = set()
        for data in meta:
            all_licenses |= {license_aliases.get(i, i) for i in data.get("licenses")}

        new_licenses = {i for i in all_licenses if i not in license_aliases}
        new_licenses_map = collect_licenses_spdx_map(list(new_licenses))
    else:
        new_licenses_map = {}

    if os.path.exists(license_aliases_file):
        try:
            updated = False
            with open(license_aliases_file) as f:
                aliases = json.loads(f.read())
            for k, v in new_licenses_map.items():
                if k not in aliases:
                    aliases[k] = v
                    updated = True
            if updated:
                with open(license_aliases_file, 'w') as f:
                    f.write(json.dumps(aliases, sort_keys=True, indent=2))
                logger.info("License aliases file {} are updated".format(license_aliases_file))
        except Exception as e:
            logger.warning("Can't update license aliases: {}".format(e))

    license_aliases.update(new_licenses_map)
    return license_aliases


def collect_licenses_spdx_map(licenses):
    if not sys.platform.startswith('linux') and sys.platform.startswith('darwin'):
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
