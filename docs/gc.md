# ya gc cache

The `ya gc cache` command serves to clear temporary files created when using the `ya make` build system.

The build process produces various intermediate and auxiliary files, and the resulting build files are temporarily cached on the disk.

Over time, these files can take up a significant amount of disk space, and deleting them manually becomes an inconvenient and risky process.  The `ya gc cache` command automates the cleanup process by deleting outdated or unnecessary files in line with configuration settings.

After a certain period, most files are supposed to either be moved to permanent storage or be deleted. This helps optimize disk space usage.

## Syntax

`ya gc cache [OPTIONS]`

`[OPTIONS]` allows the user to specify additional parameters to fine-tune the cleanup process. But in the basic use case, no additional parameters are needed.

### Options
```
Options:
  Ya operation control
    -h, --help          Print help. Use -hh for additional options, and -hhh for even more options.
    Expert options
    -B=CUSTOM_BUILD_DIRECTORY, --build-dir=CUSTOM_BUILD_DIRECTORY
                        Custom build directory (autodetected by default)
  Local cache
    --cache-stat        Show cache statistics
    --gc                Remove all cache except uids from the current graph
    --gc-symlinks       Remove all symlink results except files from the current graph
    Advanced options
    --tools-cache-size=TOOLS_CACHE_SIZE
                        Max tool cache size (default: 30.0GiB)
    --symlinks-ttl=SYMLINKS_TTL
                        Results cache TTL (default: 0.0h)
    --cache-size=CACHE_SIZE
                        Max cache size (default: 140.04687070846558GiB)
    --cache-codec=CACHE_CODEC
                        Cache codec (default: )
    --auto-clean=AUTO_CLEAN_RESULTS_CACHE
                        Auto clean results cache (default: True)
```

## Auto-clearing

You can specify the cache clearing configuration locally using the configuration file `~/.ya/ya.conf`.

### Basic functions:

1. Full cleanup of auxiliary files — Deleting all files that are stored in the tool cache, as well as other build data that is normally deleted automatically.
2. Deleting temporary files — This excludes files that are links to files used in other builds.
3. Filtered clearing of local build cache — Allows you to fine-tune the data deletion process using parameters.

#### Auxiliary build files
Auxiliary build files include compilers, SDKs, emulators, and other tools that are not directly included in the build process, but on which `ya` relies for building, running tests, and more. Such files are stored in the tool cache.

Automatic deletion is configured in Linux and MacOS.

These files are deleted in line with the LRU policy as soon as the cache reaches its capacity limit. You can override the default limit using the `tools_cache_size` parameter.

Note that tools get deleted only after all processes that use the tool (compiler, emulator, or other) complete, which is why in-the-moment space usage may exceed the limit.

If you run tools using direct paths obtained with, for example, `ya tool cc --print-path`, then such tools won't be cleared automatically as we have no control over this kind of scenario. These tools will be automatically cleared in the usual order after running `ya gc cache`.

#### Temporary build files

Temporary build files include:
1. Temporary files created using functions that are derived from `mktemp/mkdtemp` and so on. These files are created in a temporary directory, which is fully deleted once `ya` is done.
2. Temporary files that can be used in other builds. These include intermediate build files that are explicitly described in the build via `ya.make` files.
   You can limit the total disk space allocated for such files using `cache_size`. This category also includes files loaded from a distributed cache.
3. Temporary files used in the current build, or build directories. In most cases, these files are links to files used in other builds. There are several reasons why such files are put into a separate category. Firstly, some intermediate files are explicitly restricted from being used in other builds; secondly, the build mode with the `--rebuild` key uses only files from the current build; and thirdly, the cache size limit may be too small for the current build.
   By default, files in this category are deleted either during the current build or during the next build.

You can't limit in-the-moment disk space usage with the files from the first and third category in advance.

The files from the second category are stored in the local build cache.

### Basic settings for automatic file clearing:
1. `cache_size`: [Local cache](#temporary-build-files) size limit.
2. `tools_cache_size`: [Tool cache](#auxiliary-build-files) size limit.
3. `symlinks_ttl`: The limit of the lifetime of [build results](cache.md) cached by `ya`.  The lifetime is counted from the time of creation.

You can disable automatic deletion by setting large values for these parameters, and the deleting of build directories is disabled using the build system's `--keep-temps` option.

#### Sample ya.conf settings
```
tools_cache_size = "6GiB"
symlinks_ttl = "1h"
cache_size = "80GiB"
```
In this example, the tool cache limit is set to 6 GiB, the lifetime of links to build results is set to 1 hour, and the local build cache size is set to 80 GiB.

You can specify the size in bytes or time in seconds without quotes (for example, `symlinks_ttl = 3600`).

## Manual cleanup
In addition to automatic cache clearing, you can explicitly run disk cleanup using the `ya gc cache` command. It will do the cleanup and also perform additional checks for errors.

The command deletes:
- All auxiliary files (except those that are cleaned automatically).
- All temporary files (except [local cache](#temporary-build-files) files).

### Deletion based on size and "age"
```
- --object-size-limit=OBJECT_SIZE_LIMIT: Deletes build cache objects that exceed the specified size (in MiB).
- --age-limit=AGE_LIMIT: Deletes build cache objects that are older than the specified "age" (in hours).
```
If a filter is not set, then the cleanup will proceed in line with the `cache_size` setting.

### Managing build symlinks
```
- --gc-symlinks: Clears outdated build symlinks in the repository.
```
### Practical application examples
Deleting large cache objects
```
ya gc cache --object-size-limit=100
```
Clears the cache of objects larger than 100 MiB, freeing up a significant amount of disk space.

Clearing "old" cache objects
```
ya gc cache --age-limit=72
```
Initiates the deletion of data that hasn't been used in the last 72 hours, keeping the cache up to date and optimized.
