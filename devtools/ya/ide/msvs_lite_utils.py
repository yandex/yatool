from __future__ import absolute_import
import os

from exts.hashing import md5_value


def to_guid(path):
    h = md5_value(path)
    return '{}-{}-{}-{}-{}'.format(h[:8], h[8:12], h[12:16], h[16:20], h[20:]).upper()


def get_ext(path):
    return os.path.splitext(path)[1]
