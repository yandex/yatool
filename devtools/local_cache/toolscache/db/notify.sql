-- NO_STMT: StartInsert
-- BEGIN EXCLUSIVE;
    -- PRAGMA defer_foreign_keys = ON;
    -- STMT: GetToolId
    SELECT ROWID AS Rowid, special AS Special, disk_size AS DiskSize FROM tools WHERE sb_id == @Sb AND path == @Path;
    -- IF no-empty result
    -- STMT: UpdateTools
    UPDATE tools SET special = @GetToolIdSpecialAcc, is_in_use = 1 WHERE ROWID == @GetToolId;
    -- ELSE
    -- STMT: InsertToTools
    INSERT INTO tools(sb_id, path, special, is_in_use) VALUES(@Sb, @Path, @GetToolIdSpecialAcc, 1);
    -- STMT: GetToolIdAgain
    SELECT ROWID AS Rowid FROM tools WHERE sb_id == @Sb AND path == @Path;
    -- ENDIF

    -- TInsertRunningStmts::InsertRunningProc to get @GetTaskId and runningId

    -- STMT: InsertRequests
    -- Expect that only possible error is constraint violation (duplicate entry).
    INSERT OR IGNORE INTO requests(task_ref, tool_ref) VALUES(@GetTaskId, @GetToolId);

-- Services-specific part of transaction.
    -- STMT: GetService
    SELECT serv.ROWID AS Rowid, serv.version AS Version, serv.tool_ref AS OldToolRef,
        tools.sb_id AS SbId, tools.path AS Path, serv.env_cwd_args AS EnvCmdArgs
        FROM services AS serv INNER JOIN tools ON serv.tool_ref == tools.ROWID WHERE name == @Name;
    -- STMT: InsertToServices
    INSERT INTO services(tool_ref, name, version, env_cwd_args) VALUES(@ToolRef, @Name, @Version, @EnvCwdArgs);

    -- STMT: SelectOtherUsingTask
    SELECT 1 FROM requests WHERE tool_ref == @ToolRef LIMIT 1;
    -- STMT: UpdateIsInUse
    UPDATE tools SET is_in_use = 0 WHERE ROWID == @ToolRef;
    -- STMT: UpdateServices
    UPDATE services SET tool_ref = @ToolRef, version = @Version, env_cwd_args = @EnvCwdArgs WHERE ROWID == @Rowid;
-- NO_STMT: Commit is generic
-- COMMIT;

-- NO_STMT: StartInsertGC
-- BEGIN EXCLUSIVE;
    -- STMT: GetToolRef
    SELECT pattern AS Pattern, bottle AS Bottle FROM toolsgc WHERE tool_ref == @GetToolId;
    -- IF empty result
    -- STMT: InsertForGC
    INSERT INTO toolsgc(tool_ref, pattern, bottle, last_access, last_access_time) VALUES(@GetToolId, @Pattern, @Bottle, @LastAccess, @LastAccessTime);
    -- ELSE
    -- STMT: UpdateForGC
    UPDATE toolsgc SET pattern = @Pattern, bottle = @Bottle, last_access = @LastAccess, last_access_time = @LastAccessTime WHERE tool_ref == @GetToolId;
    -- ENDIF
-- NO_STMT: Commit is generic
-- COMMIT;

-- STMT: EmergencyGC
SELECT sd.sb_id AS SbId, sd.path AS Path FROM safe_to_delete AS sd ORDER BY sd.disk_size DESC LIMIT 64;
