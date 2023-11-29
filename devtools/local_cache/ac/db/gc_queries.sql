-- BEGIN;
    -- last_access_time only so far.
    -- STMT: GCNextAny
    SELECT acs.ROWID AS AcsRef, acs_gc.last_access AS LastAccess, acs.request_count AS RequestCount
        FROM acs INNER JOIN acs_gc ON acs.ROWID == acs_gc.acs_ref WHERE acs_gc.last_access > @PrevLastAccess ORDER BY acs_gc.last_access ASC;
    -- remove using RemoveUidNested
-- COMMIT;

-- BEGIN;
    -- STMT: GCOldNext
    SELECT acs.ROWID AS AcsRef, acs_gc.last_access_time AS LastAccessTime, acs.request_count AS RequestCount
        FROM acs INNER JOIN acs_gc ON acs.ROWID == acs_gc.acs_ref WHERE acs_gc.last_access_time > @PrevLastAccessTime AND acs_gc.last_access_time < @AgeLimit ORDER BY acs_gc.last_access ASC;
    -- remove using RemoveUidNested
-- COMMIT;

-- BEGIN;
    -- STMT: GCBigBlobs
    SELECT ROWID AS BlobRef, fs_size AS FSSize FROM blobs WHERE ROWID >= @PrevBlobId ORDER BY ROWID ASC;
    -- STMT: GCBigAcs
    SELECT DISTINCT acs_ref AS AcsRef FROM acs_blob WHERE blob_ref == @BlobRef;
    -- STMT: GCAcReqCount
    SELECT request_count AS RequestCount FROM acs WHERE ROWID == @AcsRef;
    -- remove using RemoveUidNested
-- COMMIT;

-- BEGIN;
-- STMT: SelectDeadTask
SELECT ROWID As TaskRef FROM tasks WHERE alive == 0;
-- STMT: SelectUsedAcs
SELECT acs_ref AS AcsRef, ROWID As ReqRowid FROM requests WHERE task_ref == @TaskRef;
-- STMT: SelectReqCount
SELECT request_count AS RequestCount from acs WHERE ROWID == @AcsRef;
-- STMT: UpdateIsInUse
UPDATE acs SET request_count = @RequestCount WHERE ROWID == @AcsRef;
-- STMT: DeleteReq
DELETE FROM requests WHERE ROWID = @ReqRowid;
-- STMT: DeleteDeadTask
DELETE FROM tasks WHERE ROWID = @TaskRef;
-- COMMIT;
