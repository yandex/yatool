# -*- coding: utf-8 -*-

from __future__ import division, print_function, unicode_literals

import os


def make_folder(path, exist_ok=False, need_remove=True):
    """ Check folder and create it if need"""
    from exts.fs import ensure_dir, ensure_removed

    if os.path.exists(path) and not exist_ok:
        raise NameError("Path {} exists".format(path),
                        'make sure folder does not exist or use `exist_ok=True, need_remove=True/False`')

    if not exist_ok and need_remove:
        ensure_removed(path)

    ensure_dir(path)
