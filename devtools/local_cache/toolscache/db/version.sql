-- STMT: GetService
SELECT version AS Version from services WHERE name == @Name;
