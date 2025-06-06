import os

try:
    import ymakeyaml as yaml
except Exception:
    import yaml


class PnpmWorkspace(object):
    @classmethod
    def load(cls, path):
        ws = cls(path)
        ws.read()

        return ws

    def __init__(self, path):
        if not os.path.isabs(path):
            raise TypeError("Absolute path required, given: {}".format(path))

        self.path = path
        # NOTE: pnpm requires relative workspace paths.
        self.packages = set()

    def read(self):
        with open(self.path) as f:
            parsed = yaml.load(f, Loader=yaml.CSafeLoader) or {}
            self.packages = set(parsed.get("packages", []))

    def write(self, path=None):
        if not path:
            path = self.path

        with open(path, "w") as f:
            data = {
                "packages": list(self.packages),
            }
            yaml.dump(data, f, Dumper=yaml.CSafeDumper)

    def get_paths(self, base_path=None, ignore_self=False):
        """
        Returns absolute paths of the workspace packages.
        :param base_path: base path to resolve relative dep paths
        :type base_path: str
        :param ignore_self: whether path of the current module will be excluded (if present)
        :type ignore_self: bool
        :rtype: list of str
        """
        if base_path is None:
            base_path = os.path.dirname(self.path)

        return [
            os.path.normpath(os.path.join(base_path, pkg_path))
            for pkg_path in self.packages
            if not ignore_self or pkg_path != "."
        ]

    def set_from_package_json(self, package_json):
        """
        Sets packages to "workspace" deps from given package.json.
        :param package_json: package.json of workspace
        :type package_json: PackageJson
        """
        if os.path.dirname(package_json.path) != os.path.dirname(self.path):
            raise TypeError(
                "package.json should be in workspace directory {}, given: {}".format(
                    os.path.dirname(self.path), package_json.path
                )
            )

        self.packages = set(path for _, path in package_json.get_workspace_dep_spec_paths())
        # Add relative path to self.
        self.packages.add(".")

    def merge(self, ws):
        """
        Adds `ws`'s packages to the workspace.
        :param ws: workspace to merge
        :type ws: PnpmWorkspace
        """
        dir_path = os.path.dirname(self.path)
        ws_dir_path = os.path.dirname(ws.path)

        for p_rel_path in ws.packages:
            p_path = os.path.normpath(os.path.join(ws_dir_path, p_rel_path))
            self.packages.add(os.path.relpath(p_path, dir_path))
