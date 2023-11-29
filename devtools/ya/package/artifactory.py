import logging
import os
import subprocess
from six import StringIO

from exts.os2 import change_dir
import xml.etree.ElementTree as ET
import yalibrary.tools as tools


logger = logging.getLogger(__name__)


def get_publish_cmd(settings_path):
    cmd = [tools.tool("mvn"), '-s', settings_path, "deploy:deploy-file"]
    logger.debug("maven comand: {}".format(cmd))
    return cmd


def fill_settings(settings_path, password_path, version):
    # remove namespaces to unify settings.xml format
    with open(settings_path, 'r') as afile:
        settings = afile.read()
        settings = settings.format(version=version)
        it = ET.iterparse(StringIO(settings))
        for _, el in it:
            prefix, has_namespace, postfix = el.tag.partition('}')
            if has_namespace:
                el.tag = postfix
        root = it.root
    # set password
    if password_path:
        with open(password_path, 'r') as afile:
            password = afile.read().strip()
            try:
                password_section = root.find("servers").find("server").find("password")
            except AttributeError:
                msg = "Can't find server section in settings.xml"
                logging.error(msg)
                raise AttributeError(msg)
            if password_section is not None:
                password_section.text = password
            else:
                server = root.find("servers").find("server")
                ET.SubElement(server, "password").text = password
    # set version
    if version:
        try:
            version_section = root.find("profiles").find("profile").find("properties").find("version")
        except AttributeError:
            msg = "Can't find properties section in settings.xml"
            logging.error(msg)
            raise AttributeError(msg)
        if version_section is not None:
            version_section.text = str(version)
        else:
            properties = root.find("profiles").find("profile").find("properties")
            ET.SubElement(properties, "version").text = str(version)
    updated_settings_path = os.path.abspath("settings.xml")
    ET.ElementTree(root).write(updated_settings_path)
    return updated_settings_path


def publish_to_artifactory(package_dir, package_version, artifactory_settings, password_path=None):
    env = os.environ.copy()
    env["JAVA_HOME"] = str(os.path.abspath(os.path.join(tools.tool("java"), '..', '..')))
    with change_dir(package_dir):
        updated_settings = fill_settings(artifactory_settings, password_path, package_version)
        with open(updated_settings, 'r') as afile:
            logger.debug("maven settings.xml:\n{}".format(afile.read()))
        subprocess.check_call(get_publish_cmd(updated_settings), env=env)
        os.remove(updated_settings)
