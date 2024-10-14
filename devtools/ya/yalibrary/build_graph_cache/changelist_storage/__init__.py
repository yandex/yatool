import os

import yalibrary.loggers.file_log as file_log


def with_changelist_storage(log_dir, run_uid):
    now = file_log.log_creation_time(run_uid)

    chunks = file_log.LogChunks(log_dir)

    log_chunk = chunks.get_or_create(file_log.format_date(now))
    store_path = os.path.join(
        log_chunk,
        '.'.join([file_log.format_time(now), run_uid, 'changelists']),
    )

    os.makedirs(store_path, exist_ok=True)

    yield store_path

    if not os.listdir(store_path):
        os.rmdir(store_path)
