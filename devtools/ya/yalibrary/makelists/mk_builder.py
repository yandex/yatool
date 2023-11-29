from . import mk_common
from . import macro_definitions
from . import com_handler


class Token(object):
    def __init__(self, value='', comment=''):
        self.value = value
        self.comment = comment

    def __str__(self):
        return '"{value}" "{comment}"'.format(value=self.value, comment=self.comment)


class Builder(object):
    def __init__(self):
        self.root = macro_definitions.MakeList('root')
        self.current = self.root
        self.com_handle = com_handler.CommentsHandler(self.root)
        self.macros_stack = []
        self.scope = {}
        self.scope_stack = []
        self.value_tokens = []
        self.if_value_comment = False
        self.current_macro_name = ''

    def on_macro_start(self, name):
        self.current_macro_name = name

    def on_macro_end(self):
        if self.value_tokens:
            self.value_tokens.append(Token(mk_common.V_END))

        if self.current_macro_name in self.scope:
            node = self.scope[self.current_macro_name]
            node.parse_tokens(self.value_tokens)
            self.com_handle.set_element(node)
            self.value_tokens = []
            return
        else:
            node = macro_definitions.Macro.create_node(self.current_macro_name)
            node.parse_tokens(self.value_tokens)
            if node.name in mk_common.SAFE_JOIN_MACROS:
                self.scope[node.name] = node

        if node.name in ['ELSE', 'ELSEIF', 'ENDIF']:
            self.scope = {}
            if self.current.name != 'IF' and self.macros_stack:
                self.current = self.macros_stack.pop()
            elif not self.macros_stack:
                self.current = self.root
        self.current.children.append(node)
        self.com_handle.set_element(node)

        if node.name in mk_common.PROJECT_MACROS or node.name in ['IF', 'ELSE', 'ELSEIF']:
            self.macros_stack.append(self.current)
            self.current = node
            if node.name not in ['ELSE', 'ELSEIF']:
                self.scope_stack.append(self.scope)
                self.scope = {}
        elif node.name in ['END', 'ENDIF']:
            if self.macros_stack:
                self.current = self.macros_stack.pop()
            if self.scope_stack:
                self.scope = self.scope_stack.pop()
            else:
                self.scope = {}

        self.value_tokens = []

    def on_value(self, value):
        token = Token(value)
        self.value_tokens.append(token)
        self.if_value_comment = True

    def on_comment(self, comment):
        self.com_handle.set_comment(comment)

    def on_value_comment(self, comment):
        if self.if_value_comment:
            self.value_tokens[-1].comment = comment
            self.if_value_comment = False
        else:
            token = Token('', comment)
            self.value_tokens.append(token)

    def on_value_new_line(self):
        self.if_value_comment = False

    def on_new_line(self):
        self.com_handle.add_new_line()
        self.if_value_comment = False

    def on_global_end(self):
        self.com_handle.end()
