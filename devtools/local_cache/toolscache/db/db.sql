BEGIN EXCLUSIVE;

-- Entities being managed
CREATE TABLE IF NOT EXISTS tools(
    ROWID INTEGER NOT NULL,
    sb_id TEXT NOT NULL,
    path TEXT NOT NULL,
    -- 0 - do not remove. For example, multi-file service where atomic unlink is hard.
    -- 1 - stale: downloaded w/o notification
    -- 2 - known service, see known_service.proto.
    -- The following are safe to remove
    -- 3 - tool + regular resource
    -- 4 - regular resource
    -- 5 - tool
    special SMALLINT NOT NULL,
    -- asynchronously computed
    disk_size INTEGER DEFAULT(-1),
    is_in_use SMALLINT DEFAULT(1), -- ignored in running_task.sql for special < 2
    PRIMARY KEY(ROWID),
    UNIQUE(sb_id, path)
);

-- Additional information about services
CREATE TABLE IF NOT EXISTS services(
    ROWID INTEGER NOT NULL,
    tool_ref INTEGER REFERENCES tools(ROWID) ON DELETE CASCADE,
    version INTEGER NOT NULL,
    name TEXT NOT NULL,
    env_cwd_args TEXT NOT NULL,
    PRIMARY KEY(ROWID),
    UNIQUE(name)
);

-- tasks <-> tools relation
-- removal in 'running' causes removal in 'requests'.
CREATE TABLE IF NOT EXISTS requests(
    task_ref INTEGER REFERENCES tasks(ROWID) ON DELETE CASCADE,
    tool_ref INTEGER REFERENCES tools(ROWID) ON DELETE CASCADE,
    UNIQUE(task_ref, tool_ref)
);

CREATE TABLE IF NOT EXISTS toolsgc(
    ROWID INTEGER NOT NULL,
    tool_ref INTEGER REFERENCES tools(ROWID) ON DELETE CASCADE,
    -- may be null for special <= 2
    pattern TEXT,
    -- may be null for special <= 2
    bottle TEXT,
    last_access_time INTEGER NOT NULL,
    -- strong consistency is not enforced
    last_access INTEGER NOT NULL,
    PRIMARY KEY(ROWID),
    UNIQUE(tool_ref)
);

COMMIT;
