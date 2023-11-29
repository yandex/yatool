PRAGMA main.journal_mode = WAL;
PRAGMA main.locking_mode = NORMAL;
-- Do not be paranoid.
PRAGMA main.synchronous = OFF;
PRAGMA temp_store = MEMORY;
PRAGMA main.auto_vacuum = FULL;
PRAGMA threads = 2;
PRAGMA busy_timeout = 50;

-- Checks cost ~2sec during startup on 60MB db.
-- PRAGMA main.integrity_check = 1000;
-- PRAGMA main.foreign_key_check;
PRAGMA main.mmap_size = 20971520;
