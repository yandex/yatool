from . import mk_common
from . import macro_definitions


class CommentsHandler(object):
    def __init__(self, root, mode=2):
        self.comments = ''
        self.new_lines = 0
        self.root = root
        self.element = None
        self.comment = ''
        self.parents = [self.root]
        self.mode = mode

    def add_new_line(self, inside=False):
        if self.comment:
            if self.comments and self.new_lines == 1:
                self.comments += self.comment
                self.new_lines -= 1
            else:
                self.comments = self.comment
            self.comment = ''

        if self.element:
            if self.comments:
                self.element.children.append(macro_definitions.Comment(self.comments))
                self.comments = ''

        if self.comments or self.element:
            self.new_lines += 1

        if self.new_lines >= self.mode:
            if self.comments:
                last = self.parents[-1]
                if inside:
                    last.children.append(macro_definitions.CommElement(self.comments))
                elif last.name in mk_common.PROJECT_MACROS or last.name in ['IF', 'ELSE', 'ELSEIF']:
                    last.children.append(macro_definitions.CommInside(self.comments))
                else:
                    last.children.append(macro_definitions.Comment(self.comments))
                self.comments = ''
            self.element = None
            self.new_lines = 0

    def set_comment(self, comment):
        self.comment = comment + '\n'

    def set_element(self, element):
        if element.name in ['ELSE', 'ELSEIF'] and self.parents[-1].name in ['IF', 'ELSE', 'ELSEIF']:
            self.parents[-1] = element

        if element.name in mk_common.PROJECT_MACROS or element.name in ['IF']:
            self.parents.append(element)
        elif element.name in ['END', 'ENDIF']:
            self.parents.pop()

        if element.name == 'IF':
            self.mode = 1
        elif element.name == 'ENDIF':
            self.mode = 2

        self.element = element

    def end(self):
        if len(self.comments) > 0:
            self.parents[-1].children.append(macro_definitions.Comment(self.comments))
            self.comments = ''
