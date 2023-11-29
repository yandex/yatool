-- NO_STMT: RemoveFileGCStart
-- BEGIN EXCLUSIVE;
    -- last_access_time only so far.
    -- STMT: GCNext
    SELECT  sd.tool_ref AS ToolRef,
            -- sd.sb_id AS SbId,
            -- sd.path AS Path,
            -- sd.special AS Special,
            sd.disk_size AS DiskSize
            -- gc.last_access_time AS LastAccessTime,
            -- gc.last_access AS LastAccess
    FROM safe_to_delete AS sd INNER JOIN toolsgc AS gc USING(tool_ref) ORDER BY gc.last_access_time ASC LIMIT 1;
    -- STMT: GCStale
    SELECT  sd.tool_ref AS ToolRef
            -- sd.sb_id AS SbId,
            -- sd.path AS Path,
            -- sd.special AS Special,
            -- gc.last_access_time AS LastAccessTime,
            -- gc.last_access AS LastAccess
    FROM safe_to_delete AS sd WHERE sd.disk_size == -1 LIMIT 1;
-- NO_STMT: RemoveFileGCStop
-- COMMIT;
