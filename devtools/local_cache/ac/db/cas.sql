-- BEGIN; PutBlob
-- STMT: GetBlobChunk
SELECT uid AS Uid, ROWID AS Rowid FROM blobs WHERE ROWID > @Rowid ORDER BY ROWID ASC LIMIT 64;

-- STMT: GetBlobRowid
SELECT ROWID AS Rowid, stored_kind AS DBStoreMode, ref_count AS RefCount FROM blobs WHERE uid == @Uid;

-- STMT: InsertBlob
INSERT INTO blobs(uid, stored_kind, content, size, fs_size, mode, ref_count, codec)
    VALUES(@Uid, @DBStoreMode, @Content, @Size, @FSSize, @Mode, @RefCount, @Codec);

-- STMT: GetBlobData
SELECT stored_kind AS DBStoreMode, content AS Content, size AS Size, fs_size AS FSSize,
    ROWID AS Rowid, ref_count AS RefCount, mode AS Mode, IFNULL(codec, '') AS Codec FROM blobs WHERE uid == @Uid;

-- STMT: UpdateRefCount
UPDATE blobs SET ref_count = @RefCount WHERE ROWID == @Rowid;

-- STMT: RemoveBlobData
DELETE FROM blobs WHERE ROWID == @Rowid;
-- COMMIT; PutBlob

-- STMT: SelectColdBlobs
SELECT uid AS Uid, size AS Size FROM blobs
    WHERE IFNULL(codec, '') == '' AND stored_kind == 0 AND size >= @Size AND ref_count > 0
        AND ROWID NOT IN (SELECT acs_blob.blob_ref FROM acs_gc INNER JOIN acs_blob ON acs_blob.acs_ref == acs_gc.acs_ref
                          WHERE acs_gc.last_access_time >= @LastAccessTime)
    ORDER BY size DESC LIMIT @Limit;

-- STMT: SelectBlobAccessedSince
SELECT 1 FROM acs_blob INNER JOIN acs_gc ON acs_gc.acs_ref == acs_blob.acs_ref
    WHERE acs_blob.blob_ref == @Rowid AND acs_gc.last_access_time >= @LastAccessTime LIMIT 1;

-- STMT: UpdateStored
UPDATE blobs SET codec = @Codec, fs_size = @FSSize, mode = @Mode WHERE ROWID == @Rowid;

-- STMT: SelectZstdUids
SELECT uid AS Uid FROM blobs WHERE codec == 'zstd';
