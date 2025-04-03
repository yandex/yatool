import re
import os
import stat
import shutil
import logging

import exts.archive
import exts.fs
import exts.tmp
import exts.os2
import exts.path2
import exts.retry

import devtools.ya.core.yarg
import package
import package.debian_uploader as uploader
import package.packager
import package.source
import package.fs_util
import package.noconffiles
import package.process
from devtools.ya.package import const

logger = logging.getLogger(__name__)

DEBUILD_COMMAND = 'debuild'

RULES_CONTENT = """\
#!/usr/bin/make -f
export DH_VERBOSE=1

%:
\tdh $@

PACKAGE=$(word 1, $(shell dh_listpackages))
TMP=$(CURDIR)/debian/$(PACKAGE)

override_dh_builddeb:
\t$(CURDIR)/debian/dh_noconffiles {noconffiles_opts}
\t$(CURDIR)/debian/chown.sh $(TMP)
\tdh_md5sums
\tdh_builddeb -- {builddeb_opts}

override_dh_usrlocal:
\t# Nothing to do

override_dh_shlibdeps:
\t# suppress for cross compilation
"""

CONTROL_CONTENT = """\
Source: {source}
Section: {section}
Priority: optional
Maintainer: {maintainer}
Build-Depends: {buildDepends}
Standards-Version: 3.9.3
Homepage: {homepage}

Package: {package}
Architecture: {arch}
Pre-Depends: {preDepends}
Depends: {depends}
Provides: {provides}
Conflicts: {conflicts}
Replaces: {replaces}
Breaks: {breaks}
Description: {description}
"""

DEBUG_CONTROL_CONTENT = """\

Package: {debug_package}
Architecture: {arch}
Section: debug
Depends: {package} (= ${{binary:Version}}), ${{misc:Depends}}
Description: Debug symbols for {package}
"""

DCH_COMMAND = "dch"

# This hook runs in the unpacked source directory (as per `man dpkg-buildpackage`),
# which in this case is new_result_dir. Build outputs are produced in the parent
# directory (see https://www.debian.org/doc/manuals/maint-guide/), i.e. temp_dir,
# hence the use of the wildcard '../*.deb'.
# NOTE: hook must be a single line & must not include '='
DEBSIGS_HOOK_FMT = "debsigs -v --sign origin {key} ../*.deb"


def create_debian_package(
    result_dir,
    package_context,
    arch_all,
    sign,
    sign_debsigs,
    key,
    sloppy_deb,
    publish_to_list,
    force,
    store_debian,
    dupload_max_attempts,
    dupload_no_mail,
    ensure_package_published,
    debian_arch,
    debian_distribution,
    debian_upload_token,
    dist2_repo,
    dist2_repo_pgp_private_key,
    dist2_repo_reindex,
    dist2_repo_s3_access_key,
    dist2_repo_s3_bucket,
    dist2_repo_s3_endpoint,
    dist2_repo_s3_secret_key,
):
    package_name = package_context.package_name
    package_version = package_context.version
    full_package_name = '_'.join([package_name, str(package_version)])

    with exts.tmp.temp_dir() as temp_dir:
        new_result_dir = os.path.join(temp_dir, 'result_dir')

        # Temporally move result_dir inside temp_dir
        shutil.move(result_dir, new_result_dir)

        if debian_upload_token and os.path.exists(debian_upload_token):
            with open(debian_upload_token) as f:
                debian_upload_token = f.read()

        try:
            with exts.os2.change_dir(new_result_dir):
                args = [
                    '--preserve-envvar=TMP',
                    '--preserve-envvar=TMPDIR',
                    '--no-lintian',
                    '--no-tgz-check',
                    '-b',
                ]

                if debian_arch:
                    args.extend(['-a{}'.format(debian_arch)])

                if key and sign:
                    args.append('-k{}'.format(key))

                if not sign:
                    args.extend(['-i', '-us', '-uc'])

                # This hook is passed through to dpkg-buildpackage,
                # so it must be placed after all the regular debuild arguments
                if sign and sign_debsigs:
                    debsigs_hook_key = '--default-key {}'.format(key) if key else ''
                    debsigs_hook = DEBSIGS_HOOK_FMT.format(key=debsigs_hook_key)
                    args.append('--hook-buildinfo={}'.format(debsigs_hook))

                package.process.run_process(
                    DEBUILD_COMMAND,
                    args,
                    add_line_timestamps=True,
                    tee=True,
                )

                if publish_to_list:
                    upload_opts = devtools.ya.core.yarg.merge_opts([])
                    upload_opts.force = force
                    upload_opts.dupload_no_mail = dupload_no_mail
                    upload_opts.dupload_max_attempts = dupload_max_attempts
                    upload_opts.ensure_package_published = ensure_package_published
                    upload_opts.arch_all = arch_all
                    upload_opts.debian_distribution = debian_distribution
                    upload_opts.debian_arch = debian_arch
                    upload_opts.debian_upload_token = debian_upload_token

                    if dist2_repo:
                        upload_opts.dist2_repo_pgp_private_key = dist2_repo_pgp_private_key
                        upload_opts.dist2_repo_reindex = dist2_repo_reindex
                        upload_opts.dist2_repo_s3_access_key = dist2_repo_s3_access_key
                        upload_opts.dist2_repo_s3_bucket = dist2_repo_s3_bucket
                        upload_opts.dist2_repo_s3_endpoint = dist2_repo_s3_endpoint
                        upload_opts.dist2_repo_s3_secret_key = dist2_repo_s3_secret_key

                    publish_to_curl_list = []
                    publish_to_dist_list = []
                    publish_to_dist2_repo_list = []
                    for rep in publish_to_list:
                        if dist2_repo:
                            publish_to_dist2_repo_list.append(rep)
                        elif rep.startswith(('http://', 'https://')):
                            publish_to_curl_list.append(rep)
                        else:
                            publish_to_dist_list.append(rep)

                    if publish_to_curl_list:
                        upload_curl_opts = upload_opts
                        upload_curl_opts.publish_to_list = publish_to_list
                        uploader.uploader_curl.upload_package(temp_dir, full_package_name, upload_curl_opts)
                    if publish_to_dist_list:
                        upload_dist_opts = upload_opts
                        upload_dist_opts.publish_to_list = publish_to_list
                        uploader.uploader_dist.upload_package(temp_dir, full_package_name, upload_dist_opts)
                    if publish_to_dist2_repo_list:
                        upload_dist_opts = upload_opts
                        upload_dist_opts.publish_to_list = publish_to_list
                        uploader.uploader_dist2.upload_package(temp_dir, full_package_name, upload_dist_opts)
        finally:
            # Move result_dir back
            shutil.move(new_result_dir, result_dir)

        if store_debian:
            tar_gz_file = package_context.resolve_filename(extra={"package_ext": "tar.gz"})

            with package.fs_util.AtomicPath(tar_gz_file) as temp_file:
                exts.archive.create_tar(
                    temp_dir,
                    temp_file,
                    exts.archive.GZIP,
                    exts.archive.Compression.Fast if sloppy_deb else exts.archive.Compression.Default,
                )

            return tar_gz_file


def prepare_debian_folder(result_dir, debian_dir):
    temp_debian_dir = os.path.join(result_dir, 'debian')

    if os.path.exists(debian_dir):
        exts.fs.copytree3(debian_dir, temp_debian_dir)
    else:
        exts.fs.create_dirs(temp_debian_dir)

    noconffiles = os.path.join(temp_debian_dir, 'dh_noconffiles')
    exts.fs.write_file(noconffiles, package.noconffiles.SCRIPT)
    st = os.stat(noconffiles)
    os.chmod(noconffiles, st.st_mode | stat.S_IEXEC)

    return temp_debian_dir


def create_rules_file(debian_dir, source_elements, params, result_root, noconffiles_all):
    rules_file_name = os.path.join(debian_dir, 'rules')
    chown_file_name = os.path.join(debian_dir, 'chown.sh')

    with open(chown_file_name, 'w') as chown_file:
        chown_file.write('#!/bin/sh\n' 'cd $@\n')

        for element in source_elements:
            attributes = element.get_attributes()

            for attribute, command in zip(package.source.ATTRIBUTES, ['chown', 'chgrp', 'chmod']):
                value = attributes[attribute]['value']
                recursive = attributes[attribute]['recursive']

                destinations = [os.path.relpath(p, result_root) for p in element.destination_paths] or [
                    element.destination_path()
                ]
                if value:
                    for path in destinations:
                        chown_file.write(
                            '{command} {recursive} {value} {path}\n'.format(
                                command=command,
                                recursive='-R' if recursive else '',
                                value=value,
                                path=path,
                            )
                        )

        os.chmod(chown_file_name, 0o755)

    if os.path.exists(rules_file_name):
        logger.warning("debian/rules exists, but is not used. Please write your rules to the debian/extra_rules.")

    with open(rules_file_name, 'w') as rules_file:
        builddeb_opts = []
        if params.sloppy_deb:
            builddeb_opts.append('-z0')
        elif params.debian_compression_level is not None:
            builddeb_opts.append('-z{}'.format(params.debian_compression_level))

        if (
            params.debian_compression_type
            and params.debian_compression_type != const.DEBIAN_HOST_DEFAULT_COMPRESSION_LEVEL
        ):
            builddeb_opts.append('-Z{}'.format(params.debian_compression_type))

        builddeb_opts = " ".join(builddeb_opts)
        logger.debug("builddeb_opts = %s", builddeb_opts)
        rules_file.write(
            RULES_CONTENT.format(builddeb_opts=builddeb_opts, noconffiles_opts='--all' if noconffiles_all else '')
        )
        if not params.strip or params.full_strip or params.create_dbg:
            rules_file.write('\noverride_dh_strip:\n\t# Preserve debug symbols\n')
        extra_rules = os.path.join(debian_dir, "extra_rules")
        if os.path.exists(extra_rules):
            logger.info("Using extra_rules file %s", extra_rules)
            extra_rules_data = exts.fs.read_file(extra_rules)
            if isinstance(extra_rules_data, bytes):
                extra_rules_data = extra_rules_data.decode()
            rules_file.write("\n" + extra_rules_data)


def create_install_file(debian_dir, source_elements, package_name):
    install_file_name = os.path.join(debian_dir, package_name + '.install')

    for f in os.listdir(debian_dir):
        if f.endswith('install'):
            os.remove(os.path.join(debian_dir, f))

    with open(install_file_name, 'w') as install_file:
        install_file.write('.content/* /\n')


def create_compat_file(debian_dir):
    compat_file_name = os.path.join(debian_dir, 'compat')

    if os.path.exists(compat_file_name):
        package.display.emit_message('Using existing compat file: [[imp]]{}[[rst]]'.format(compat_file_name))
        return

    with open(compat_file_name, 'w') as compat_file:
        compat_file.write('7\n')


def create_control_file(debian_dir, package_name, package_meta, arch_all, debug_package_name=None):
    control_file_name = os.path.join(debian_dir, 'control')

    if os.path.exists(control_file_name):
        package.display.emit_message('Using existing control file: [[imp]]{}[[rst]]'.format(control_file_name))
        return

    description = package_meta.get('description', '')
    description = description.replace('\n', '\n\t')

    arch = 'all' if arch_all else 'any'

    with open(control_file_name, 'w') as control_file:
        control_file.write(
            CONTROL_CONTENT.format(
                package=package_name,
                source=package_meta.get('source', package_name),
                preDepends=', '.join(package_meta.get('pre-depends', [])),
                buildDepends=", ".join(["debhelper (>= 7.0.0)"] + package_meta.get("build-depends", [])),
                depends=', '.join(package_meta.get('depends', [])),
                provides=', '.join(package_meta.get('provides', [])),
                conflicts=', '.join(package_meta.get('conflicts', [])),
                replaces=', '.join(package_meta.get('replaces', [])),
                breaks=', '.join(package_meta.get('breaks', [])),
                maintainer=package_meta.get('maintainer'),
                description=description,
                homepage=package_meta.get('homepage', 'http://www.yandex.ru'),
                arch=arch,
                section=package_meta.get('section', 'misc'),
            )
        )

        if debug_package_name:
            control_file.write(
                DEBUG_CONTROL_CONTENT.format(
                    package=package_name,
                    debug_package=debug_package_name,
                    arch=arch,
                )
            )


def create_changelog_file(
    debian_dir,
    package_name,
    package_version,
    distribution,
    changelog_message,
    force_bad_version=False,
):
    changelog_file_path = os.path.join(debian_dir, 'changelog')

    if not os.path.exists(changelog_file_path):
        args = ["--create"]
    else:
        args = []

    args += [
        '-v',
        package_version,
        '--package',
        package_name,
        '--force-distribution',
        '-D',
        distribution,
    ]

    if force_bad_version:
        args.append('--force-bad-version')

    if changelog_message:
        args.append(changelog_message)
    with exts.os2.change_dir(os.path.join(debian_dir, '..')):
        package.process.run_process(DCH_COMMAND, args)


class ChangeLogVersion(object):
    version_re = "Version: (.*)$"

    def __init__(self, changelog):
        self._changelog = changelog
        self._version = None

    def __str__(self):
        if not self._version:
            assert os.path.exists(self._changelog), "Cannot find changelog file to get the version from"
            out, _ = package.process.run_process("dpkg-parsechangelog", ["-l{}".format(self._changelog)])
            out = out.strip()
            search = re.search("Version: (.*)$", out, re.MULTILINE)
            assert search, "Cannot parse changelog version from {}".format(out)
            self._version = search.groups()[0]
        return self._version
