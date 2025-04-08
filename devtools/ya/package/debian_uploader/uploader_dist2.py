import logging

import exts.os2
import os
import package

import yalibrary.tools

RETRY_SLEEP = 120

logger = logging.getLogger(__name__)


def upload_package(package_file_dir, _, opts):
    logger.info('Uploading package to Dist 2.0')
    dist2_command = yalibrary.tools.tool('dist2')
    with exts.os2.change_dir(package_file_dir):
        deb_file_name = list(filter(lambda x: x.endswith('.deb'), os.listdir(package_file_dir)))
        logger.info('Deb packages found: %s', deb_file_name)
        env = os.environ.copy()
        env.update(
            {
                'ACCESS_KEY': opts.dist2_repo_s3_access_key,
                'SECRET_KEY': opts.dist2_repo_s3_secret_key,
                'S3_ENDPOINT': opts.dist2_repo_s3_endpoint,
            }
        )
        if opts.dist2_repo_pgp_private_key:
            env['PGP_PRIVATE_KEY'] = opts.dist2_repo_pgp_private_key
        for key, value in os.environ.items():
            if key.startswith('DIST2_REPO_'):
                env[key.replace('DIST2_REPO_', '', 1)] = value
        for repo in opts.publish_to_list:
            for pkg in deb_file_name:
                upload_args = [
                    'upload',
                    '--bucket',
                    opts.dist2_repo_s3_bucket,
                    '--repo',
                    repo,
                    '--component',
                    opts.debian_distribution,
                    '--file',
                    pkg,
                ]
                logger.info(
                    'Uploading "%s" to S3 bucket "%s", repository "%s", component "%s"',
                    pkg,
                    opts.dist2_repo_s3_bucket,
                    repo,
                    opts.debian_distribution,
                )
                package.process.run_process(
                    dist2_command, upload_args, env=env, max_retry_times=opts.dupload_max_attempts
                )
            if opts.dist2_repo_reindex:
                reindex_args = [
                    'reindex',
                    '--bucket',
                    opts.dist2_repo_s3_bucket,
                    '--repo',
                    repo,
                    '--component',
                    opts.debian_distribution,
                ]
                logger.info(
                    'Reindexing S3 bucket "%s", repository "%s", component "%s"',
                    opts.dist2_repo_s3_bucket,
                    repo,
                    opts.debian_distribution,
                )
                package.process.run_process(
                    dist2_command, reindex_args, env=env, max_retry_times=opts.dupload_max_attempts
                )
