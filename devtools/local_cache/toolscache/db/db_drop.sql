BEGIN EXCLUSIVE;
DROP VIEW IF EXISTS view_safe_to_delete;
DROP VIEW IF EXISTS services_safe_to_delete;
DROP VIEW IF EXISTS tools_safe_to_delete;
DROP VIEW IF EXISTS stale_services;
DROP VIEW IF EXISTS dead_tasks;
DROP VIEW IF EXISTS task_disk_usage;
DROP VIEW IF EXISTS safe_to_delete;
DROP VIEW IF EXISTS tools_and_services_safe_to_delete;
DROP VIEW IF EXISTS tools_not_used;
DROP VIEW IF EXISTS externally_added;
DROP VIEW IF EXISTS locked_forever;

DROP TABLE IF EXISTS toolsgc;
DROP TABLE IF EXISTS requests;
DROP TABLE IF EXISTS running_tasks;
DROP TABLE IF EXISTS tasks;
DROP TABLE IF EXISTS running;
DROP TABLE IF EXISTS services;
DROP TABLE IF EXISTS tools;
COMMIT;

VACUUM;
