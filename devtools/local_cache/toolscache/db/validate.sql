-- is_in_use is critical for quiescence of service.
-- Any error (or incompatibility wrt is_in_use) should be fixed before start.
-- is_in_use is optimization to get rid of LEFT OUTER JOINs
UPDATE tools SET is_in_use = 0 WHERE ROWID IN (SELECT tool_ref FROM view_safe_to_delete);

-- CREATE INDEX IF NOT EXISTS tools_for_use ON tools(disk_size DESC) WHERE is_in_use == 0;

-- CREATE INDEX IF NOT EXISTS tools_for_disk_asc ON tools(disk_size ASC) WHERE disk_size >= 0;

-- CREATE INDEX IF NOT EXISTS tools_for_unknown ON tools(disk_size) WHERE disk_size < 0;

-- CREATE INDEX IF NOT EXISTS tools_for_disk_desc ON tools(disk_size DESC) WHERE disk_size >= 0;

CREATE INDEX IF NOT EXISTS requests_for_tools ON requests(tool_ref);

CREATE INDEX IF NOT EXISTS requests_for_tasks ON requests(task_ref);

CREATE INDEX IF NOT EXISTS services_for_tools ON services(tool_ref);
