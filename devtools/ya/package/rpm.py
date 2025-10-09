import os
import re

import exts.archive
import exts.fs
import exts.tmp
import exts.os2
import exts.path2

import package.source
import package.tarball
import package.fs_util
import package.noconffiles
import package.process


SPEC_FILE = """
Name:\t {name}
Version:\t {version}
Release:\t {release}
Summary:\t {summary}

License:\t {license}
URL:\t {url}
{sources}

{requires}
{pre_requires}
{provides}
{conflicts}
{obsoletes}
{buildarch}

%description
{description}
%prep
%setup -q

%install
mkdir -p %{buildroot}
cp -rfa * %{buildroot}

{additional}

%files
{files}
"""

rpm_sections = {'pre', 'post', 'preun', 'postun', 'check', 'changelog'}

RPMBUILD_COMMAND = "rpmbuild"

UPLOAD_COMMAND = "scp"
DEFAULT_REPO = "paysys-rhel"
DEFAULT_REPO_VERSION = "7"
INSTALL_COMMAND = ["sudo", "/usr/sbin/rpm-install", "-n", "%%REPO%%", "-v", "%%REPO_VERSION%%"]
SSH = "ssh"


def prepare_install_cmd(publish_to):
    install_repo = DEFAULT_REPO
    install_repo_verion = DEFAULT_REPO_VERSION
    m = re.match(r'[^:]+:/repo/([^/]+)/incoming/([^/]+)/?', publish_to)
    if m:
        install_repo = m.groups()[0]
        install_repo_verion = m.groups()[1]
    return [
        part.replace('%%REPO%%', install_repo).replace('%%REPO_VERSION%%', install_repo_verion)
        for part in INSTALL_COMMAND
    ]


def publish_package(path_to_package, publish_to, key_user):
    if publish_to.startswith("paysys-rhel.dist.yandex"):

        remote_host, remote_path = publish_to.split(':', 1)

        pkg_dir = os.path.dirname(path_to_package)
        pkg_file = os.path.basename(path_to_package)

        tar_cmd = ['tar', 'cf', '-', '-C', pkg_dir, pkg_file]

        # Extract directory path from remote_path
        remote_dir = os.path.dirname(remote_path)

        # Remote command to extract tar and install
        remote_cmd = f"tar xf - -C '{remote_dir}'"
        install_cmd = prepare_install_cmd(publish_to)
        remote_cmd += f" && {' '.join(install_cmd)}"

        # Combine commands into single SSH session
        package.process.run_process(
            'bash', ['-c', f"{' '.join(tar_cmd)} | {SSH} {key_user}@{remote_host} '{remote_cmd}'"]
        )
    else:
        # For non-paysys hosts, use original SCP method
        package.process.run_process(UPLOAD_COMMAND, [path_to_package, key_user + "@" + publish_to])


def create_rpm_package(temp_dir, package_context, rpmbuild_dir, publish_to_list, key_user):
    package_name = package_context.package_name
    args = [
        '--define',
        '_topdir {}'.format(os.path.join(temp_dir, "rpmbuild")),
        '-bb',
        os.path.join(rpmbuild_dir, "SPECS", package_name + '.spec'),
    ]
    package.process.run_process(RPMBUILD_COMMAND, args)
    tar_gz_file = package_context.resolve_filename(extra={"package_ext": "tar.gz"})
    if publish_to_list:
        packages_dir = os.path.join(rpmbuild_dir, "RPMS")
        for publish_to in publish_to_list:
            for rpm_arch in os.listdir(packages_dir):
                for rpm_package in os.listdir(os.path.join(packages_dir, rpm_arch)):
                    source = os.path.join(packages_dir, rpm_arch, rpm_package)
                    dest = os.path.join(publish_to, rpm_package)
                    publish_package(source, dest, key_user)

    with package.fs_util.AtomicPath(tar_gz_file) as temp_file:
        exts.archive.create_tar(
            os.path.join(rpmbuild_dir, "RPMS"),
            temp_file,
        )

    return tar_gz_file


def prepare_rpm_folder_structure(temp_dir, spec_file, gz_file):
    rpmbuild_dir = exts.fs.create_dirs(os.path.join(temp_dir, "rpmbuild"))
    sources_dir = exts.fs.create_dirs(os.path.join(rpmbuild_dir, "SOURCES"))
    specs_dir = exts.fs.create_dirs(os.path.join(rpmbuild_dir, "SPECS"))
    exts.fs.create_dirs(os.path.join(rpmbuild_dir, "SRPMS"))
    exts.fs.create_dirs(os.path.join(rpmbuild_dir, "BUILD"))
    exts.fs.create_dirs(os.path.join(rpmbuild_dir, "RPMS"))
    exts.fs.copy_file(spec_file, os.path.join(specs_dir, os.path.basename(spec_file)))
    exts.fs.copy_file(gz_file, os.path.join(sources_dir, os.path.basename(gz_file)))
    return rpmbuild_dir


def create_spec_file(
    temp_dir,
    package_name,
    package_version,
    package_release,
    package_buildarch,
    package_summary,
    package_url,
    package_requires,
    package_pre_requires,
    package_provides,
    package_conflicts,
    package_obsoletes,
    package_sources,
    package_description,
    package_license,
    package_files,
    package_root,
):
    spec_file = os.path.join(temp_dir, package_name + '.spec')
    additional_section = ''
    sources = ''
    if os.path.exists(os.path.join(package_root, 'rpm')):
        additional_sections = os.listdir(os.path.join(package_root, 'rpm'))
        for section in additional_sections:
            if section in rpm_sections:
                with open(os.path.join(package_root, 'rpm', section), 'r') as afile:
                    section_content = afile.read()
                    if section_content.startswith("%{}".format(section)):
                        additional_section += section_content + '\n'
                    else:
                        additional_section += "%{}".format(section) + '\n' + section_content + '\n'

    if package_sources:
        for num, source in enumerate(package_sources):
            sources += "Source{num}:\t {src}\n".format(num=num, src=source)
    requires = ''
    pre_requires = ''
    conflicts = ''
    provides = ''
    obsoletes = ''
    buildarch = ''
    if package_requires:
        requires = "Requires:\t {}".format(', '.join(package_requires))
    if package_pre_requires:
        pre_requires = "Requires(pre):\t {}".format(', '.join(package_pre_requires))
    if package_conflicts:
        conflicts = "Conflicts:\t {}".format(', '.join(package_conflicts))
    if package_provides:
        provides = "Provides:\t {}".format(', '.join(package_provides))
    if package_obsoletes:
        obsoletes = "Obsoletes:\t {}".format(', '.join(package_obsoletes))
    if package_buildarch:
        buildarch = "BuildArch:\t {}".format(package_buildarch)

    with open(spec_file, 'w') as sfile:
        sfile.write(
            SPEC_FILE.format(
                name=package_name,
                version=package_version,
                release=package_release,
                summary=package_summary,
                license=package_license,
                buildarch=buildarch,
                url=package_url,
                requires=requires,
                pre_requires=pre_requires,
                provides=provides,
                conflicts=conflicts,
                obsoletes=obsoletes,
                sources=sources,
                description=package_description,
                buildroot="buildroot",
                files=package_files,
                additional=additional_section,
            )
        )

    return spec_file


def create_gz_file(package_context, temp_work_dir, package_data_path, threads=None):
    package_name = package_context.package_name
    package_version = package_context.version.split('-', 1)[0]

    dir_to_arch = os.path.join(temp_work_dir, "dir_to_arch")
    content_dir = os.path.join(dir_to_arch, package_name) + '-{}'.format(package_version)
    exts.fs.copytree3(package_data_path, content_dir, symlinks=True)

    package_filename = package_context.resolve_filename(extra={"package_ext": "tar.gz"})
    return package.tarball.create_tarball_package(os.getcwd(), dir_to_arch, package_filename, threads=threads)
