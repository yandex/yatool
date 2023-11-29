-- BEGIN; PutBlob
-- STMT: GetBlobChunk
SELECT uid AS Uid, ROWID AS Rowid FROM blobs WHERE ROWID > @Rowid ORDER BY ROWID ASC LIMIT 64;

-- STMT: GetBlobRowid
SELECT ROWID AS Rowid, stored_kind AS DBStoreMode, ref_count AS RefCount FROM blobs WHERE uid == @Uid;

-- STMT: InsertBlob
-- codec is not used and default value set.
INSERT INTO blobs(uid, stored_kind, content, size, fs_size, mode, ref_count)
    VALUES(@Uid, @DBStoreMode, @Content, @Size, @FSSize, @Mode, @RefCount);

-- STMT: GetBlobData
SELECT stored_kind AS DBStoreMode, content AS Content, size AS Size, fs_size AS FSSize,
    ROWID AS Rowid, ref_count AS RefCount, mode AS Mode FROM blobs WHERE uid == @Uid;

-- STMT: UpdateRefCount
UPDATE blobs SET ref_count = @RefCount WHERE ROWID == @Rowid;

-- STMT: RemoveBlobData
DELETE FROM blobs WHERE ROWID == @Rowid;
-- COMMIT; PutBlob
