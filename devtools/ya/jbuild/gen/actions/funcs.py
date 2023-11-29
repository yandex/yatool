import shutil
import subprocess
import tarfile
import os

import exts.fs as fs
import yalibrary.tools as tools


class JarError(Exception):
    mute = True


def jarx(p, dest):
    def f():
        cmd = [tools.tool('jar'), 'xvf', p]
        env = os.environ.copy()
        env.pop('_JAVA_OPTIONS', None)

        pr = subprocess.Popen(cmd, stderr=subprocess.PIPE, cwd=dest, env=env)
        _, err = pr.communicate()
        rc = pr.wait()

        if rc != 0:
            raise JarError(err)

    return f


def rm(p):
    def f():
        fs.ensure_removed(p)

    return f


def cp(s, d):
    def f():
        fs.copy_tree(s, d)

    return f


def mv(s, d):
    def f():
        shutil.move(s, d)

    return f


def mkdirp(p):
    def f():
        fs.create_dirs(p)

    return f


def untar_all(s, d):
    def f():
        with tarfile.open(s) as tf:
            tf.extractall(d)

    return f
