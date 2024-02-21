# ya ide

Сгенерировать проекты для IDE на основе описания сборки.

`ya ide <subcommand>`

## Доступные команды
```
 clion       Generate stub for CLion
 goland      Generate stub for Goland
 idea        Generate stub for IntelliJ IDEA
 msvc        Alias for msvs
 msvs        Generate Microsoft Visual Studio solution
 pycharm     Generate stub for PyCharm
 qt          Generate QtCreator project
```

## Примеры
```
  ya ide msvs util/generic util/datetime  Generate solution for util/generic, util/datetime and all their dependencies
  ya ide msvs -P Output                   Generate solution in Output directory
  ya ide msvs -T my_solution              Generate solution titled my_solution.sln
  ya ide idea -r ~/djfs disk/djfs         Generate idea project for disk/djsf in ~/djfs directory 
```
