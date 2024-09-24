## ya package

The `ya package` packaging command enables you to create various types of packages described in special JSON files and publish them in different fixed configurations.

### Syntax
The general command format is as follows:

`ya package [OPTION]... [PACKAGE DESCRIPTION FILE NAME(S)]...`

where:
- `[OPTION]` are additional flags or keys that modify the behavior of the selected subcommand.
- `[PACKAGE DESCRIPTION FILE NAME(S)]` are the names of package description files in JSON format (`package.json`).

### How it works

To build a package, run the command specifying the path to the JSON description and the required format. The package will be built in the current directory.
If the packaging format is neither specified in the arguments nor described in the package description file, a `tar` archive is created.
```bash translate=no
ya package devtools/project/package/hello_world.json
```
**Execution result**
```bash translate=no
tar -tvzf ./some-package-hello-world.85c7e374108166bfc1b2a47ca888830965a07708.tar.gz
drwxrwxr-x 0/0 0 2021-03-11 06:58 some_package_dir/
-rwxrwxr-x 0/0 19488 2021-03-11 06:58 some_package_dir/hello_world
```
We get an archive with an executable file inside the `some_package_dir` directory.

**Archive description:**
```bash translate=no
{
    "meta": {
        "name": "some-package-hello-world",
        "maintainer": "Programmers <mail>",
        "description": "hello world test package",
        "version": "{revision}"
    },
    "build": {
        "targets": [
            "devtools/project/hello_world"
        ]
    },
    "data": [
        {
            "source": {
                "type": "BUILD_OUTPUT",
                "path": "devtools/project/hello_world/hello_world"
            },
            "destination": {
                "path": "/some_package_dir/"
            }
        }
    ]
}
```
This description defines:
- Package name:`some-package-hello-world`.
- Package version: the current repository revision determined automatically.
- Package contents: the `devtools/project/hello_world` program to be built for the package will be placed in `some_package_dir`.

By default, `ya package` creates a package in the current directory.
Use the `-O/--package-output` options to change the directory to which `ya package` saves the created packages.

#### package.json contents

`ya package` is an add-on on top of the build system and follows the same principles as `ya make`. It provides a tight and reproducible target build with the minimum required set of command-line parameters.

`package.json` contains information about the:
- Build configuration: target programs, build flags, platforms, and more.
- Dependencies from the repository and resources.
- Ways artifacts are structured within the package, access rights, file and directory names.
- Metadata related to the package and its build.
- Packaging parameters directly related to the package being described.

In most cases, running the `ya package <package-path>` command in the context of the given repository revision should be enough to build the package of a particular version.

The description format uses special sections with parameters:
- `meta`: A metainformation section with the package name, version, description, and other fields.
- `include`: A section with the list of paths to other packages whose contents need to be added.
- `build`: A section with directives to build the contents of the package using the `ya make` command.
- `data`: A section with elements that move or copy files to the desired paths (`source` and `destination`).
- `params`: A section for specifying packaging parameters that can be overridden by the command line when running `ya package`.
- `userdata`: A section for arbitrary user data. It's not used or checked by `ya package`.
- `postprocess`: A section describing actions to be completed after `ya package` has finished performing its main functions.

### Supported package formats

The following formats selected with the command-line key are available for packaging:

- `--tar`: A `tar` archive (default).
- `--debian`: A `deb` package.
- `--rpm`: An `rpm` package.
- `--docker`: A `docker` image.
- `--wheel`: A Python `wheel` package.
- `--aar`: A native `aar` package for Android.
- `--npm`: An `npm` package for Node.js.

By default, all programs specified in `package.json` are built in `release` mode.

For example, a `Docker` image can be built with the following command:
```bash translate=no
ya package <package.json> --docker
```
To do this, add a `Dockerfile` that will be used for subsequent `docker build` and `docker-push` calls to the `json` description of the package. The file itself doesn't have to be located next to the `json` description, but must have `/Dockerfile` in `destination`:
```bash translate=no
{
    "source": {
        "type": "RELATIVE",
        "path": "Dockerfile"
    },
    "destination": {
        "path": "/Dockerfile"
    }
}
```
Before the `docker build` command is invoked, the package is built, and its contents are prepared in its working directory. When forming commands in a `DockerFile` (`COPY` and others), you can expect to have files and the structure of directories described in the `json` description.

### Packaging options

The `ya package` command supports multiple options. You can find out more about them by running `ya package --help`.

- `--no-cleanup`: Skip clearing temporary directories, used for debugging.
- `--change-log=CHANGE_LOG`: Changelog description or a path to the existing changelog file.
- `--publish-to=PUBLISH_TO`: Publish the package to the applicable package repository.
- `--strip`: Clear (strip) binary files of debugging information (only debugging symbols: `strip -g`).
- `--full-strip`: Fully clear (strip) binary files.
- `--no-compression`: Skip compressing a `tar` archive (only for the `--tar` parameter).
- `--create-dbg`: Creates an additional package with debugging information (works only if `--strip` or `--full-strip` are used).
- `--key=KEY`: A key to sign the package.
- `--wheel-repo-access-key=WHEEL_ACCESS_KEY_PATH`: A path to the `wheel` repository access key.
- `--wheel-repo-secret-key=WHEEL_SECRET_KEY_PATH`: A path to the `wheel` repository secret key.
- `--docker-registry=DOCKER_REGISTRY`: `Docker` registry.
- `--docker-repository=DOCKER_REPOSITORY`: Specify a private repository.
- `--docker-save-image`: Save a `Docker` image to an archive.
- `--docker-push`: Save a `Docker` image to the registry.
- `--docker-network=DOCKER_BUILD_NETWORK`: The `--network` parameter for the `docker build` command.
- `--raw-package`: Used with `--tar` to get the contents of the package without packaging.
- `--raw-package-path=RAW_PACKAGE_PATH`: User path for the `--raw-package` parameter.
- `--codec=CODEC`: Name of the `uc` compression codec.
- `--codecs-list`: Show available compression codecs using `--uc`.
- `--ignore-fail-tests`: Create a package regardless of whether the tests failed or not.
- `--new`: Use the new JSON format for `ya package`.
- `--old`: Use the old JSON format for `ya package`.
- `--not-sign-debian`: Skip signing a `debian` package.
- `--custom-version=CUSTOM_VERSION`: Specifies the package version.
- `--debian-distribution=DEBIAN_DISTRIBUTION`: Debian distribution (the default is `unstable`).
- `--arch-all`: Use "Architecture: all" in `debian`.
- `--force-dupload`: Enable `dupload --force`.
- `-z=DEBIAN_COMPRESSION_LEVEL`, `--debian-compression=DEBIAN_COMPRESSION_LEVEL`: `deb` file compression level (none, low, medium, high).
- `-Z=DEBIAN_COMPRESSION_TYPE`, `--debian-compression-type=DEBIAN_COMPRESSION_TYPE`: Compression type used when creating a `deb` file (allowed types: `gzip`, `xz`, `bzip2`, `lzma`, `none`).
- `--data-root=CUSTOM_DATA_ROOT`: A path to the user data directory; the default is `<source root>/../data`.
- `--dupload-max-attempts=DUPLOAD_MAX_ATTEMPTS`: Number of attempts to run `dupload` in case of failure (the default is `1`).
- `--dupload-no-mail`: Enable `dupload` with `--no-mail`.
- `--overwrite-read-only-files`: Overwrite files with read-only permissions in the package.
- `--ensure-package-published`: Ensure that the package is available in the repository.

### Additional parameters

#### Basic build options
- `-d`: Building in debugging mode.
- `-r`: Building in release mode (default).
- `--sanitize=SANITIZE`: Sanitizer type (`address`, `memory`, `thread`, `undefined`, `leak`).
- `--sanitizer-flag=SANITIZER_FLAGS`: Additional flags for the sanitizer.
- `--lto`: Building using Link Time Optimization (`LTO`).
- `--thinlto`: Building using `ThinLTO`.
- `--musl`: Building using the `musl-libc` library.
- `--afl`: Use `AFL` instead of `libFuzzer`.

The majority of [ya make](ya_make.md) build system options are supported.

#### Running tests

- `-A`, `--run-all-tests`: Run all tests when building targets from the `build` section.
- `-t`, `--run-tests`: Run only quick tests when building targets from the `build` section.
- `--ignore-fail-tests`: Ignore test failures when building packages (otherwise the package building process will be aborted).
- `--add-peerdirs-tests=PEERDIRS_TEST_TYPE`: Types of `peerdirs` tests (`none`, `gen`, `all`) (the default is `none`).

#### Publishing and uploading packaging results

You can immediately publish the resulting package to the package repository:

- `--publish-to=<repo_url>`: Publish the package to the applicable package repository.
- `--ensure-package-published`: Check package availability after publishing.
- `--upload-resource-type`: Type of the resource to be uploaded.
- `--artifactory`: Publish the package to the `artifactory` (a storage system for binary versions of components and products).

To upload the package to the `Artifactory`, you need to create and configure the `settings.xml` file. This file stores the settings used by the `Maven Deploy Plugin`. You can use this file to declare all the necessary options that would otherwise have to be passed directly to `Maven`.

`settings.xml` also supports taking the package version from your `package.json`.

To locally upload data to the `Artifactory`, run the `ya package` command with the additional `--artifactory` and `--publish-to` options. Pass the path to `settings.xml` to the `--publish-to` option. It can be either absolute or relative from the root of your project.

To pass the password, use the `--artifactory-password-path` option and specify the path to the file with the password.
```bash translate=no
ya package --artifactory --publish-to /path/settings.xml --artifactory-password-path /path/password/file
```
