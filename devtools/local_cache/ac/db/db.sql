BEGIN EXCLUSIVE;

CREATE TABLE IF NOT EXISTS blobs(
    ROWID INTEGER NOT NULL,
    uid TEXT NOT NULL,
    -- 0 - absolute path;
    -- 1 - data is in-place in content;
    -- 2 - data is absent.
    stored_kind SMALLINT NOT NULL,
    content BLOB NOT NULL,          -- redundant if stored_kind == 0, 2
    -- size of file
    size INTEGER NOT NULL,          -- redundant if stored_kind == 1
    -- size of file as stored on FS
    fs_size INTEGER NOT NULL,       -- redundant if stored_kind == 1
    -- mode of file
    mode INTEGER NOT NULL,
    -- references from acs
    ref_count INTEGER NOT NULL CHECK(ref_count >= 0), -- 0 for stored_kind == 2
    codec TEXT DEFAULT "",
    PRIMARY KEY(ROWID),
    UNIQUE(uid)
);

CREATE TABLE IF NOT EXISTS acs(
    ROWID INTEGER NOT NULL,
    uid TEXT NOT NULL,
    -- dynamic uid, etc.
    aux_uid TEXT NOT NULL,
    eorigin SMALLINT NOT NULL,
    -- Relative weight to obtain given ac, time or RAM consumed during creation of ac.
    weight INTEGER DEFAULT(0),
    num_dependencies INTEGER DEFAULT(-1) CHECK(num_dependencies >= -1),
    request_count INTEGER NOT NULL CHECK(request_count >= 0),
    PRIMARY KEY(ROWID),
    UNIQUE(uid)
);

-- acs <-> blobs relation
CREATE TABLE IF NOT EXISTS acs_blob(
    ROWID INTEGER NOT NULL,
    acs_ref REFERENCES acs(ROWID),
    -- TODO: Cannot remove blobs without invalidating acs
    blob_ref REFERENCES blobs(ROWID),
    relative_path TEXT NOT NULL,
    PRIMARY KEY(ROWID),
    UNIQUE(acs_ref, blob_ref, relative_path)
);

-- Graph representation
CREATE TABLE IF NOT EXISTS dependencies(
    ROWID INTEGER NOT NULL,
    from_ref REFERENCES acs(ROWID),
    to_ref REFERENCES acs(ROWID),
    -- dynamic uid, etc.
    edge_id INTEGER NOT NULL,
    PRIMARY KEY(ROWID),
    UNIQUE(from_ref, to_ref, edge_id)
);

-- tasks <-> acs relation
-- removal in 'running' causes removal in 'requests'.
CREATE TABLE IF NOT EXISTS requests(
    ROWID INTEGER NOT NULL,
    task_ref INTEGER REFERENCES tasks(ROWID),
    acs_ref INTEGER REFERENCES acs(ROWID),
    PRIMARY KEY(ROWID),
    UNIQUE(task_ref, acs_ref)
);

-- LRU representation for all nodes
CREATE TABLE IF NOT EXISTS acs_gc(
    ROWID INTEGER NOT NULL,
    acs_ref INTEGER REFERENCES acs(ROWID),
    -- Milliseconds since start of epoch.
    last_access_time INTEGER NOT NULL,
    -- strong consistency is not enforced
    last_access INTEGER NOT NULL,
    is_result SMALLINT NOT NULL,  -- whether last access was as for result
    PRIMARY KEY(ROWID),
    UNIQUE(acs_ref)
);

COMMIT;
