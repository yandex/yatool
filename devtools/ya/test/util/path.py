# Beware.
# This module is cythonized at build time and
# that's why it should stay simple and small.
# Huge files with complex logic can significantly
# slow down the build.


def canonize_path(path):
    """
    Append os.sep to the path if needed
    :param path: path to canonize
    :return: canonized path
    """
    return path if path.endswith('/') else path + '/'


def is_sub_path(sub_path, path):
    """
    Check if 'sub_path' is a real sub path of 'path'
    """
    sub_path = canonize_path(sub_path)
    path = canonize_path(path)
    return sub_path.startswith(path)
