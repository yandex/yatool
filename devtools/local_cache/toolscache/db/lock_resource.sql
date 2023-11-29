-- BEGIN EXCLUSIVE;
    -- STMT: LockResourceStmt
    -- Expect that only possible error is constraint violation (missing entry).
    UPDATE OR IGNORE tools SET special = 0, is_in_use = 1 WHERE sb_id == @Sb AND path == @Path;
-- COMMIT;

-- BEGIN EXCLUSIVE;
    -- STMT: UnlockResourceStmt
    UPDATE OR IGNORE tools SET special = 3, is_in_use = 0 WHERE sb_id == @Sb AND path == @Path;
-- COMMIT;

-- BEGIN EXCLUSIVE;
    -- STMT: UnlockAllResourcesStmt
    UPDATE OR IGNORE tools SET special = 3, is_in_use = 0 WHERE special == 0;
-- COMMIT;
