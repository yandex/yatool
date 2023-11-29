BEGIN EXCLUSIVE;

-- Individual running processes
CREATE TABLE IF NOT EXISTS running(
    ROWID INTEGER NOT NULL,
    task_pid INTEGER NOT NULL,   -- proc_pid is more suitable name
    task_ctime INTEGER NOT NULL, -- proc_ctime is more suitable name
    expected_lt INTEGER NOT NULL,
    PRIMARY KEY(ROWID),
    UNIQUE(task_pid, task_ctime)
);

-- Group of processes, ultimate users of resource
CREATE TABLE IF NOT EXISTS tasks(
    ROWID INTEGER NOT NULL,
    task_id TEXT NOT NULL,
    alive SMALLINT DEFAULT(1),
    PRIMARY KEY(ROWID),
    UNIQUE(task_id)
);

-- tasks <-> running relation
CREATE TABLE IF NOT EXISTS running_tasks(
    ROWID INTEGER NOT NULL,
    running_ref INTEGER REFERENCES running(ROWID),
    task_ref INTEGER REFERENCES tasks(ROWID),
    PRIMARY KEY(ROWID),
    UNIQUE(running_ref)
);

COMMIT;
