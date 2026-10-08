-- A query that joins a DuckLake table with an ordinary heap table must return the same rows
-- with and without duckdb.force_execution. When pg_duckdb is co-installed (the coexistence
-- run) and loads after pg_ducklake, its planner hook used to plan such a query on its own,
-- read the DuckLake table as its empty PostgreSQL placeholder, and silently return no rows.

CREATE TABLE lake (id int, name text) USING ducklake;
INSERT INTO lake SELECT g, 'r' || g FROM generate_series(1, 1000) g;
CREATE TABLE heap AS SELECT g AS id FROM generate_series(1, 200) g;

SELECT count(*) FROM heap h JOIN lake l ON l.id = h.id;

SET duckdb.force_execution = true;
SELECT count(*) FROM heap h JOIN lake l ON l.id = h.id;
SELECT count(*) FROM lake;
SELECT count(*) FROM heap;
RESET duckdb.force_execution;

DROP TABLE lake;
DROP TABLE heap;
