from __future__ import print_function
import os
import sys
import re
import base64
import zlib
import logging

import six

logger = logging.getLogger(__name__)


def read_file(path, _=None):
    with open(path) as f:
        return f.read()


# * download_url - treat 'patch' as url and download it (http/https/ftp/...)
# * download_rbtorrent - treat 'patch' as rbtorrent identifier and download it using skynet
# * decode_base64 - decode 'patch' as base64
# * decompress_zlib - decompress using zlib
# * apply_unified_diff - apply as unified diff


TRANSFORMATION_HANDLERS = [
    'read_file',
    'download_url',
    'download_rbtorrent',
    'decode_base64',
    'decompress_zlib',
    'apply_unified_diff',
]

TRANSFORMATIONS_SEPARATOR = ','
TRANSFORMATIONS_DATA_SEPARATOR = '@'


def is_universal_spec(spec):
    trs_re = "[a-z0-9_]+"
    r = r'(?P<trs>({trs})(?:{tr_sep}({trs}))*){data_sep}(?P<data>.*)'.format(
        trs=trs_re,
        tr_sep=TRANSFORMATIONS_SEPARATOR,
        data_sep=TRANSFORMATIONS_DATA_SEPARATOR,
    )
    m = re.match(r, spec)
    if not m:
        return False

    trs = m.group('trs').split(',')
    for tr in trs:
        if tr not in TRANSFORMATION_HANDLERS:
            raise Exception("{} is not a valid transformation".format(tr))

    return True


def is_rbtorrent_id(spec):
    return spec.startswith('rbtorrent:')


def is_url(spec):
    return spec.startswith('http://') or spec.startswith('https://')


def is_file(spec):
    return os.path.isfile(spec)


def is_valid_unified_diff(diff):
    diff_re = '(({head})*{rm}{add}({hunk_head}({hunk_line})+)+(\\\\ No newline at end of file\n)?)+$'.format(
        head='[^ +-].*\n',
        rm='[-][-][-] .*\n',
        add='[+][+][+] .*\n',
        hunk_head='@@[^@]*@@.*\n',
        hunk_line='[ +-].*\n',
    )
    m = re.match(diff_re, diff, re.MULTILINE)
    return bool(m)


def parse_universal_spec(spec):
    trs, data = spec.split(TRANSFORMATIONS_DATA_SEPARATOR, 1)
    trs = trs.split(TRANSFORMATIONS_SEPARATOR)
    return (trs, data)


def convert_patch_spec(spec):
    """
    converts any known patch spec to universal spec
    """
    if is_universal_spec(spec):
        trs, data = parse_universal_spec(spec)

        # hack to read files in-place
        if trs[0] == 'read_file':
            trs = trs[1:]
            data = read_file(data)

        return (trs, data)

    trs = ['apply_unified_diff']

    if is_url(spec):
        trs = ['download_url'] + trs
        return (trs, spec)

    if is_rbtorrent_id(spec):
        trs = ['download_rbtorrent'] + trs
        return (trs, spec)

    if is_file(spec):
        data = read_file(spec)
    else:
        data = spec

    if not is_valid_unified_diff(data):
        raise Exception("diff is invalid")

    data = zlib.compress(six.ensure_binary(data), 9)
    trs = ['decompress_zlib'] + trs

    data = six.ensure_str(base64.b64encode(six.ensure_binary(data)))
    trs = ['decode_base64'] + trs

    return (trs, data)


def combine_transformations(transformations):
    return TRANSFORMATIONS_SEPARATOR.join(transformations)


def combine_transformations_with_patch(transformations, patch_data):
    return TRANSFORMATIONS_DATA_SEPARATOR.join([transformations, patch_data])


if __name__ == "__main__":
    print(repr(convert_patch_spec(sys.argv[1])))
