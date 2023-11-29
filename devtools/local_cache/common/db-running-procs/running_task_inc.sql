-- NO_STMT: Start
-- BEGIN DEFERRED;
-- STMT: SelectRunning
SELECT ROWID AS ProcRef, task_pid AS ProcPid, task_ctime AS TaskCtime, expected_lt AS ExpectedLife FROM running WHERE task_ctime >= @TaskCtime ORDER BY task_ctime ASC LIMIT 16;
-- NO_STMT: Commit is generic
-- COMMIT;

-- NO_STMT:
-- BEGIN DEFERRED;
-- STMT: UsingTasks
SELECT running_tasks.task_ref AS TaskRef FROM running_tasks WHERE running_tasks.running_ref == @ProcRef;
-- STMT: StillRunningTask
SELECT running_tasks.running_ref AS StillRunningCount FROM running_tasks WHERE running_tasks.running_ref != @ProcRef AND running_tasks.task_ref == @TaskRef LIMIT 1;

-- UpdateResourceOnDeadTask

-- STMT: DropProcessLink
DELETE FROM running_tasks WHERE running_tasks.running_ref == @ProcRef;
-- STMT: DropDeadProcess
DELETE FROM running WHERE ROWID == @ProcRef;
-- STMT: DropDeadTask
DELETE FROM tasks WHERE ROWID == @TaskRef;
-- STMT: MarkDeadTask
UPDATE tasks SET alive = 0 WHERE ROWID == @TaskRef;

-- STMT: GetRunningId
SELECT ROWID As Rowid FROM running WHERE task_pid == @Pid AND task_ctime == @Ctime;
-- NO_STMT: Commit is generic
-- COMMIT;
