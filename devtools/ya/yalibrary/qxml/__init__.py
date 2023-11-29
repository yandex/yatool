import codecs
import xml.etree.ElementTree
import xml.dom.minidom

import six

import exts.fs
import exts.tmp


class QxmlError(Exception):
    pass


class QxmlElem(object):
    def __init__(self, q):
        self.tag = q.tag
        self.text = q.text
        self.key = q.attrib.get('key')
        self.typ = q.attrib['type']

    def qxml(self, key=None):
        attrib = {
            'type': self.typ,
        }
        if key is not None:
            attrib['key'] = key
        elem = xml.etree.ElementTree.Element(self.tag, attrib=attrib)
        elem.text = self.text
        return elem


def to_qxml(data, key=None):
    if isinstance(data, QxmlElem):
        return data.qxml(key)
    tag = 'value'
    text = None
    attrib = {}
    if key is not None:
        assert isinstance(key, str)
        attrib['key'] = key
    if isinstance(data, bool):
        attrib['type'] = 'bool'
        text = 'true' if data else 'false'
    elif isinstance(data, int):
        attrib['type'] = 'int'
        text = str(data)
    elif isinstance(data, six.text_type):
        attrib['type'] = 'QString'
        text = data
    elif isinstance(data, six.binary_type):
        attrib['type'] = 'QByteArray'
        text = six.ensure_str(data)
    elif isinstance(data, list):
        tag = 'valuelist'
        attrib['type'] = 'QVariantList'
    elif isinstance(data, dict):
        tag = 'valuemap'
        attrib['type'] = 'QVariantMap'
    else:
        raise QxmlError('Data type is not supported: {}'.format(type(data)))
    value = xml.etree.ElementTree.Element(tag, attrib=attrib)
    if text is not None:
        value.text = text
    if attrib['type'] == 'QVariantList':
        for elem in data:
            value.append(to_qxml(elem))
    elif attrib['type'] == 'QVariantMap':
        for k in sorted(data.keys()):
            value.append(to_qxml(data[k], key=k))
    return value


def from_qxml(q):
    assert 'type' in q.attrib
    t = q.attrib['type']
    key = q.attrib.get('key')
    if t == 'bool':
        return q.text == 'true', key
    elif t == 'int':
        return int(q.text), key
    elif t == 'QString':
        return six.ensure_text(q.text if q.text is not None else u''), key
    elif t == 'QByteArray':
        return six.ensure_binary(q.text if q.text is not None else ''), key
    elif t == 'QVariantList':
        return [from_qxml(child)[0] for child in q], key
    elif t == 'QVariantMap':
        return {k: v for v, k in (from_qxml(child) for child in q)}, key
    else:
        return QxmlElem(q), key


def to_qxml_tree(data, root_tag):
    assert isinstance(data, dict)
    root = xml.etree.ElementTree.Element(root_tag)
    tree = xml.etree.ElementTree.ElementTree(root)
    for key in sorted(data.keys()):
        assert isinstance(key, str)
        data_elem = xml.etree.ElementTree.SubElement(root, 'data')
        variable = xml.etree.ElementTree.SubElement(data_elem, 'variable')
        variable.text = key
        data_elem.append(to_qxml(data[key]))
    return tree


def from_qxml_tree(tree):
    root = tree.getroot()
    result = {}
    for data in root:
        assert data.tag == 'data'
        assert len(data) == 2
        variable, subq = list(data)
        assert variable.tag == 'variable'
        subq_value, subq_key = from_qxml(subq)
        assert subq_key is None
        result[variable.text] = subq_value
    return result, root.tag


def save(path, data, root, doctype=None, comment=None):
    tree = to_qxml_tree(data, root)
    with exts.tmp.temp_path(path + '.tmp') as tmp_path:
        with codecs.open(tmp_path, 'wb', encoding='utf-8', errors='replace') as f:
            f.write(u'<?xml version="1.0" encoding="UTF-8"?>\n')
            if doctype:
                f.write(u'<!DOCTYPE {}>\n'.format(doctype))
            if comment:
                f.write(u'<!-- {} -->\n'.format(comment))
            # XXX: switch to minidom completely
            xml_data = xml.etree.ElementTree.tostring(tree.getroot(), encoding='utf-8')
            xml.dom.minidom.parseString(xml_data).documentElement.writexml(f, addindent=' ', newl='\n')
        exts.fs.replace_file(tmp_path, path)


def load(path):
    with open(path, 'rb') as f:
        tree = xml.etree.ElementTree.parse(f, parser=xml.etree.ElementTree.XMLParser(encoding='utf-8'))
    return from_qxml_tree(tree)
