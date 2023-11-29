-- NO_STMT: StartInsert
-- BEGIN EXCLUSIVE;
    -- STMT: InsertToRunning
    -- Error are cought in GetRunningId 
    -- TODO: count inserted rows
    INSERT OR IGNORE INTO running(task_pid, task_ctime, expected_lt) VALUES(@ProcPid, @ProcCTime, @ExpectedLife);
    -- STMT: GetRunningId
    SELECT ROWID AS Rowid FROM running WHERE task_pid == @ProcPid AND task_ctime == @ProcCTime;

    -- STMT: InsertToTasks
    -- Error are cought in GetRunningId 
    INSERT OR IGNORE INTO tasks(task_id) VALUES(@Task);
    -- STMT: GetTaskId
    SELECT ROWID as Rowid FROM tasks WHERE task_id == @Task;

    -- STMT: InsertToRunningTasks
    -- Expect that only possible error is constraint violation (duplicate entry).
    INSERT OR IGNORE INTO running_tasks(task_ref, running_ref) VALUES(@GetTaskId, @GetRunningId);

-- NO_STMT: Commit is generic
-- COMMIT;
