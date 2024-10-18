import os
import uuid

import yalibrary.loggers.file_log as file_log
import logging

_UUID_LENGTH = 6
logger = logging.getLogger(__name__)


def with_changelist_storage(log_dir, run_uid):
    now = file_log.log_creation_time(run_uid)

    chunks = file_log.LogChunks(log_dir)

    log_chunk = chunks.get_or_create(file_log.format_date(now))
    store_path = os.path.join(
        log_chunk,
        '.'.join([file_log.format_time(now), run_uid, 'changelists', uuid.uuid4().hex[:_UUID_LENGTH]]),
    )

    os.makedirs(store_path, exist_ok=True)

    yield store_path

    try:
        if not os.listdir(store_path):
            os.rmdir(store_path)
    except Exception as e:
        logger.debug('Failed to remove changelist storage %s', store_path, exc_info=e)
