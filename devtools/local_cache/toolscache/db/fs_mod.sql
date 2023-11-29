-- Resource may have been deleted externally.

-- NO_STMT: RemoveStart
-- BEGIN EXCLUSIVE;
-- STMT: DeleteUnsafe
DELETE FROM tools WHERE sb_id == @Sb AND path == @Path;

-- Check if removed on FS and remove atomically if needed.

-- NO_STMT: RemoveStop
-- COMMIT;

-- NO_STMT: RemoveStart
-- BEGIN EXCLUSIVE;
-- STMT: CheckSafe
SELECT tool_ref as ToolRef, path AS Path, sb_id AS SbId FROM safe_to_delete WHERE tool_ref == @ToolRef;
-- STMT: DeleteSafe
DELETE FROM tools WHERE ROWID == @ToolRef;
-- Check if removed on FS and remove atomically if needed.

-- NO_STMT: RemoveStop
-- COMMIT;
