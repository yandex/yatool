import sys
from pathlib import Path

import xml.etree.ElementTree as eTree

from devtools.ya.ide.gradle.config import _JavaSemConfig


class _YaSettings:
    """Save command and cwd to ya-settings.xml"""

    YA_SETTINGS_XML = 'ya-settings.xml'

    def __init__(self, java_sem_config: _JavaSemConfig):
        self.config: _JavaSemConfig = java_sem_config

    def save(self) -> None:
        self._write_xml(self._make_xml(), self.config.export_root / self.YA_SETTINGS_XML)

    @classmethod
    def _make_xml(cls) -> eTree.Element:
        xml_root = eTree.Element('root')
        cmd = eTree.SubElement(xml_root, 'cmd')
        for arg in sys.argv:
            eTree.SubElement(cmd, 'part').text = arg
        eTree.SubElement(xml_root, 'cwd').text = str(Path.cwd())
        return xml_root

    @classmethod
    def _write_xml(cls, xml_root: eTree.Element, path: Path) -> None:
        cls._elem_indent(xml_root)
        with path.open('wb') as f:
            eTree.ElementTree(xml_root).write(f, encoding="utf-8")

    @classmethod
    def _elem_indent(cls, elem, level=0) -> None:
        indent = "\n" + level * " " * 4
        if len(elem):
            if not elem.text or not elem.text.strip():
                elem.text = indent + " " * 4
            if not elem.tail or not elem.tail.strip():
                elem.tail = indent
            for elem in elem:
                cls._elem_indent(elem, level + 1)
            if not elem.tail or not elem.tail.strip():
                elem.tail = indent
        else:
            if level and (not elem.tail or not elem.tail.strip()):
                elem.tail = indent
