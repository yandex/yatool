import json
import subprocess
import yalibrary.tools as tools


class StatInfo:
    def __init__(self, stat_json):
        self.stat_json = stat_json

    def __eq__(self, other):
        return self.stat_json == other.stat_json

    def is_empty(self):
        return len(self.stat_json["status"]) == 0

    def is_staged(self, path):
        for info in self.stat_json["status"].get("staged", []):
            if info["path"] == path:
                return True
        return False

    def is_untracked(self, path):
        for info in self.stat_json["status"].get("untracked", []):
            if info["path"] == path:
                return True
        return False

    def is_deleted(self, path):
        for info in self.stat_json["status"].get("changed", []):
            if info["path"] == path and info["status"] == "deleted":
                return True
        return False

    def get_untracked(self):
        res = set()
        for info in self.stat_json["status"].get("untracked", []):
            res.add(info["path"])
        return res

    def get_renamed(self):
        res = set()
        for info in self.stat_json["status"].get("staged", []):
            if info["status"] == "renamed":
                res.add(info["path"])
        return res

    def get_paths(self):
        res = set()
        for info in self.stat_json["status"].get("staged", []):
            res.add(info["path"])
        for info in self.stat_json["status"].get("changed", []):
            res.add(info["path"])
        for info in self.stat_json["status"].get("untracked", []):
            res.add(info["path"])
        return list(res)


class Arc:
    def __init__(self, source_root):
        self._arc_bin = tools.tool("arc")
        self.source_root = source_root

    def _run_arc(self, command, fail_msg_prefix="Unable to execute arc command"):
        p = subprocess.Popen([self._arc_bin] + command, cwd=self.source_root, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = p.communicate()
        return_code = p.poll()
        if return_code:
            raise Exception("{} `{}`. Return code: {}.\nOUTPUT: {}\nERROR: {}".format(
                fail_msg_prefix, " ".join(command), return_code, out, err
            ))
        return out.strip()

    def info(self, json_out=True):
        if json_out:
            return json.loads(self._run_arc(["info", "--json"]))
        else:
            return self._run_arc()

    def copy_info_set(self, path_from, path_to):
        return self._run_arc(["copy-info", "set", path_from, path_to])

    def copy_info_remove(self, path):
        return self._run_arc(["copy-info", "remove", path])

    def add(self, paths=None, add_all=False):
        if add_all:
            return self._run_arc(["add", "--all"])
        elif paths:
            return self._run_arc(["add"] + paths)
        else:
            raise ValueError("Paths was not provided")

    def checkout(self, path):
        return self._run_arc(["checkout", path])

    def stat(self):
        return StatInfo(json.loads(self._run_arc(["status", "--json"])))

    def mv(self, src, dest):
        return self._run_arc(["move", src, dest])

    def rm(self, path, recursive=False):
        cmd = ["rm", path]
        if recursive:
            cmd += ["-r"]
        return self._run_arc(cmd)

    def reset(self, path, revision="HEAD"):
        self._run_arc(["reset", revision, path])
