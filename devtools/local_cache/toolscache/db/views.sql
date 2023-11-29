BEGIN EXCLUSIVE;

-- Explicitly locked forever.
CREATE VIEW IF NOT EXISTS locked_forever(
    tool_ref, sb_id, path, disk_size
) AS SELECT ROWID, sb_id, path, disk_size FROM tools
    WHERE special == 0;

-- Not covered by requests (discovered by FS walker, almost impossible to GC).
CREATE VIEW IF NOT EXISTS externally_added(
    tool_ref, sb_id, path, disk_size
) AS SELECT ROWID, sb_id, path, disk_size FROM tools WHERE special == 1;

CREATE VIEW IF NOT EXISTS tools_not_used(
    tool_ref, sb_id, path, special, disk_size
) AS SELECT tools.ROWID, tools.sb_id, tools.path, tools.special, tools.disk_size
    FROM tools WHERE is_in_use == 0;

-- Safe to delete tools (almost, because fetching resources is done in ya).
CREATE VIEW IF NOT EXISTS tools_and_services_safe_to_delete(
    tool_ref, sb_id, path, special, disk_size
) AS SELECT tools_not_used.tool_ref, tools_not_used.sb_id, tools_not_used.path, tools_not_used.special, tools_not_used.disk_size
    FROM tools_not_used WHERE tools_not_used.special >= 2;

-- Safe to delete (almost, because fetching resources is done in ya).
CREATE VIEW IF NOT EXISTS safe_to_delete(
    tool_ref, sb_id, path, special, disk_size
) AS
    SELECT tsd.tool_ref, tsd.sb_id, tsd.path, tsd.special, tsd.disk_size
        FROM tools_and_services_safe_to_delete AS tsd;

CREATE VIEW IF NOT EXISTS task_disk_usage(
    task_ref, task_id, disk_size
) AS SELECT tasks.ROWID, tasks.task_id, SUM(tools.disk_size)
    FROM
    (tasks INNER JOIN requests ON tasks.ROWID == requests.task_ref)
     INNER JOIN tools ON requests.tool_ref == tools.ROWID AND tools.disk_size > 0
    GROUP BY tasks.ROWID;

CREATE VIEW IF NOT EXISTS task_non_computed(
    task_ref, task_id, non_computed_count
) AS SELECT tasks.ROWID, tasks.task_id, COUNT(tools.disk_size)
    FROM
    (tasks INNER JOIN requests ON tasks.ROWID == requests.task_ref)
     INNER JOIN tools ON requests.tool_ref == tools.ROWID AND tools.disk_size == -1
    GROUP BY tasks.ROWID;

CREATE VIEW IF NOT EXISTS task_locked(
    task_ref, task_id, locked_count, locked_disk_size
) AS SELECT tasks.ROWID, tasks.task_id, COUNT(tools.special), SUM(tools.disk_size)
    FROM
    (tasks INNER JOIN requests ON tasks.ROWID == requests.task_ref)
     INNER JOIN tools ON requests.tool_ref == tools.ROWID AND tools.special < 3
    GROUP BY tasks.ROWID;

-- for testing only
CREATE VIEW IF NOT EXISTS dead_tasks(
    task_ref, task_id, disk_size
) AS SELECT tasks.ROWID, tasks.task_id, SUM(tools.disk_size)
    FROM
    ((tasks LEFT OUTER JOIN running_tasks ON tasks.ROWID == running_tasks.task_ref)
     INNER JOIN requests ON tasks.ROWID == requests.task_ref)
     INNER JOIN tools ON requests.tool_ref == tools.ROWID
    WHERE running_tasks.task_ref IS NULL
    GROUP BY tasks.ROWID;

-- for testing and validation
CREATE VIEW IF NOT EXISTS stale_services(
    tool_ref, sb_id, path, disk_size
) AS SELECT tools.ROWID, tools.sb_id, tools.path, tools.disk_size
    FROM
    tools LEFT OUTER JOIN services ON tools.ROWID == services.tool_ref
    WHERE services.tool_ref IS NULL AND tools.special == 2;

-- for testing and validation
CREATE VIEW IF NOT EXISTS tools_safe_to_delete(
    tool_ref, sb_id, path, special, disk_size
) AS SELECT tools.ROWID, tools.sb_id, tools.path, tools.special, tools.disk_size
    FROM
    tools LEFT OUTER JOIN requests ON tools.ROWID == requests.tool_ref
    WHERE requests.tool_ref IS NULL AND tools.special >= 3;

-- for testing and validation
CREATE VIEW IF NOT EXISTS services_safe_to_delete(
    tool_ref, sb_id, path, disk_size
) AS SELECT tools.ROWID, tools.sb_id, tools.path, tools.disk_size
    FROM
    (tools LEFT OUTER JOIN requests ON tools.ROWID == requests.tool_ref)
     INNER JOIN stale_services ON tools.ROWID == stale_services.tool_ref
    WHERE requests.tool_ref IS NULL;

-- for testing and validation
CREATE VIEW IF NOT EXISTS view_safe_to_delete(
    tool_ref, sb_id, path, special, disk_size
) AS
    SELECT tsd.tool_ref, tsd.sb_id, tsd.path, tsd.special, tsd.disk_size
        FROM tools_safe_to_delete AS tsd
    UNION
    SELECT ssd.tool_ref, ssd.sb_id, ssd.path, 2, ssd.disk_size
         FROM services_safe_to_delete AS ssd;

COMMIT;
