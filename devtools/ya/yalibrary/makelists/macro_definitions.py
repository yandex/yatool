from . import mk_common

import functools
from collections import deque


TYPE_UNKNOWN = -1
TYPE_MACRO = 100
TYPE_MACRO_MODIFIER = 101
TYPE_VALUE = 200
TYPE_COMMENT = 300
TYPE_COMM_ELEMENT = 301
TYPE_COMM_INSIDE = 302

# These macros will be formatted in columnar format despite number of elements, i. e.
# PEERDIR(
#     lib1
#     lib2
# )
COLUMNAR_LIST_MACROS = {
    'ADDINCL',
    'CFLAGS',
    'DATA',
    'DEPENDS',
    'FILES',
    'NO_CHECK_IMPORTS',
    'PEERDIR',
    'PY_REGISTER',
    'PY_SRCS',
    'RECURSE',
    'RECURSE_FOR_TESTS',
    'RECURSE_ROOT_RELATIVE',
    'RESOURCE_FILES',
    'SRCS',
    'TEST_SRCS',
}

ONE_LINE_MACROS = {
    'BASE_CODEGEN',
    'BUILDWITH_CYTHON',
    'BUILD_CATBOOST',
    'BUILD_MN',
    'BUILD_ONLY_IF',
    'CONFIGURE_FILE',
    'COPY_FILE',
    'COPY_FILE_WITH_CONTEXT',
    'DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE_BY_JSON',
    'DECLARE_EXTERNAL_RESOURCE',
    'DLL_FOR',
    'DOCS_CONFIG',
    'END',
    'ENDIF',
    'GEN_SCHEEME2',
    'MESSAGE',
    'NO_BUILD_IF',
    'PY_MAIN',
    'SOURCE_GROUP',
    'SRC',
    'SRC_C_AVX',
    'SRC_C_AVX2',
    'SRC_C_AVX512',
    'SRC_C_SSE2',
    'SRC_C_SSE3',
    'SRC_C_SSE41',
    'SRC_C_SSSE3',
    'SRC_C_XOP',
    'TASKLET_REG',
}

MAX_OUTPUT_STRING_LEN = 110


class MacroException(mk_common.MkLibException):
    pass


@functools.total_ordering
class Node(object):
    def __init__(self, name, node_type):
        self.name = name
        self.node_type = node_type
        self.children = []

    def __lt__(self, v):
        if not isinstance(v, Node):
            return NotImplemented
        if type(self).key == type(v).key:
            return self.key() < v.key()
        return self.name < v.name

    def __iter__(self):
        for node in self.children:
            yield node
            for x in iter(node):
                yield x

    def key(self):
        return self.name

    def insert_before(self, new_child, old_child):
        if old_child in self.children:
            i = self.children.index(old_child)
            self.children.insert(i, new_child)
        else:
            raise MacroException('error: no old_child in Node children')

    def insert_after(self, new_child, old_child):
        if old_child in self.children:
            i = self.children.index(old_child)
            self.children.insert(i + 1, new_child)
        else:
            raise MacroException('error: no old_child in Node children')

    def replace_child(self, new_child, old_child):
        self.insert_before(new_child, old_child)
        self.remove_child(old_child)

    def remove_child(self, old_child):
        if old_child in self.children:
            self.children.remove(old_child)
        else:
            raise MacroException('error: no old_child in Node children')

    def append_child(self, new_child):
        self.children.append(new_child)

    def is_empty(self):
        return not bool(self.children)

    def find_nodes(self, name):
        nodes = []
        for child in self.children:
            if child.name == name:
                nodes.append(child)

        return nodes

    def find_siblings(self, name=None, names=None):
        names = names or []
        if names and name:
            raise AssertionError("You can't request name and names simultaneously")
        if not names and not name:
            raise AssertionError("At least one argument is required")

        if names:

            def match(x):
                return x in names

        else:

            def match(x):
                return x == name

        queue = deque([self])
        nodes = []
        while True:
            if len(queue) == 0:
                break

            node = queue.popleft()
            if node.node_type == TYPE_MACRO and match(node.name):
                nodes.append(node)

            for el in node.children:
                queue.append(el)

        return nodes

    def _write(self):
        is_top = True
        out = []
        for child in self.children:
            if not is_top and out:
                out.append('')
            if is_top:
                is_top = False
            out += child._write()

        return out

    def write(self, file_name):
        with open(file_name, 'w') as f:
            f.write(self.dump())

    def dump(self):
        return '\n'.join(self._write()) + '\n'


class Macro(Node):
    def __init__(self, name):
        super(Macro, self).__init__(name, TYPE_MACRO)

    def add_value(self, value_name):
        value_node = Value(value_name)
        self.append_child(value_node)

    def remove_value(self, value_name):
        value_nodes = self.find_values(value_name)
        for node in value_nodes:
            self.remove_child(node)

    def get_values(self):
        if not self.is_empty():
            nodes = []
            for value_node in self.children:
                if value_node.node_type == TYPE_VALUE:
                    nodes.append(value_node)
            return nodes

        return []

    def find_values(self, name):
        nodes = []
        for value_node in self.children:
            if value_node.node_type == TYPE_VALUE and value_node.name == name:
                nodes.append(value_node)

        return nodes

    def has_values(self):
        for child in self.children:
            if child.node_type == TYPE_VALUE:
                return True

        return False

    def sort_values(self):
        value_nodes = []
        com_nodes = []
        for node in self.children:
            if node.node_type == TYPE_VALUE:
                value_nodes.append(node)
            else:
                com_nodes.append(node)

        self.children = sorted(value_nodes) + com_nodes

    MODIFIERS = {
        'PY_SRCS': ['NAMESPACE', 'MAIN'],
        'RESOURCE_FILES': [
            'PREFIX',
        ],
    }

    def set_modifier(self, node_modifier):
        if self.NAME not in self.MODIFIERS:
            raise MacroException('Macro {} can not have any modifiers'.format(self.NAME))

        if node_modifier.NAME not in self.MODIFIERS[self.NAME]:
            raise MacroException(
                'Modifier {} not allowed for macro {} (expected names are {})'.format(
                    node_modifier.NAME, self.NAME, self.MODIFIERS[self.NAME]
                )
            )

        attr_name = node_modifier.NAME.lower()
        old_modifier = getattr(self, attr_name, None)

        if old_modifier:
            self.children.remove(old_modifier)

        setattr(self, attr_name, node_modifier)
        self.children.append(node_modifier)

    @staticmethod
    def create_node(name):
        if name in mk_common.PROJECT_MACROS:
            return Project.create_custom(name)
        elif name == 'PEERDIR':
            return Peerdir()
        elif name == 'SRCS':
            return Srcs()
        elif name == 'PY_SRCS':
            return PySrcs()
        elif name == 'JAVA_SRCS':
            return JavaSrcs()
        elif name == 'DOCS_DIR':
            return DocsDir()
        elif name in ['SET', 'SET_APPEND', 'DEFAULT']:
            return Set(name)
        elif name in ['CFLAGS', 'CXXFLAGS', 'CUDA_NVCC_FLAGS']:
            return Flags(name)
        elif name == 'ADDINCL':
            return Addincl()
        elif name in ['RUN_PYTHON3', 'LUA', 'RUN_PROGRAM']:
            return Script(name)
        elif name in ['ARCHIVE', 'ARCHIVE_ASM', 'BUILD_MNS']:
            return Archive(name)
        elif name == 'RESOURCE':
            return Resource()
        elif name == 'OWNER':
            return Owner()
        elif name == 'FROM_SANDBOX':
            return FromSandbox()
        elif name == 'LICENSE':
            return LicenseMacro()
        elif name == 'RESOURCE_FILES':
            return ResourceFiles()
        elif name in ONE_LINE_MACROS:
            return StringMacro(name)
        else:
            return Macro(name)

    def parse_tokens(self, tokens):
        from .com_handler import CommentsHandler

        com_h = CommentsHandler(self, 1)
        START = 1
        VALUE = 2
        END = 3

        state = START
        for token in tokens:
            while True:
                if state == START:
                    if token.value == mk_common.V_END:
                        break

                    if token.comment and not token.value:
                        com_h.set_comment(token.comment)
                        com_h.add_new_line(inside=True)
                        break
                    elif token.comment:
                        com_h.set_comment(token.comment)

                    if token.value:
                        state = VALUE
                        val_obj = Value('')
                        com_h.set_element(val_obj)
                elif state == VALUE:
                    val_obj.name = token.value
                    state = END
                elif state == END:
                    self.children.append(val_obj)
                    state = START
                    com_h.add_new_line()
                    break

    def _write_macro(self):
        out = []
        value_output = []
        comments = get_comments_blocks(self.children)
        for comment in comments:
            out.append('{com}'.format(com=comment))

        value_output += self._write_children([TYPE_MACRO_MODIFIER])
        value_output += self._write_children([TYPE_VALUE, TYPE_COMM_ELEMENT])

        string = '{name}('.format(name=self.name)
        if not value_output:
            string += ')'
            out.append(string)
        elif len(value_output) == 1 and value_output[0].find('#') == -1 and self.name not in COLUMNAR_LIST_MACROS:
            string += '{val})'.format(val=value_output[0])
            out.append(string)
        else:
            out.append(string)
            for line in value_output:
                out.append('    ' + line)

            out.append(')')

        return out

    def _write_children(self, types):
        out = []
        for node in self.children:
            if node.node_type in types:
                out += node._write()
        return out

    def _write_if_block(self):
        out = []
        value_nodes = []
        macro_nodes = []
        comments = get_comments_blocks(self.children)

        for node in self.children:
            if node.node_type == TYPE_VALUE:
                value_nodes.append(node)
            elif node.node_type in [TYPE_MACRO, TYPE_COMM_INSIDE]:
                macro_nodes.append(node)

        for comment in comments:
            out.append('{com}'.format(com=comment))

        if self.name == 'ELSE':
            string = '{name}('.format(name=self.name)
        else:
            string = '{name} ('.format(name=self.name)

        for node in value_nodes:
            string += '{val} '.format(val=node.name)

        if string.endswith(' '):
            string = string[: len(string) - 1] + ')'
        else:
            string += ')'

        out.append(string)

        for node in macro_nodes:
            if node.name not in ['ELSE', 'ELSEIF', 'ENDIF']:
                node_out = node._write()
                for string in node_out:
                    if string:
                        out.append('    ' + string)
                    else:
                        out.append(string)
            else:
                out += node._write()

        return out

    def _write(self):
        out = []
        if self.name in ['IF', 'ELSEIF', 'ELSE']:
            out += self._write_if_block()
        else:
            out += self._write_macro()
            for node in self.children:
                if node.node_type in [TYPE_MACRO, TYPE_COMM_INSIDE]:
                    node_out = node._write()
                    if node_out:
                        out.append('')
                    out += node_out

        return out


class Value(Node):
    def __init__(self, name):
        super(Value, self).__init__(name, TYPE_VALUE)

    @staticmethod
    def create_node(value, parent):
        if isinstance(parent, Peerdir):
            return PeerdirValue(value)
        elif isinstance(parent, (Srcs, PySrcs, JavaSrcs, DocsDir)):
            return SrcValue(value)
        else:
            return Value(value)

    def __eq__(self, v):
        if not isinstance(v, Value):
            return NotImplemented
        return self.name == v.name

    def __hash__(self):
        return hash(self.name)

    def _write(self):
        out = []

        nodes = []
        for node in self.children:
            if node.node_type == TYPE_COMMENT:
                nodes.extend(node._write())

        comments = ' '.join(nodes)

        if len(self.name) + len(comments) > MAX_OUTPUT_STRING_LEN:
            if len(self.name) < MAX_OUTPUT_STRING_LEN:
                out.append(comments)
                out.append(self.name)
            else:
                words = self.name.split()
                first = True
                string = []
                cur_len = 0
                i = 0
                while i < len(words):
                    if (
                        cur_len + len(words[i]) <= MAX_OUTPUT_STRING_LEN
                        or not cur_len
                        and len(words[i]) > MAX_OUTPUT_STRING_LEN
                    ):
                        cur_len = cur_len + len(words[i])
                        string.append(words[i])
                    else:
                        if first:
                            out.append(' '.join(string))
                            first = False
                        else:
                            out.append(' '.join(['    '] + string))
                        string = []
                        cur_len = 0
                        i -= 1

                    i += 1

                if string:
                    out.append(' '.join(['    '] + string))

                if comments:
                    out[-1] += ' ' + comments
        else:
            if comments:
                out.append('{val} {com}'.format(val=self.name, com=comments))
            else:
                out.append(self.name)

        return out


class Comment(Node):
    def __init__(self, name):
        super(Comment, self).__init__(name, TYPE_COMMENT)

    def _write(self):
        out = self.name.split('\n')
        if out[-1] == '':
            out = out[:-1]
        return out


class CommElement(Node):
    def __init__(self, name):
        super(CommElement, self).__init__(name, TYPE_COMM_ELEMENT)

    def _write(self):
        out = self.name.split('\n')[:-1]
        return out


class CommInside(Node):
    def __init__(self, name):
        super(CommInside, self).__init__(name, TYPE_COMM_INSIDE)

    def _write(self):
        out = self.name.split('\n')[:-1]
        return out


class MakeList(Node):
    def __init__(self, name='root'):
        super(MakeList, self).__init__(name, TYPE_UNKNOWN)

    def _find_project(self):
        def _find_projects_recursivly(obj):
            num_proj = 0
            proj = None
            for child in obj.children:
                if isinstance(child, Project):
                    num_proj += 1
                    proj = child
                if isinstance(child, Macro) and child.name in ['IF', 'ELSE', 'ELSEIF']:
                    child_proj, child_num_proj = _find_projects_recursivly(child)
                    if child_num_proj:
                        num_proj += child_num_proj
                        proj = child_proj
            return proj, num_proj

        return _find_projects_recursivly(self)

    @property
    def project(self):
        proj, num_proj = self._find_project()

        if num_proj == 0:
            raise MacroException('error: none projects in makelist')
        elif num_proj > 1:
            raise MacroException('error: too many projects in makelist')

        return proj

    def get_values_from_macro(self, macro_name):
        nodes = self.find_siblings(macro_name)
        values = []
        for node in nodes:
            values.extend([value.name for value in node.get_values()])

        return values

    def has_project(self):
        return self._find_project()[1] > 0

    def find_parent(self, node):
        queue = deque([(self, self)])
        while True:
            if len(queue) == 0:
                raise MacroException('error: No parent')

            parent, child = queue.popleft()
            if child == node:
                return parent

            for el in child.children:
                queue.append((child, el))


class Project(Macro):
    def __init__(self, name):
        super(Project, self).__init__(name)
        if name not in mk_common.PROJECT_MACROS:
            raise MacroException('error: name - {name} is not supported'.format(name=name))

    @property
    def peerdir(self):
        peerdir_nodes = self.peerdirs(nested=False)
        if len(peerdir_nodes) > 1:
            raise MacroException('error: too many PEERDIR macros')

        if peerdir_nodes:
            return peerdir_nodes[0]

        before_node = self.find_nodes('SRCS')
        if not before_node:
            before_node = self.find_nodes('END')

        if len(before_node) != 1:
            raise MacroException('error: failed insert new PEERDIR macro')

        peerdir = Peerdir()
        self.insert_before(peerdir, before_node[0])

        return peerdir

    def peerdirs(self, nested=True):
        if nested:
            return self.find_siblings('PEERDIR')
        return self.find_nodes('PEERDIR')

    def _find_generic_srcs(self, src_class):
        srcs_nodes = self.find_siblings(src_class.NAME)
        if len(srcs_nodes) > 1:
            raise MacroException('error: too many {} macros'.format(src_class.NAME))

        if srcs_nodes:
            return srcs_nodes[0]

        before_node = self.find_nodes('END')
        if len(before_node) != 1:
            raise MacroException('error: failed insert new {} macro'.format(src_class.NAME))

        srcs = src_class()
        self.insert_before(srcs, before_node[0])

        return srcs

    def get_resource_files(self):
        return self._resource_files()

    def resource_files(self, prefix=''):
        return self._resource_files(prefix)[prefix]

    def _resource_files(self, prefix=None):
        resource_nodes = self.find_siblings(ResourceFiles.NAME)
        result = {}
        for node in resource_nodes:
            node_prefix = '' if node.prefix is None else node.prefix.value
            if prefix is not None and not prefix == node_prefix:
                continue

            if node_prefix not in result:
                result[node_prefix] = node
            else:
                raise MacroException('error: too many {} macros with prefix {}'.format(ResourceFiles.NAME, node_prefix))

        if result:
            return result

        before_node = self.find_nodes('END')
        if len(before_node) != 1:
            raise MacroException('error: failed insert new {} macro: {}'.format(ResourceFiles.NAME, before_node))

        resource_files = ResourceFiles()
        if prefix is not None:
            resource_files.set_prefix(prefix)
        self.insert_before(resource_files, before_node[0])
        result[prefix or ''] = resource_files

        return result

    def get_py_srcs(self):
        return self._py_srcs()

    def _py_srcs(self, namespace=None):
        srcs_nodes = self.find_siblings(PySrcs.NAME)
        result = {}
        for node in srcs_nodes:
            node_namespace = '' if node.namespace is None else node.namespace.value
            if namespace is not None and not namespace == node_namespace:
                continue

            if node_namespace not in result:
                result[node_namespace] = node
            else:
                raise MacroException('error: too many {} macros with prefix {}'.format(PySrcs.NAME, node_namespace))

        if result:
            return result

        before_node = self.find_nodes('END')
        if len(before_node) != 1:
            raise MacroException('error: failed insert new {} macro'.format(PySrcs.NAME))

        srcs = PySrcs()
        if namespace is not None:
            srcs.set_namespace(namespace)
        self.insert_before(srcs, before_node[0])
        result[namespace or ''] = srcs

        return result

    @property
    def srcs(self):
        return self._find_generic_srcs(Srcs)

    def py_srcs(self, namespace=''):
        return self._py_srcs(namespace)[namespace]

    @property
    def java_srcs(self):
        return self._find_generic_srcs(JavaSrcs)

    @property
    def docs_dir(self):
        return self._find_generic_srcs(DocsDir)

    @staticmethod
    def create_program():
        return Project('PROGRAM')

    @staticmethod
    def create_library():
        return Project('LIBRARY')

    @staticmethod
    def create_custom(name):
        return Project(name)

    def parse_tokens(self, tokens):
        START = 1
        VALUE = 2
        MAJ_VER = 3
        MIN_VER = 4
        MOD = 5
        EXPORTS = 6
        PREFIX = 7
        END = 8

        state = START
        change_state = ['EXPORTS', 'PREFIX', mk_common.V_END]

        for token in tokens:
            while True:
                if state == START:
                    if token.value:
                        val_obj = ProjectValue('')
                        state = VALUE
                elif state == VALUE:
                    val_obj.project_name = token.value
                    state = MOD
                    break
                elif state == MOD:
                    if token.value == 'EXPORTS':
                        state = EXPORTS
                        break
                    elif token.value == 'PREFIX':
                        state = PREFIX
                        break
                    elif token.value == mk_common.V_END:
                        state = END
                    else:
                        state = MAJ_VER
                elif state == MAJ_VER:
                    val_obj.major_version = token.value
                    state = MIN_VER
                    break
                elif state == MIN_VER:
                    if token.value in change_state:
                        state = MOD
                    else:
                        val_obj.minor_version = token.value
                        state = MOD
                        break
                elif state == PREFIX:
                    val_obj.prefix = token.value
                    state = MOD
                    break
                elif state == EXPORTS:
                    if token.value in change_state:
                        state = MOD
                    else:
                        val_obj.exports.append(token.value)
                        break
                elif state == END:
                    val_obj._update()
                    self.children.append(val_obj)
                    break


class ProjectValue(Value):
    def __init__(self, value):
        super(ProjectValue, self).__init__(value)
        self._project_name = ''
        self._major_version = ''
        self._minor_version = ''
        self._exports = []
        self._prefix = ''

    def key(self):
        return self._project_name, self._major_version, self._minor_version, self._exports, self._prefix

    @property
    def project_name(self):
        return self._project_name

    @project_name.setter
    def project_name(self, name):
        self._project_name = name
        self._update()

    @property
    def major_version(self):
        return self._major_version

    @major_version.setter
    def major_version(self, version):
        self._major_version = version
        self._update()

    @property
    def minor_version(self):
        return self._minor_version

    @minor_version.setter
    def minor_version(self, version):
        self._minor_version = version
        self._update()

    @property
    def exports(self):
        return self._exports

    @exports.setter
    def exports(self, sym_file):
        self._exports = sym_file
        self._update()

    @property
    def prefix(self):
        return self._prefix

    @prefix.setter
    def prefix(self, pref):
        self._prefix = pref
        self._update()

    def _update(self):
        value = []
        if self._project_name:
            value.append(self._project_name)
        if self._major_version:
            value.append(self._major_version)
        if self._minor_version:
            value.append(self._minor_version)
        if self._exports:
            value.append('EXPORTS')
            for exp in self._exports:
                value.append(exp)
        if self._prefix:
            value.append('PREFIX')
            value.append(self._prefix)

        self.name = ' '.join(value)


class Owner(Macro):
    def __init__(self):
        super(Owner, self).__init__('OWNER')

    def parse_tokens(self, tokens):
        super(Owner, self).parse_tokens(tokens)
        self.sort_values()

    def sort_values(self):
        logins = set()
        groups = set()
        for node in self.children:
            if node.name.startswith('g:'):
                groups.add(node)
            else:
                logins.add(node)

        self.children = sorted(logins) + sorted(groups)


class Peerdir(Macro):
    def __init__(self):
        super(Peerdir, self).__init__('PEERDIR')

    def __getitem__(self, project):
        nodes = self.find_peerdirs(project)
        if len(nodes) != 1:
            raise KeyError()

        return nodes[0]

    def add(self, project):
        if not self.find_peerdirs(project):
            new_peerdir = PeerdirValue(project)
            self.append_child(new_peerdir)
            self.sort_values()
            return new_peerdir
        else:
            return None

    def add_peerdir(self, node):
        if not self.find_peerdirs(node.name):
            self.append_child(node)
            self.sort_values()
            return node
        else:
            return None

    def remove(self, project):
        peerdirs = self.find_peerdirs(project)
        for peer in peerdirs:
            self.remove_child(peer)

    def remove_peerdir(self, node):
        nodes = self.find_peerdirs(node.name)
        for peer_node in nodes:
            self.remove_child(peer_node)

    def find_peerdirs(self, name):
        nodes = []
        for node in self.children:
            if isinstance(node, PeerdirValue) and node.path == name:
                nodes.append(node)

        return nodes

    def parse_tokens(self, tokens):
        from .com_handler import CommentsHandler

        com_h = CommentsHandler(self, 1)

        START = 1
        MOD = 2
        ADDINCL = 3
        VALUE = 4
        END = 5

        state = START
        comments = []
        for token in tokens:
            while True:
                if state == START:
                    if token.value == mk_common.V_END:
                        break

                    if token.comment and not token.value:
                        com_h.set_comment(token.comment)
                        com_h.add_new_line(inside=True)
                        break
                    elif token.comment:
                        comments.append(token.comment)

                    if token.value:
                        val_obj = PeerdirValue('')
                        state = MOD
                elif state == MOD:
                    if token.comment and token.comment not in comments:
                        comments.append(token.comment)
                    if not token.value:
                        break
                    if token.value == 'ADDINCL':
                        state = ADDINCL
                    else:
                        state = VALUE
                elif state == ADDINCL:
                    val_obj.modifier = 'ADDINCL'
                    state = MOD
                    break
                elif state == VALUE:
                    val_obj.path = token.value
                    state = END
                elif state == END:
                    self.children.append(val_obj)
                    if comments:
                        com_h.set_element(val_obj)
                        com_h.set_comment(''.join(comments))
                        com_h.add_new_line()
                        comments = []
                    state = START
                    break

    def _write(self):
        if self.is_empty():
            return []
        else:
            return super(Peerdir, self)._write()


class PeerdirValue(Value):
    def __init__(self, name):
        super(PeerdirValue, self).__init__(name)
        self._path = name
        self._modifier = ''

    def _update(self):
        if self._modifier:
            self.name = '{mod} {name}'.format(mod=self._modifier, name=self._path)
        else:
            self.name = self._path

    @property
    def path(self):
        return self._path

    @path.setter
    def path(self, name):
        self._path = name
        self._update()

    @property
    def modifier(self):
        return self._modifier

    @modifier.setter
    def modifier(self, mod):
        self._modifier = mod
        self._update()

    def key(self):
        return self.path, self.modifier


class _GenericSrcs(Macro):
    def __init__(self, name):
        super(_GenericSrcs, self).__init__(name)

    def __getitem__(self, project):
        nodes = self.find_values(project)
        if len(nodes) != 1:
            raise KeyError()

        return nodes[0]

    def add(self, source_path):
        if not self.find_values(source_path):
            src = SrcValue(source_path)
            self.append_child(src)
            self.sort_values()
            return src
        else:
            return None

    def add_src(self, node):
        if not self.find_values(node.name):
            self.append_child(node)
            self.sort_values()
            return node
        else:
            return None

    def remove(self, source_path):
        nodes = self.find_values(source_path)
        for node in nodes:
            self.remove_child(node)

    def remove_src(self, node):
        nodes = self.find_values(node.name)
        for src_node in nodes:
            self.remove_child(src_node)

    def find_srcs(self, path=None):
        nodes = []
        for node in self.children:
            if isinstance(node, SrcValue) and (path is None or node.filepath == path):
                nodes.append(node)

        return nodes

    def parse_tokens(self, tokens):
        from .com_handler import CommentsHandler

        com_h = CommentsHandler(self, 1)
        START = 1
        MOD = 2
        GLOBAL = 3
        OBJECT_DEPENDS = 4
        DEPENDS = 5
        VALUE = 6
        END = 7
        MODIFIER = 8
        MODIFIER_VALUE = 9

        modifier_map = {
            "MAIN": PyMainValue,
            "NAMESPACE": PyNamespaceValue,
        }

        state = START
        comments = []
        for token in tokens:
            while True:
                if state == START:
                    if token.value == mk_common.V_END:
                        break

                    if token.comment and not token.value:
                        com_h.set_comment(token.comment)
                        com_h.add_new_line(inside=True)
                        break
                    elif token.comment:
                        comments.append(token.comment)

                    if token.value:
                        state = MODIFIER
                elif state == MODIFIER:
                    if token.value in modifier_map:
                        state = MODIFIER_VALUE
                        val_obj = modifier_map[token.value]()
                        break
                    else:
                        val_obj = SrcValue('')
                        state = GLOBAL
                elif state == MODIFIER_VALUE:
                    if token.comment and token.comment not in comments:
                        comments.append(token.comment)
                    if not token.value:
                        break
                    val_obj.value = token.value
                    state = END
                    break
                elif state == GLOBAL:
                    if token.value == 'GLOBAL':
                        val_obj.global_modifier = token.value
                        state = VALUE
                        break
                    else:
                        state = VALUE
                elif state == VALUE:
                    if token.comment and token.comment not in comments:
                        comments.append(token.comment)
                    if not token.value:
                        break
                    val_obj.filepath = token.value
                    state = MOD
                    break
                elif state == MOD:
                    if token.value == 'DEPENDS':
                        if token.comment and token.comment not in comments:
                            comments.append(token.comment)
                        state = DEPENDS
                        break
                    elif token.value == 'OBJECT_DEPENDS':
                        if token.comment and token.comment not in comments:
                            comments.append(token.comment)
                        state = OBJECT_DEPENDS
                        break
                    else:
                        state = END
                elif state == DEPENDS:
                    if token.comment and token.comment not in comments:
                        comments.append(token.comment)
                    if not token.value:
                        break
                    val_obj.depends.append(token.value)
                    state = MOD
                    break
                elif state == OBJECT_DEPENDS:
                    if token.comment and token.comment not in comments:
                        comments.append(token.comment)
                    if not token.value:
                        break
                    val_obj.object_depends.append(token.value)
                    state = MOD
                    break
                elif END:
                    if val_obj.node_type == TYPE_MACRO_MODIFIER:
                        self.set_modifier(val_obj)
                    else:
                        val_obj._set_name()
                        self.children.append(val_obj)
                    state = START
                    if comments:
                        com_h.set_comment(''.join(comments))
                        com_h.set_element(val_obj)
                        com_h.add_new_line()
                        comments = []


class ResourceFiles(_GenericSrcs):
    NAME = 'RESOURCE_FILES'

    def __init__(self):
        super(ResourceFiles, self).__init__(self.NAME)
        self.prefix = None
        self.set_modifier(ResourcePrefixValue())

    def set_prefix(self, prefix):
        prefix_node = ResourcePrefixValue()
        prefix_node.value = prefix
        self.set_modifier(prefix_node)

    def parse_tokens(self, tokens):
        from .com_handler import CommentsHandler

        com_h = CommentsHandler(self, 1)
        START = 1
        PREFIX = 2
        PREFIX_VALUE = 3
        VALUE = 4
        END = 5

        state = START
        comments = []
        for token in tokens:
            while True:
                if state == START:
                    if token.value == mk_common.V_END:
                        break

                    if token.comment and not token.value:
                        com_h.set_comment(token.comment)
                        com_h.add_new_line(inside=True)
                        break
                    elif token.comment:
                        comments.append(token.comment)

                    if token.value:
                        state = PREFIX
                elif state == PREFIX:
                    if token.value == 'PREFIX':
                        state = PREFIX_VALUE
                        val_obj = ResourcePrefixValue()
                        break
                    else:
                        val_obj = SrcValue('')
                        state = VALUE
                elif state == PREFIX_VALUE:
                    if token.comment and token.comment not in comments:
                        comments.append(token.comment)
                    if not token.value:
                        break
                    val_obj.value = token.value
                    state = END
                elif state == VALUE:
                    if token.comment and token.comment not in comments:
                        comments.append(token.comment)
                    if not token.value:
                        break
                    val_obj.filepath = token.value
                    state = END
                elif state == END:
                    if val_obj.node_type == TYPE_MACRO_MODIFIER:
                        self.set_modifier(val_obj)
                    else:
                        val_obj._set_name()
                        self.children.append(val_obj)
                    state = START
                    if comments:
                        com_h.set_comment(''.join(comments))
                        com_h.set_element(val_obj)
                        com_h.add_new_line()
                        comments = []
                    break


class ResourcePrefixValue(Node):
    NAME = 'PREFIX'

    def __init__(self):
        super(ResourcePrefixValue, self).__init__(self.NAME, TYPE_MACRO_MODIFIER)
        self._prefix = ''

    @property
    def value(self):
        return self._prefix

    @value.setter
    def value(self, namespace):
        self._prefix = namespace

    def _write(self):
        return ['{} {}'.format(self.NAME, self._prefix)] if self._prefix else []

    def _set_name(self):
        self.name = self._prefix


class Srcs(_GenericSrcs):
    NAME = 'SRCS'

    def __init__(self):
        super(Srcs, self).__init__(self.NAME)


class PySrcs(_GenericSrcs):
    NAME = 'PY_SRCS'

    def __init__(self):
        super(PySrcs, self).__init__(self.NAME)
        self.namespace = None
        self.main = None
        self.set_modifier(PyNamespaceValue())

    def set_namespace(self, ns_name):
        ns_node = PyNamespaceValue()
        ns_node.value = ns_name
        self.set_modifier(ns_node)


class PyNamespaceValue(Node):
    NAME = 'NAMESPACE'

    def __init__(self):
        super(PyNamespaceValue, self).__init__(self.NAME, TYPE_MACRO_MODIFIER)
        self._namespace = ''

    @property
    def value(self):
        return self._namespace

    @value.setter
    def value(self, namespace):
        self._namespace = namespace

    def _write(self):
        return ['{} {}'.format(self.NAME, self._namespace)] if self._namespace else []


class PyMainValue(Node):
    NAME = 'MAIN'

    def __init__(self):
        super(PyMainValue, self).__init__(self.NAME, TYPE_MACRO_MODIFIER)
        self._modifier_value = ''

    @property
    def value(self):
        return self._modifier_value

    @value.setter
    def value(self, namespace):
        self._modifier_value = namespace

    def _write(self):
        return ['{} {}'.format(self.NAME, self._modifier_value)] if self._modifier_value else []


class JavaSrcs(_GenericSrcs):
    NAME = 'JAVA_SRCS'

    def __init__(self):
        super(JavaSrcs, self).__init__(self.NAME)


class DocsDir(_GenericSrcs):
    NAME = 'DOCS_DIR'

    def __init__(self):
        super(DocsDir, self).__init__(self.NAME)


class SrcValue(Value):
    def __init__(self, value):
        super(SrcValue, self).__init__(value)
        self._filepath = value
        self._global_modifier = ''
        self._object_depends = []
        self._depends = []

    def _set_name(self):
        if self._global_modifier:
            value = '{glob} {filepath}'.format(glob=self.global_modifier, filepath=self.filepath)
        else:
            value = self.filepath

        for path in self.object_depends:
            value += ' OBJECT_DEPENDS {path}'.format(path=path)
        for path in self.depends:
            value += ' DEPENDS {path}'.format(path=path)

        self.name = value

    @property
    def filepath(self):
        return self._filepath

    @filepath.setter
    def filepath(self, path):
        self._filepath = path
        self._set_name()

    @property
    def global_modifier(self):
        return self._global_modifier

    @global_modifier.setter
    def global_modifier(self, mod):
        self._global_modifier = mod
        self._set_name()

    @property
    def object_depends(self):
        return self._object_depends

    @object_depends.setter
    def object_depends(self, val):
        self._object_depends = val
        self._set_name()

    @property
    def depends(self):
        return self._depends

    @depends.setter
    def depends(self, val):
        self._depends = val
        self._set_name()

    def key(self):
        return self.filepath, self.global_modifier, self.object_depends, self.depends


class Set(Macro):
    def __init__(self, name):
        super(Set, self).__init__(name)

    @property
    def var_name(self):
        for node in self.children:
            if node.node_type == TYPE_VALUE:
                return node.name
        return None

    def sort_values(self):
        var = self.children.pop(0)
        super(Set, self).sort_values()
        self.children.insert(0, var)

    def _write_macro(self):
        out = []
        value_output = []
        comments = get_comments_blocks(self.children)
        for comment in comments:
            out.append('{com}'.format(com=comment))

        var_name = None
        for node in self.children:
            if node.node_type == TYPE_VALUE and var_name is None:
                var_name = node._write()
            elif node.node_type in [TYPE_VALUE, TYPE_COMM_ELEMENT]:
                value_output.extend(node._write())

        string = '{name}({var_name}'.format(name=self.name, var_name='\n'.join(var_name or []))
        if not value_output:
            string += ')'
            out.append(string)
        elif len(value_output) == 1 and value_output[0].find('#') == -1:
            string += '{space}{val})'.format(space=' ' if var_name else '', val=value_output[0])
            out.append(string)
        else:
            out.append(string)
            for line in value_output:
                out.append('    ' + line)

            out.append(')')

        return out


class Flags(Macro):
    def __init__(self, name):
        super(Flags, self).__init__(name)

    def parse_tokens(self, tokens):
        from .com_handler import CommentsHandler

        com_h = CommentsHandler(self, 1)
        START = 1
        VALUE = 2
        GLOBAL = 3
        END = 4

        state = START
        comments = []
        for token in tokens:
            while True:
                if state == START:
                    if token.value == mk_common.V_END:
                        break

                    if token.comment and not token.value:
                        com_h.set_comment(token.comment)
                        com_h.add_new_line(inside=True)
                        break
                    elif token.comment:
                        comments.append(token.comment)

                    if token.value:
                        val_obj = FlagsValue('')
                        state = GLOBAL
                elif state == GLOBAL:
                    if token.value == 'GLOBAL':
                        val_obj.modifier = token.value
                        state = VALUE
                        break
                    else:
                        state = VALUE
                elif state == VALUE:
                    if token.comment and token.comment not in comments:
                        comments.append(token.comment)
                    if not token.value:
                        break

                    val_obj.value = token.value
                    state = END
                elif state == END:
                    self.children.append(val_obj)
                    state = START
                    if comments:
                        com_h.set_comment(''.join(comments))
                        com_h.set_element(val_obj)
                        com_h.add_new_line()
                        comments = []
                    break


class FlagsValue(Value):
    def __init__(self, value):
        super(FlagsValue, self).__init__(value)
        self._modifier = ''
        self._value = value

    @property
    def modifier(self):
        return self._modifier

    @modifier.setter
    def modifier(self, mod):
        self._modifier = mod
        self.name = ' '.join([self._modifier, self._value])

    @property
    def value(self):
        return self._value

    @value.setter
    def value(self, value):
        self._value = value
        if self.modifier:
            self.name = ' '.join([self._modifier, self._value])
        else:
            self.name = self._value


class Addincl(Macro):
    def __init__(self):
        super(Addincl, self).__init__('ADDINCL')

    def parse_tokens(self, tokens):
        from .com_handler import CommentsHandler

        com_h = CommentsHandler(self, 1)
        START = 1
        VALUE = 2
        GLOBAL = 3
        END = 4

        state = START
        comments = []
        for token in tokens:
            while True:
                if state == START:
                    if token.value == mk_common.V_END:
                        break

                    if token.comment and not token.value:
                        com_h.set_comment(token.comment)
                        com_h.add_new_line(inside=True)
                        break
                    elif token.comment:
                        comments.append(token.comment)

                    if token.value:
                        val_obj = AddinclValue('')
                        state = GLOBAL
                elif state == GLOBAL:
                    if token.value == 'GLOBAL':
                        val_obj.modifier = token.value
                        state = VALUE
                        break
                    else:
                        state = VALUE
                elif state == VALUE:
                    if token.comment and token.comment not in comments:
                        comments.append(token.comment)
                    if not token.value:
                        break

                    val_obj.value = token.value
                    state = END
                elif state == END:
                    self.children.append(val_obj)
                    state = START
                    if comments:
                        com_h.set_comment(''.join(comments))
                        com_h.set_element(val_obj)
                        com_h.add_new_line()
                        comments = []
                    break


class AddinclValue(Value):
    def __init__(self, value):
        super(AddinclValue, self).__init__(value)
        self._modifier = ''
        self._value = value

    @property
    def modifier(self):
        return self._modifier

    @modifier.setter
    def modifier(self, mod):
        self._modifier = mod
        self.name = ' '.join([self._modifier, self._value])

    @property
    def value(self):
        return self._value

    @value.setter
    def value(self, value):
        self._value = value
        if self._modifier:
            self.name = ' '.join([self._modifier, self._value])
        else:
            self.name = self._value


class Script(Macro):
    def __init__(self, name):
        super(Script, self).__init__(name)

    def _write_macro(self):
        out = []
        value_output = []
        comments = get_comments_blocks(self.children)
        for comment in comments:
            out.append('{com}'.format(com=comment))

        for node in self.children:
            if node.node_type in [TYPE_VALUE, TYPE_COMM_ELEMENT]:
                value_output.extend(node._write())

        string = '{name}('.format(name=self.name)
        if not value_output:
            string += ')'
            out.append(string)
            return out

        out.append(string)

        START = 1
        SCRIPT_PATH = 2
        SCRIPT_ARGS = 3
        SCRIPT_ARGS_MOD = 31
        MOD = 4
        CWD = 5
        TOOL = 6
        IN = 7
        OUTPUT_INCLUDES = 9
        OUT = 10
        STDOUT = 11
        ENV = 12
        END = 13

        state = START
        states = ['CWD', 'TOOL', 'IN', 'OUTPUT_INCLUDES', 'OUT', 'OUT_NOAUTO', 'STDOUT', 'STDOUT_NOAUTO']
        arg_mode = ''
        for line in value_output:
            while True:
                if state == START:
                    state = SCRIPT_PATH
                elif state == SCRIPT_PATH:
                    state = SCRIPT_ARGS
                    out.append('    ' + line)
                    break
                elif state == SCRIPT_ARGS:
                    if line == '-' and len(line) < 3:
                        state = SCRIPT_ARGS_MOD
                    elif line in states:
                        state = MOD
                    else:
                        string = ' ' + arg_mode + line
                        if len(out[-1]) + len(string) > MAX_OUTPUT_STRING_LEN:
                            out.append('       ')
                        out[-1] += string
                        arg_mode = ''
                        break
                elif state == SCRIPT_ARGS_MOD:
                    arg_mode = line + ' '
                    state = SCRIPT_ARGS
                    break
                elif state == MOD:
                    if line == 'CWD':
                        state = CWD
                    elif line == 'TOOL':
                        state = TOOL
                    elif line == 'IN':
                        state = IN
                    elif line == 'OUTPUT_INCLUDES':
                        state = OUTPUT_INCLUDES
                    elif line in ['OUT', 'OUT_NOAUTO']:
                        state = OUT
                    elif line in ['STDOUT', 'STDOUT_NOAUTO']:
                        state = STDOUT
                    elif line.startswith('#'):
                        pass
                    elif line == 'ENV':
                        state = ENV
                        pass
                    else:
                        state = END
                        break
                    out.append('    ' + line)
                    break
                elif state == CWD:
                    out[-1] += ' ' + line
                    state = MOD
                    break
                elif state == TOOL:
                    if line in states:
                        state = MOD
                    else:
                        string = ' ' + line
                        if len(out[-1]) + len(string) > MAX_OUTPUT_STRING_LEN:
                            out.append('       ')
                        out[-1] += string
                        break
                elif state == IN:
                    if line in states:
                        state = MOD
                    else:
                        string = ' ' + line
                        if len(out[-1]) + len(string) > MAX_OUTPUT_STRING_LEN:
                            out.append('       ')
                        out[-1] += string
                        break
                elif state == OUTPUT_INCLUDES:
                    if line in states:
                        state = MOD
                    else:
                        string = ' ' + line
                        if len(out[-1]) + len(string) > MAX_OUTPUT_STRING_LEN:
                            out.append('       ')
                        out[-1] += string
                        break
                elif state == OUT:
                    if line in states:
                        state = MOD
                    else:
                        string = ' ' + line
                        if len(out[-1]) + len(string) > MAX_OUTPUT_STRING_LEN:
                            out.append('       ')
                        out[-1] += string
                        break
                elif state == STDOUT:
                    out[-1] += ' ' + line
                    state = MOD
                    break
                elif state == ENV:
                    out[-1] += ' ' + line
                    state = MOD
                    break
                elif state == END:
                    raise Exception("something gone wrong with serialization")

        out.append(')')

        return out


class Archive(Macro):
    def __init__(self, name):
        super(Archive, self).__init__(name)

    def _write_macro(self):
        out = []
        value_output = []
        comments = get_comments_blocks(self.children)
        for comment in comments:
            out.append('{com}'.format(com=comment))

        for node in self.children:
            if node.node_type in [TYPE_VALUE, TYPE_COMM_ELEMENT]:
                value_output.extend(node._write())

        string = '{name}('.format(name=self.name)
        if not value_output:
            string += ')'
            out.append(string)
            return out

        out.append(string)

        for line in value_output:
            if out[-1] == '    NAME':
                out[-1] += ' ' + line
            else:
                out.append('    ' + line)

        out.append(')')

        return out


class Resource(Macro):
    def __init__(self):
        super(Resource, self).__init__('RESOURCE')

    def _write_macro(self):
        out = []
        value_output = []
        comments = get_comments_blocks(self.children)
        for comment in comments:
            out.append('{com}'.format(com=comment))

        for node in self.children:
            if node.node_type in [TYPE_VALUE, TYPE_COMM_ELEMENT]:
                value_output.extend(node._write())

        string = '{name}('.format(name=self.name)
        if not value_output:
            string += ')'
            out.append(string)
            return out

        out.append(string)

        value = []
        for line in value_output:
            if line.startswith('#'):
                out.append('    ' + line)
            else:
                value.append(line)
                if len(value) == 2:
                    out.append('    ' + ' '.join(value))
                    value = []

        out.append(')')

        return out


class AddTestValue(Value):
    def __init__(
        self,
        type,
        paths,
        data=None,
        depends=None,
        timeout=None,
        fork_tests=False,
        fork_subtests=False,
        split_factor=None,
        test_size=None,
        tags=None,
    ):
        super(AddTestValue, self).__init__('')
        self.type = type
        self.rel_paths = paths or []
        self.data = data or []
        self.depends = depends or []
        self.timeout = timeout
        self.fork_tests = fork_tests
        self.fork_subtests = fork_subtests
        self.split_factor = split_factor
        self.test_size = test_size
        self.tags = tags or []
        self._make_value()

    def _write(self):
        self._make_value()
        return super(AddTestValue, self)._write()

    def _make_value(self):
        value = [self.type]
        if self.rel_paths:
            value += self.rel_paths
        if self.tags:
            value += ['TAG'] + self.tags
        if self.data:
            value += ['DATA'] + self.data
        if self.depends:
            value += ['DEPENDS'] + self.depends
        if self.timeout:
            value += ['TIMEOUT', self.timeout]
        if self.fork_tests:
            value += ['FORK_TESTS']
        if self.fork_subtests:
            value += ['FORK_SUBTESTS']
        if self.split_factor:
            value += ['SPLIT_FACTOR', self.split_factor]
        if self.test_size:
            value += ['SIZE', self.test_size]

        self.name = ' '.join(value)


class StringMacro(Macro):
    def __init__(self, name):
        super(StringMacro, self).__init__(name)

    def parse_tokens(self, tokens):
        if tokens:
            self.children.append(
                StringMacroValue(' '.join([token.value for token in tokens if token.value != mk_common.V_END]))
            )


class StringMacroValue(Value):
    def __init__(self, value):
        super(StringMacroValue, self).__init__(value)


class FromSandbox(Macro):
    def __init__(self):
        super(FromSandbox, self).__init__('FROM_SANDBOX')

    def parse_tokens(self, tokens):
        START = 1
        RESOURCE = 2
        MODE = 3
        FILENAME = 4
        PREFIX = 6
        RENAME = 7
        AUTOUPDATED = 8
        END = 5

        state = START
        for token in tokens:
            while True:
                if state == START:
                    if token.value == mk_common.V_END:
                        break

                    if not token.value:
                        break

                    if token.value:
                        value_obj = FromSandboxValue('', [])
                        state = MODE
                elif state == MODE:
                    if token.value == 'FILE':
                        value_obj.is_archive = False
                        break
                    elif token.value == 'PREFIX':
                        state = PREFIX
                        break
                    elif token.value == 'RENAME':
                        state = RENAME
                        break
                    elif token.value in ['OUT', 'OUT_NOAUTO']:
                        value_obj._out_token = token.value
                        state = FILENAME
                        break
                    elif token.value == 'AUTOUPDATED':
                        state = AUTOUPDATED
                        break
                    else:
                        state = RESOURCE
                elif state == RESOURCE:
                    if token.value == mk_common.V_END:
                        state = END
                        continue
                    value_obj.resource_id = token.value
                    state = MODE
                    break
                elif state == PREFIX:
                    value_obj.prefix = token.value
                    state = MODE
                    break
                elif state == AUTOUPDATED:
                    value_obj.autoupdated = token.value
                    state = MODE
                    break
                elif state == RENAME:
                    if token.value in ['OUT', 'OUT_NOAUTO']:
                        value_obj._out_token = token.value
                        state = FILENAME
                        break
                    if token.value == mk_common.V_END:
                        state = END
                        continue
                    if token.value:
                        value_obj.rename_files.append(token.value)
                    break
                elif state == FILENAME:
                    if token.value == mk_common.V_END:
                        state = END
                        continue
                    if token.value:
                        value_obj.filenames.append(token.value)
                    break
                elif state == END:
                    value_obj._make_value()
                    self.children.append(value_obj)
                    state = START

    def _write(self):
        values = list([x for x in self.children if isinstance(x, FromSandboxValue)])

        if len(values) == 1 and len(values[0].filenames) == 1:
            return super(FromSandbox, self)._write()

        out = ['FROM_SANDBOX(']
        for value in values:
            output = (
                ('FILE ' if not value.is_archive else '')
                + str(value.resource_id)
                + (' AUTOUPDATED {}'.format(value.autoupdated) if value.autoupdated else '')
                + (' PREFIX {}'.format(value.prefix) if value.prefix else '')
                + (' RENAME {}'.format(' '.join(value.rename_files)) if value.rename_files else '')
                + ' '
                + str(value._out_token)
            )
            if len(value.filenames) == 1:
                output = ' '.join([output] + value.filenames)
                out.append('    {}'.format(output))
            else:
                out.append('    {}'.format(output))
                for filename in value.filenames:
                    out.append('        {}'.format(filename))

        out.append(')')
        return out


class FromSandboxValue(Value):
    def __init__(self, resource_id, filenames):
        super(FromSandboxValue, self).__init__('')
        self.resource_id = resource_id
        self.filenames = filenames or []
        self.is_archive = True
        self.prefix = None
        self.autoupdated = None
        self.rename_files = []
        self._out_token = ''
        self._make_value()

    def _write(self):
        self._make_value()
        return super(FromSandboxValue, self)._write()

    def _make_value(self):
        parts = []

        if not self.is_archive:
            parts.append('FILE')

        parts.append(str(self.resource_id))

        if self.autoupdated:
            parts += ['AUTOUPDATED', self.autoupdated]

        if self.prefix:
            parts += ['PREFIX', self.prefix]

        if self.rename_files:
            parts.append('RENAME')
            parts.extend(self.rename_files)

        if self._out_token:
            parts.append(self._out_token)
        parts.extend(self.filenames)
        self.name = ' '.join(parts)


class LicenseMacro(Macro):
    def __init__(self):
        super(LicenseMacro, self).__init__('LICENSE')

    def parse_tokens(self, tokens):
        if not tokens:
            return

        AND_TOKEN = 'AND'
        OR_TOKEN = 'OR'
        WITH_TOKEN = 'WITH'

        keywords = (AND_TOKEN, OR_TOKEN, WITH_TOKEN)

        latest = []
        already_with = False
        already_quote = False
        for i, token in enumerate(tokens):
            if token.value == mk_common.V_END:
                break
            if token.value == '"':
                if not already_quote:
                    already_quote = True
                    latest.append(token)
                    continue
            if token.value.startswith('"'):
                already_quote = True
            if already_quote:
                ends = token.value.replace('\\\\', '').replace('\\"', '')
                if ends.endswith('"') and not ends.endswith('\\"'):
                    already_quote = False
                else:
                    latest.append(token)
                    continue

            if already_with:
                if token.value in keywords:
                    raise MacroException('Unexpected {} token at position {}'.format(token.value, i))
                already_with = False
                self.children[-1].name = self.children[-1].name + ' {} {}'.format(WITH_TOKEN, token.value)
                continue
            if token.value == WITH_TOKEN:
                already_with = True
                continue

            assert not already_quote
            assert not already_with

            if token.value == AND_TOKEN:
                pass
            elif token.value == OR_TOKEN:
                self.children[-1] = LicenseOrMacroValue(self.children[-1].name, self.children[-1].comment)
            else:
                latest.append(token)

            if latest:
                candidate_value = ' '.join([i.value for i in latest if i.value])
                candidate_comment = ' '.join([i.comment for i in latest if i.comment])
                if candidate_value.startswith('(') and candidate_value.endswith(')'):
                    candidate_value = '"' + candidate_value + '"'
                if candidate_comment:
                    candidate_value += candidate_comment
                self.children.append(LicenseAndMacroValue(candidate_value, candidate_comment))
                latest = []

        else:
            raise MacroException("Something wrong: can't find END token")

    def parse_strings(self, strings):
        from . import mk_builder

        tokens = [mk_builder.Token(v, '') for v in strings] + [mk_builder.Token(mk_common.V_END, '')]
        self.parse_tokens(tokens)

    def add_values(self, *values):
        from . import mk_builder

        values_to_add = []
        for v in values:
            if self.find_values(v):
                if values_to_add and values_to_add[-1] in ('AND', 'OR'):
                    values_to_add = values_to_add[:-1]
            else:
                values_to_add.append(v)
        tokens = [mk_builder.Token(v, '') for v in values_to_add] + [mk_builder.Token(mk_common.V_END, '')]
        self.parse_tokens(tokens)

    def add_value(self, value_name):
        self.add_values(value_name)

    def contains_or(self):
        for child in self.children:
            if isinstance(child, LicenseOrMacroValue):
                return True
        return False

    def get_licenses(self):
        return list(item.key() for item in self.children)

    def _write(self):
        values = list(self.children)

        if len(values) == 1:
            return super(LicenseMacro, self)._write()

        out = ['LICENSE(']
        latest_non_comment = -1
        for i, value in enumerate(values):
            if not value.comment or value.comment != value.key():
                latest_non_comment = i

        for i, value in enumerate(values):
            if value.comment and value.comment == value.key():
                out.append('    {}'.format(value.key()))
            elif value.comment.strip():
                out.append(
                    '    {}{} {}'.format(
                        value.key()[: -len(value.comment)],
                        '' if i >= latest_non_comment else value.suffix(),
                        value.key()[-len(value.comment) :],
                    )
                )
            else:
                out.append('    {}{}'.format(value.key(), '' if i == len(values) - 1 else value.suffix()))

        out.append(')')
        return out


class LicenseAndMacroValue(Value):
    def __init__(self, value, comment=None):
        self.comment = comment
        super(LicenseAndMacroValue, self).__init__(value)

    @staticmethod
    def suffix():
        return ' AND'


class LicenseOrMacroValue(Value):
    def __init__(self, value, comment=None):
        self.comment = comment
        super(LicenseOrMacroValue, self).__init__(value)

    @staticmethod
    def suffix():
        return ' OR'


def get_comments_blocks(nodes):
    comments = []
    for el in nodes:
        if el.node_type == TYPE_COMMENT:
            comments.extend(el.name.split('\n')[:-1])

    return comments
