-- ducklake.require_table_path refuses to create a table with no data path of its own, instead of
-- silently storing its Parquet files in the PostgreSQL data directory. Existing behaviour (off) is
-- unchanged, and the setting is superuser-only.

SHOW ducklake.require_table_path;
SET ducklake.require_table_path = on;

-- No path anywhere: refused, for CREATE TABLE and CTAS alike.
CREATE TABLE rtp_none (id int) USING ducklake;
CREATE TABLE rtp_ctas USING ducklake AS SELECT 1 AS id;

-- A session default path, or a per-table path, satisfies it.
SET ducklake.default_table_path = '/tmp/pg_ducklake_testdata/rtp_default/';
CREATE TABLE rtp_default (id int) USING ducklake;
RESET ducklake.default_table_path;
CREATE TABLE rtp_own (id int) USING ducklake WITH (ducklake.table_path = '/tmp/pg_ducklake_testdata/rtp_own/');

RESET ducklake.require_table_path;
CREATE TABLE rtp_off (id int) USING ducklake;

SELECT count(*) FROM pg_class WHERE relname LIKE 'rtp\_%' AND relam = (SELECT oid FROM pg_am WHERE amname = 'ducklake');

DROP TABLE rtp_default, rtp_own, rtp_off;

-- Superuser-only.
CREATE ROLE rtp_user;
SET ROLE rtp_user;
SET ducklake.require_table_path = on;
RESET ROLE;
DROP ROLE rtp_user;
