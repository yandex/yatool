-- BEGIN; PutUid
-- STMT: GetACRowid
SELECT ROWID AS Rowid, eorigin AS Origin, request_count AS RequestCount FROM acs WHERE uid == @Uid;

-- TInsertRunningStmts::InsertRunningProc to get @TaskRef

-- STMT: InsertIntoAC
INSERT INTO acs(uid, aux_uid, eorigin, weight, request_count) VALUES(@Uid, @AuxUid, @Origin, @Weight, 0);

-- STMT: UpdateAC
UPDATE acs SET uid = @Uid, aux_uid = @AuxUid, eorigin = @Origin, weight = @Weight WHERE ROWID = @AcsRef;

-- STMT: SelectFromReqs
SELECT ROWID FROM requests WHERE acs_ref == @AcsRef AND task_ref == @TaskRef;

-- STMT: InsertIntoReqs
INSERT INTO requests(acs_ref, task_ref) VALUES(@AcsRef, @TaskRef);

-- STMT: DeleteFromReqs
DELETE FROM requests WHERE acs_ref == @AcsRef AND task_ref == @TaskRef;

-- STMT: UpdateRequestCount
UPDATE acs SET request_count = @RequestCount WHERE ROWID == @AcsRef;

-- put blobs using cas.sql statements

-- STMT: GetBlobUid
SELECT ROWID AS Rowid, ref_count AS RefCount FROM blobs WHERE uid == @Uid;

-- STMT: InsertIntoACBlobs
INSERT INTO acs_blob(acs_ref, blob_ref, relative_path)
    VALUES(@AcsRef, @BlobRef, @RelativePath);

-- STMT: InsertIntoACGC
INSERT INTO acs_gc(acs_ref, last_access, last_access_time, is_result) VALUES(@AcsRef, @LastAccess, @LastAccessTime, @IsResult);

-- STMT: UpdateIntoACGC
UPDATE acs_gc SET last_access_time = @LastAccessTime, last_access = @LastAccess, is_result = @IsResult WHERE acs_ref == @AcsRef;

-- COMMIT; PutUid

-- BEGIN; GetUid
-- NO_STMT: GetACRowid
-- STMT: GetBlobRefs
SELECT buid AS BlobUid, relative_path AS RelativePath FROM acs_blob_plain WHERE acs_ref == @AcsRef;

-- STMT: DeleteAcsBlob
DELETE FROM acs_blob WHERE acs_ref == @AcsRef;

-- STMT: DeleteDepsFrom
DELETE FROM dependencies WHERE from_ref == @AcsRef;

-- STMT: DeleteDepsTo
DELETE FROM dependencies WHERE to_ref == @AcsRef;

-- STMT: DeleteRequest
DELETE FROM requests WHERE acs_ref == @AcsRef;

-- STMT: DeleteAcsGc
DELETE FROM acs_gc WHERE acs_ref == @AcsRef;

-- get blobs reusing cas.sql statements

-- NO_STMT: UpdateIntoACGC

-- COMMIT; GetUid

-- BEGIN; RemoveUid
-- NO_STMT: GetACRowid
-- NO_STMT: GetBlobRefs

-- remove blobs reusing cas.sql statements

-- STMT: RemoveAC
DELETE FROM acs WHERE ROWID == @AcsRef;

-- GC is due to foreign keys

-- COMMIT; RemoveUid

-- BEGIN; RemoveUidNested

-- STMT: GetACOriginAndUse
SELECT eorigin AS Origin, request_count AS RequestCount FROM acs WHERE ROWID == @AcsRef;

-- remove blobs reusing cas.sql statements
-- NO_STMT: RemoveAC

-- COMMIT; RemoveUidNested

-- BEGIN; PutDeps
    -- NO_STMT: GetACRowid
    -- STMT: GCPutEdge
    INSERT OR IGNORE INTO dependencies(from_ref, to_ref, edge_id) VALUES(@FromUidId, @ToUidId, @EdgeId);
    -- STMT: GCSetNumDeps
    UPDATE acs SET num_dependencies = @NumDeps WHERE ROWID == @AcsRef;
-- COMMIT;
