import logging

import exts.os2
import package
from six.moves.urllib.request import urlopen
import six

DUPLOAD_COMMAND = 'dupload'
RETRY_SLEEP = 120


logger = logging.getLogger(__name__)


class PackageNotFoundError(Exception):
    pass


@exts.retry.retrying(max_times=5, retry_sleep=lambda i, t: RETRY_SLEEP)
def check_if_package_exists(repo, pkg_name, arch_all, debian_distribution, debian_arch):
    repo_web_url = "http://dist.yandex.ru/%s/%s/%s/Packages" % (
        repo,
        debian_distribution,
        "all" if arch_all else debian_arch or "amd64",
    )
    package_web_index = six.ensure_text(urlopen(repo_web_url).read())
    if pkg_name in package_web_index:
        logger.info("%s found in %s" % (pkg_name, repo_web_url))
        return True
    else:
        logger.info("Information about %s was not found in %s" % (pkg_name, repo_web_url))
        raise PackageNotFoundError("package {} was not found in {}".format(pkg_name, repo))


def upload_package(package_file_dir, full_package_name, opts):
    logger.info('Uploading package to dist.yandex.ru')
    with exts.os2.change_dir(package_file_dir):
        for rep in opts.publish_to_list:
            args = []
            if opts.force:
                args.append('--force')
            if opts.dupload_no_mail:
                args.append('--nomail')
            args.extend(['--to', rep])
            package.process.run_process(DUPLOAD_COMMAND, args, max_retry_times=opts.dupload_max_attempts)
            if opts.ensure_package_published:
                check_if_package_exists(
                    rep, full_package_name, opts.arch_all, opts.debian_distribution, opts.debian_arch
                )
