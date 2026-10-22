-- ducklake.add_data_files registers existing Parquet files in a table's catalog without copying them:
-- the rows become visible, and the file stays exactly where it is.

CREATE TABLE af_src (id int, v text) USING ducklake;
CALL ducklake.set_option('data_inlining_row_limit', 0, 'af_src'::regclass);
INSERT INTO af_src SELECT g, 'v' || g FROM generate_series(1, 1000) g;

SELECT r['data_file']::text AS src_file FROM ducklake.list_files('af_src'::regclass) r LIMIT 1 \gset

CREATE TABLE af_dst (id int, v text) USING ducklake;
SELECT count(*), min(id) FROM af_dst;

-- text form, then the regclass form with a list of files.
SELECT count(*) FROM ducklake.add_data_files('public', 'af_dst', :'src_file');
SELECT count(*), min(id), max(id), sum(id) FROM af_dst;

-- The registered file is the source's own file, not a copy.
SELECT count(DISTINCT r['data_file']::text) AS distinct_files,
       bool_and(r['data_file']::text = :'src_file') AS same_path
FROM ducklake.list_files('af_dst'::regclass) r;

-- Dropping the table that merely references the file leaves the source readable.
DROP TABLE af_dst;
SELECT count(*), sum(id) FROM af_src;

-- Errors (messages carry data-file names, so only report that the call was rejected): missing table,
-- and a file whose columns do not match the table.
SELECT set_config('af.file', :'src_file', false) IS NOT NULL AS stashed;
DO $$
BEGIN
    PERFORM count(*) FROM ducklake.add_data_files('public', 'no_such_table', current_setting('af.file'));
EXCEPTION WHEN OTHERS THEN
    RAISE NOTICE 'missing table rejected';
END $$;
CREATE TABLE af_bad (id int, other text, extra int) USING ducklake;
DO $$
BEGIN
    PERFORM count(*) FROM ducklake.add_data_files('af_bad'::regclass, ARRAY[current_setting('af.file')]);
EXCEPTION WHEN OTHERS THEN
    RAISE NOTICE 'mismatched columns rejected';
END $$;
SELECT count(*) FROM af_bad;

DROP TABLE af_bad;
DROP TABLE af_src;
