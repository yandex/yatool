-- STMT: GetChunk
SELECT tools.ROWID AS Rowid, tools.path AS Path, tools.sb_id AS SbId FROM tools INNER JOIN toolsgc ON tools.ROWID == toolsgc.tool_ref WHERE disk_size == -1 ORDER BY last_access ASC LIMIT 10;
-- STMT: GetChunkAll
SELECT tools.ROWID AS Rowid, tools.path AS Path, tools.sb_id AS SbId FROM tools WHERE disk_size == -1 LIMIT 10;
-- STMT: UpdateSize
-- May have gone before we computed size.
UPDATE OR IGNORE tools SET disk_size = @NewSize WHERE ROWID == @Rowid;

-- NO_STMT: PopulateStart
-- BEGIN DEFERRED;
-- STMT: QueryExisting
SELECT sb_id AS SbId FROM tools WHERE path == @Path;

-- Get listing and compare with query results.

-- STMT: InsertDiscovered
-- special == 1, disk_size == -1
INSERT INTO tools(sb_id, path, special, disk_size) VALUES(@Sb, @Path, 1, -1);
-- NO_STMT: PopulateStop
-- COMMIT;
