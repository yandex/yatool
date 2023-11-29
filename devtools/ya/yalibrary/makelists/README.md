## Библиотека для манипуляции с ya.make

### Прочитать, изменить и записать ya.make.

```python
from yalibrary.makelists import from_file

PATH_TO_YA_MAKE = '/some/path/to/ya/make/here/ya.make'

make_list = from_file(PATH_TO_YA_MAKE)
make_list.project.srcs.add('foo/bar.cpp')
make_list.project.py_srcs().add('baz/qux.py')
strange_macros_list = make_list.project.find_nodes('SOME_STRANGE_MACRO')
strange_macros_list[0].add_value('asd')
strange_macros_list[0].sort_values()
make_list.write(PATH_TO_YA_MAKE)
```

### Создать ya.make руками

```python
from yalibrary.makelists.macro_definitions import Project, MakeList

make_list = MakeList()
make_list.append_child(Project.create_program())
make_list.write('somewhere')

```
