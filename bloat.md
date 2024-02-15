# ya tool bloat

`ya tool bloat [OPTIONS]`
Проанализировать состав бинарного кода.

```
cd arc/path/myexecutable
ya make ... -DDUMP_LINKER_MAP
ya tool bloat -i myexecutable -m myexecutable.map.lld
# Зайти в браузере на указанный порт
```

## Опции
```
  {-V|--svnrevision}     print svn version
  {-?|--help}            print usage
  --verbose              enable DEBUG logging
  {-i|--input} PATH      path to input object file
  {-m|--linker-map} PATH path to linker map file
  --load-json PATH       path to json report input file
  --save-json PATH       path to json report output file
  --save-html PATH       path to html report output directory
  --serve                run HTTP server
  {-p|--port} PORT       serve port (default: 8000)
  --strip-prefix REGEX   POSIX regex for source path prefix to be removed
  --no-default-paths     do not guess default arcadia prefixes
  --no-multi-threading   do not use multi-threading
  --no-virtual-sections  exclude virtual sections (without corresponding data in
                         binary, e.g. .bss)
```
