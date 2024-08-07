import os
import shutil
import subprocess
from zipfile import ZipFile, ZIP_DEFLATED, ZIP_STORED

import exts.tmp
import yalibrary.tools as tools


def get_publish_cmd(settings_path, filename, version):
    return [
        tools.tool("mvn"),
        '-s',
        settings_path,
        "deploy:deploy-file",
        "-Dfile={}".format(filename),
        "-Dversion={}".format(version),
    ]


def create_aar_package(result_dir, package_dir, package_context, compress=True, publish_to_list=None):
    archive_file = package_context.resolve_filename(extra={"package_ext": "aar"})

    with exts.tmp.temp_dir() as temp_dir:
        zip_archive = os.path.join(temp_dir, archive_file)
        with ZipFile(zip_archive, 'w', compression=ZIP_DEFLATED if compress else ZIP_STORED) as aar_archive:
            for root, dirs, files in os.walk(package_dir):
                for f in files:
                    filepath = os.path.join(root, f)
                    aar_archive.write(filepath, os.path.relpath(filepath, package_dir))

        result_path = os.path.join(result_dir, os.path.basename(archive_file))
        shutil.move(zip_archive, result_path)
    if publish_to_list:
        env = os.environ.copy()
        env["JAVA_HOME"] = str(os.path.abspath(os.path.join(tools.tool("java"), '..', '..')))
        for setting in publish_to_list:
            cmd = get_publish_cmd(setting, result_path, package_context.version)
            subprocess.check_call(cmd, env=env)
    return result_path
