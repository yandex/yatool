from __future__ import print_function
import os
import sys
import re
import base64
import zlib
import logging
import zipfile

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
# * apply_zipatch - apply as zipatch


TRANSFORMATION_HANDLERS = [
    'read_file',
    'download_url',
    'download_rbtorrent',
    'decode_base64',
    'decompress_zlib',
    'apply_unified_diff',
    'apply_zipatch',
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
    (trs, data) = spec.split(TRANSFORMATIONS_DATA_SEPARATOR, 1)
    trs = trs.split(TRANSFORMATIONS_SEPARATOR)
    return (trs, data)


def convert_patch_spec(spec):
    """
    converts any known patch spec to universal spec
    """
    if is_universal_spec(spec):
        (trs, data) = parse_universal_spec(spec)

        # hack to read files in-place
        if trs[0] == 'read_file':
            trs = trs[1:]
            data = read_file(data)

        return (trs, data)

    zipatch = False
    trs = ['apply_unified_diff']
    needs_compression = True
    if spec.startswith('zipatch:'):
        zipatch = True
        trs = ['apply_zipatch']
        needs_compression = False
        spec = spec[len('zipatch:') :]

    if is_url(spec):
        trs = ['download_url'] + trs
        return (trs, spec)

    if is_rbtorrent_id(spec):
        trs = ['download_rbtorrent'] + trs
        return (trs, spec)

    if is_file(spec):
        if zipatch:
            z = zipfile.ZipFile(spec, 'r')
            assert z.testzip() is None
        data = read_file(spec)
    else:
        if zipatch:
            raise Exception("you can not embed zipatch data into command line argument")
        data = spec

    if not zipatch:
        if not is_valid_unified_diff(data):
            raise Exception("diff is invalid")

    if needs_compression:
        data = zlib.compress(six.ensure_binary(data), 9)
        trs = ['decompress_zlib'] + trs

    data = six.ensure_str(base64.b64encode(six.ensure_binary(data)))
    trs = ['decode_base64'] + trs

    return (trs, data)


def generate_universal_spec(zipatch_path, separate_transformations=False):
    patch_transformations = ['apply_zipatch']
    patch_data = open(zipatch_path).read()

    zlib_patch_data = zlib.compress(patch_data, 9)
    if len(zlib_patch_data) < len(patch_data):
        patch_transformations = ['decompress_zlib'] + patch_transformations
        patch_data = zlib_patch_data

    patch_transformations = ['decode_base64'] + patch_transformations
    patch_data = six.ensure_str(base64.b64encode(six.ensure_binary(patch_data)))

    transformations = TRANSFORMATIONS_SEPARATOR.join(patch_transformations)
    if separate_transformations:
        return (transformations, patch_data)
    else:
        return combine_transformations_with_patch(transformations, patch_data)


def combine_transformations(transformations):
    return TRANSFORMATIONS_SEPARATOR.join(transformations)


def combine_transformations_with_patch(transformations, patch_data):
    return TRANSFORMATIONS_DATA_SEPARATOR.join([transformations, patch_data])


if __name__ == "__main__":
    print(repr(convert_patch_spec(sys.argv[1])))
