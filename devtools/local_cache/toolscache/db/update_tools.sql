-- STMT: SelectUsedTools
SELECT tool_ref AS ToolRef FROM requests WHERE task_ref == @TaskRef;
-- STMT: SelectOtherUsingTask
SELECT 1 FROM requests WHERE task_ref != @TaskRef AND tool_ref == @ToolRef LIMIT 1;
-- STMT: SelectUsingService
SELECT 1 FROM services WHERE tool_ref == @ToolRef LIMIT 1;
-- STMT: UpdateIsInUse
UPDATE tools SET is_in_use = 0 WHERE ROWID == @ToolRef;
