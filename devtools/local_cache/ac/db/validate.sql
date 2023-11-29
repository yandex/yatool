CREATE INDEX IF NOT EXISTS gc_last_access_time ON acs_gc(last_access_time ASC);

CREATE INDEX IF NOT EXISTS gc_last_access ON acs_gc(last_access ASC);

CREATE INDEX IF NOT EXISTS acs_not_in_use ON acs(weight ASC) WHERE request_count == 0;

CREATE INDEX IF NOT EXISTS dead_tasks ON tasks(ROWID) WHERE alive == 0;

CREATE INDEX IF NOT EXISTS to_edge ON dependencies(to_ref);

CREATE INDEX IF NOT EXISTS from_edge ON dependencies(from_ref);

CREATE INDEX IF NOT EXISTS acs_request ON requests(acs_ref);
