import logging
import os
import subprocess

import exts.fs
import exts.os2
import exts.path2
import exts.archive
import exts.tmp
import exts.func

import package

logger = logging.getLogger(__name__)


def setup(
    content_dir,
    result_dir,
    publish_to_list,
    access_key,
    secret_key,
    wheel_python3,
    wheel_platform,
    wheel_limited_api,
    package_version,
):
    setup_script = os.path.join(content_dir, "setup.py")
    if not os.path.exists(setup_script):
        raise package.packager.YaPackageException("setup.py script not found")
    python = "python3" if wheel_python3 else "python"
    env = os.environ.copy()
    if package_version:
        env["YA_PACKAGE_VERSION"] = package_version
    cmd = ["setup.py", "bdist_wheel"]
    if wheel_platform:
        cmd += ["--plat-name", wheel_platform]
    if wheel_limited_api:
        cmd += ["--py-limited-api", wheel_limited_api]

    package.process.run_process(python, cmd, cwd=content_dir, env=env)

    if os.path.exists(os.path.join(content_dir, "dist")):
        logger.info("Has result in dist")
        import glob

        dists = glob.glob(os.path.join(content_dir, "dist", "*.whl"))
        if len(dists) == 1:
            logger.info("Wheel distributive created %s", os.path.basename(dists[0]))
            package_path = os.path.join(result_dir, os.path.basename(dists[0]))
            exts.fs.copy_file(dists[0], package_path)
            publish_to_repo(dists[0], publish_to_list, content_dir, access_key, secret_key)
            return package_path
        else:
            raise package.packager.YaPackageException("No wheel distributive generated")
    else:
        raise package.packager.YaPackageException("NO dist")


def publish_to_repo(package_path, publish_to_list, cwd, access_key_path, secret_key_path):
    if not publish_to_list:
        return

    access_key = None
    if access_key_path and os.path.exists(access_key_path):
        with open(access_key_path) as f:
            access_key = f.read().strip()

    secret_key = None
    if secret_key_path and os.path.exists(secret_key_path):
        with open(secret_key_path) as f:
            secret_key = f.read().strip()

    repo_keys = []
    if access_key:
        repo_keys.extend(["-u", access_key])
    if secret_key:
        repo_keys.extend(["-p", secret_key])

    for rep in publish_to_list:
        try:
            cmd = ["twine", "upload"]
            cmd.extend(['--repository-url', rep])

            cmd.extend(repo_keys)
            cmd.append(package_path)

            process = subprocess.Popen(cmd, cwd=cwd)

            if access_key and access_key in cmd:
                cmd[cmd.index(access_key)] = access_key[:2] + "***" + access_key[-2:]

            if secret_key and secret_key in cmd:
                cmd[cmd.index(secret_key)] = secret_key[:2] + "***" + secret_key[-2:]

            package.display.emit_message('Running command: [[imp]]\'{}\''.format(cmd))

            rc = process.wait()
            package.display.emit_message('[[{}]]RC = {}'.format("bad" if rc else "good", rc))
            if rc != 0:
                raise package.packager.YaPackageException("Failed to publish package: RC = {}".format(rc))

        except Exception as e:
            logger.error("Failed to publish package %s -- Exception: %s", os.path.basename(package_path), str(e))
            raise package.packager.YaPackageException(
                "Failed to publish package {} -- Exception: {}".format(os.path.basename(package_path), e)
            )
