import os
import logging
import subprocess

import exts.process
import exts.fs

logger = logging.getLogger(__name__)


class AllureReportNotFoundError(Exception):
    pass


class AllureReportGenerator(object):
    def __init__(self):
        import yalibrary.tools

        self._allure_path = yalibrary.tools.tool('allure')
        self._java_path = yalibrary.tools.tool('java')

    def create(self, build_dir, output_dir):
        # type: (str, str) -> None
        exts.fs.create_dirs(output_dir)
        env = os.environ.copy()
        # Internal test reports must not load Google tags or send generation analytics.
        env["ALLURE_NO_ANALYTICS"] = "true"

        # XXX remove after allure update
        if os.path.exists(os.path.join(os.path.dirname(self._allure_path), "..", "app")):
            new_allure = False
        else:
            new_allure = True

        if new_allure:
            allure_dir = os.path.join(build_dir, "allure_merged")
            exts.fs.create_dirs(allure_dir)
            for root, dirs, files in os.walk("."):
                if "allure" in dirs:
                    for _file in os.listdir(os.path.join(root, "allure")):
                        # Allure rejects symlink attachments; copy files and history directories, following source links.
                        exts.fs.copy_tree(
                            os.path.realpath(os.path.join(root, "allure", _file)), os.path.join(allure_dir, _file)
                        )
        else:
            allure_dir = build_dir

        cmd = [self._allure_path, "generate", allure_dir, "-o", output_dir]

        if new_allure:
            cmd += ["--clean"]

        env["JAVA_HOME"] = str(os.path.abspath(os.path.join(self._java_path, "..", "..")))
        logger.debug("Executing %s with env %s", cmd, env)
        p = exts.process.popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out, err = p.communicate()
        logger.debug("Command stdout: %s", out)
        logger.debug("Command stderr: %s", err)
        if p.returncode != 0:
            if b"Could not find any allure results" in err:
                raise AllureReportNotFoundError()
            else:
                raise Exception("Could not generate allure report - exit code {}: {} {}".format(p.returncode, out, err))
