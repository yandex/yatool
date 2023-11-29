BEGIN EXCLUSIVE;

CREATE VIEW IF NOT EXISTS acs_blob_plain(
    buid, bref, codec, bmode, relative_path, acs_ref, stored_kind
) AS
    SELECT blobs.uid, blobs.ROWID, blobs.codec, blobs.mode, acs_blob.relative_path, acs_blob.acs_ref, blobs.stored_kind
        FROM (blobs INNER JOIN acs_blob ON blobs.ROWID == acs_blob.blob_ref)
        INNER JOIN acs ON acs.ROWID == acs_blob.acs_ref;

CREATE VIEW IF NOT EXISTS task_disk_usage_helper(
    task_ref, disk_size, size, blob_ref
) AS SELECT tasks.ROWID, blobs.fs_size, blobs.size, blobs.ROWID
    FROM
    (((tasks INNER JOIN requests ON tasks.ROWID == requests.task_ref)
       INNER JOIN acs ON requests.acs_ref == acs.ROWID)
      INNER JOIN acs_blob ON acs_blob.acs_ref == acs.ROWID)
     INNER JOIN blobs ON acs_blob.blob_ref == blobs.ROWID
    GROUP BY tasks.ROWID, blobs.ROWID;

CREATE VIEW IF NOT EXISTS task_disk_usage(
    task_ref, disk_size, size
) AS SELECT task_ref, SUM(disk_size), SUM(size) FROM task_disk_usage_helper GROUP BY task_ref;

-- for testing
CREATE VIEW IF NOT EXISTS view_safe_to_delete(
    acs_ref, uid, aux_uid, eorigin, weight
) AS
    SELECT acs.ROWID, acs.uid, acs.aux_uid, acs.eorigin, acs.weight
        FROM
        acs LEFT OUTER JOIN requests ON acs.ROWID == requests.acs_ref
        WHERE requests.acs_ref IS NULL;

-- Safe to delete.
CREATE VIEW IF NOT EXISTS safe_to_delete(
    acs_ref, uid, aux_uid, eorigin, weight
) AS
    SELECT asd.ROWID, asd.uid, asd.aux_uid, asd.eorigin, asd.weight
        FROM acs AS asd WHERE request_count == 0;

CREATE VIEW IF NOT EXISTS analyze_helper(
    blob_ref, relative_path, acs_count
) AS
    SELECT blob_ref, relative_path, COUNT(acs_ref) FROM acs_blob GROUP BY blob_ref, relative_path;

CREATE VIEW IF NOT EXISTS analyze_files(
    fs_size, size, relative_path, acs_count
) AS
    SELECT SUM(blobs.fs_size), SUM(blobs.size), analyze_helper.relative_path, SUM(analyze_helper.acs_count)
        FROM blobs INNER JOIN analyze_helper ON blobs.ROWID == analyze_helper.blob_ref GROUP BY analyze_helper.relative_path;

COMMIT;
