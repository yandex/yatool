-- STMT: TotalSize
SELECT SUM(disk_size) As Sum FROM tools WHERE disk_size >= 0;

-- STMT: TotalSizeLocked
SELECT SUM(disk_size) As Sum FROM tools WHERE disk_size >= 0 AND special < 3;

-- STMT: NotDiscovered
SELECT COUNT(disk_size) As Cnt FROM tools WHERE disk_size < 0;

-- STMT: LastAccessNumber
SELECT MAX(last_access) As Max FROM toolsgc;

-- STMT: PageSize
PRAGMA PAGE_SIZE;

-- STMT: PageCount
PRAGMA PAGE_COUNT;

-- STMT: ToolsCount
SELECT COUNT(1) As Cnt FROM tools;

-- STMT: ProcsCount
SELECT COUNT(1) As Cnt FROM running;

-- STMT: GetRunningId
-- See common/db-running-procs/insert.sql
SELECT ROWID AS Rowid FROM running WHERE task_pid == @ProcPid AND task_ctime == @ProcCTime;

-- STMT: GetTaskRef
SELECT ROWID AS Rowid FROM tasks WHERE task_id == @TaskId;

-- STMT: TaskDiskSize
SELECT disk_size AS DiskSize FROM task_disk_usage WHERE task_disk_usage.task_ref == @TaskRef;

-- STMT: TaskNonComputed
SELECT non_computed_count AS NonComputedCount FROM task_non_computed WHERE task_non_computed.task_ref == @TaskRef;

-- STMT: TaskLocked
SELECT locked_count AS LockedCount, locked_disk_size AS LockedDiskSize FROM task_locked WHERE task_locked.task_ref == @TaskRef;
