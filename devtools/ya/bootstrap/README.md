# Building ya & ymake from sources

This directory contains files reqiured to build a build system without a build system.

## Dependencies

For successful build it is required to have `docker` and `python3.9+` installed in system.

## Launching bootstrap

Bootstrap is launched with `run_bootstrap` script

It has several flags, as follows:

- `--source-root` -- Path to the repository root. _Required_
- `--result-root` -- Path to the directory, where results should be put. _Required_
- `--all-platforms-build` -- This flag will also launch the second stage of bootstrap.
- `--cleanup` -- Remove all containers and images created by this image.

## How does it work?

Bootstrap can be described in two stages

### Stage one

This stage's execution results in `ya` and `ymake` for linux.

This stage can be launched separately with `stage1.sh`
Call syntax is as follows
```
./stage1.sh <source_root> <build_root> <results_root>
```
Here:
* `source_root` - Path to repository root with `ya` sources;
* `build_root` - Path to the directory, where all build results will be stored;
* `results_root` - Path to the directory, where final results will be stored.

{% note info %}

This script uses system libraries, thus tools built, for example, with `Ubuntu 22.04`, will not work on earlier Ubuntu's

{% endnote %}

This script invokes `graph_executor.py`, which downloads build graph from S3 and executes it, then moves `ya`, `ymake` and `libiconv.so` from `build_root` to `$results_root/stage1`.

It is called by:
```
python3 graph_executor.py <source_root> <build_root> <threads> [<path/to/local/graph>]
```

The last argument is optional and sohuld be used only in debug purposes.

{% note warning %}

Python 3.9 or newer is required for this script.

{% endnote %}

### Stage two

This stage is optional and uses tooling built in the stage one to build all targets, namely:
- `devtools/ya/bin3`
- `devtools/local_cache/toolscache/server`
- `devtools/ya/test/programs/test_tool/bin3`
- `devtools/ymake/bin`
- `devtools/yexport/bin`

This stage is orchestrated by `stage2.sh`.
It has similar to `stage1.sh` invocation syntax, but it builds _all_ targets from this repositories for platforms from `platform_list`

Build results will be stored in `$result_root/stage2/<arch>` for main tools and in `$result_root/stage2/additional/<arch>` for additional ones (now it is `yexport` only)

## Docker images

To ensure platform independency we use docker images built from two `Dockerfile`s.

These images each execute their own stage of bootstrap described earlier
* `stage1.Dockerfile` is a `Ubuntu 22.04`-based image with everything needed for successful graph execution.
* `stage2.Dockerfile` is the basic `Ubuntu 22.04` image.
Its launch allows you to get all the tools distributed in this repository.

### How to build an image

An image is built by default as follows:
```
docker build -f <path/to/dockerfile> . -t ya-bootstrap-stage<StageNo>:latest
```

### How to run an image

These images are run as follows:
```
docker run \
    --name test \
    --mount type=bind,source=<path/to/repo>,target=/source_root,readonly \
    --mount type=bind,source=<path/to/result>,target=/result \
    ya-bootstrap-stage<StageNo>:latest
```

## How graph is distributed

Graph is obtained with `gen_graph/gg` script and uploaded to S3.

This allows to avoid large diffs compared to committing graph straight into a repo

{% note warning %}

This script requires to have `clang-14` and `clang++-14` in user's system.

Also the path to repository root is required in `source_root` variable.

The invocation is as follows:
```
source_root=path/to/repo PATH=path/to/clangs:$PATH ./gen_graph/gg > fname
```

`PATH` setting is required only when compilers are in a custom directories.

{% endnote %}
