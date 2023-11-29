from __future__ import print_function
import os
from collections import deque


class Tree:
    class Node:
        def __init__(self, parent):
            self.parent = parent
            self.children = {}
            self.is_leaf = False

    def __init__(self):
        self.root = self.Node(None)

    def __bool__(self):
        return bool(self.root.children)

    @staticmethod
    def chunks(path):
        parts = deque()
        while path:
            path, tail = os.path.split(path)
            parts.appendleft(tail)
        return parts

    def add_path(self, path):
        if not path:
            return
        parts = self.chunks(path)
        branch = self.root
        for part in parts:
            if branch.is_leaf:
                return
            if part not in branch.children:
                branch.children[part] = self.Node(branch)
            branch = branch.children[part]
        branch.children = {}
        branch.is_leaf = True

    def print_tree(self, root=None):
        if root is None:
            root = self.root

        def print_branch(branch, path_part):
            if branch.is_leaf:
                print(path_part)
                return
            for chunk, leaf in sorted(branch.children.items(), key=lambda x: x[0]):
                print_branch(leaf, os.path.join(path_part, chunk))

        print_branch(root, "")

    def gen_excludes(self, root_path, relative=True, only_dirs=True):
        def list_local_path(path):
            for directory in os.listdir(path):
                abs_path = os.path.join(path, directory)
                if not only_dirs or os.path.isdir(abs_path):
                    yield abs_path

        exclude_folders = []

        def _walk_dir(path, branch):
            for local_dir in list_local_path(path):
                name = os.path.basename(local_dir)
                if name in branch.children:
                    child = branch.children[name]
                    if child.is_leaf:
                        continue
                    _walk_dir(local_dir, child)
                else:
                    if relative:
                        exclude_folders.append(os.path.relpath(local_dir, root_path))
                    else:
                        exclude_folders.append(local_dir)

        _walk_dir(root_path, self.root)
        return exclude_folders
