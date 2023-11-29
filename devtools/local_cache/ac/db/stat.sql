-- STMT: LastAccessNumber
SELECT MAX(last_access) As Max FROM acs_gc;

-- STMT: TotalDiskSize
SELECT SUM(fs_size) AS DiskSize, SUM(size) AS Size FROM blobs;

-- STMT: PageSize
PRAGMA PAGE_SIZE;

-- STMT: PageCount
PRAGMA PAGE_COUNT;

-- STMT: BlobsCount
SELECT COUNT(1) As Cnt FROM blobs;

-- STMT: ACsCount
SELECT COUNT(1) As Cnt FROM acs;

-- STMT: ProcsCount
SELECT COUNT(1) As Cnt FROM running;

-- STMT: GetRunningId
-- See common/db-running-procs/insert.sql
SELECT ROWID AS Rowid FROM running WHERE task_pid == @ProcPid AND task_ctime == @ProcCTime;

-- STMT: GetTaskRef
SELECT ROWID AS Rowid FROM tasks WHERE task_id == @TaskId;

-- STMT: TaskDiskSize
SELECT disk_size AS DiskSize, size AS Size FROM task_disk_usage WHERE task_disk_usage.task_ref == @TaskRef;

-- STMT: AnalyzeDisk
SELECT relative_path AS RelativePath, fs_size AS FileSize, size AS Size, acs_count AS AcsCount
    FROM analyze_files ORDER BY fs_size DESC LIMIT 100;
