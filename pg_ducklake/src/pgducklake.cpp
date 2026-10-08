/*
 * pgducklake.cpp -- PostgreSQL extension bootstrap entry points.
 */

#include "pgducklake/constants.hpp"
#include "pgducklake/duckdb_manager.hpp"
#include "pgducklake/direct_insert/native_inline_writer.hpp"
#include "pgducklake/direct_insert/native_writer_queue.hpp"
#include "pgducklake/pgducklake_metadata_manager.hpp"

#include <storage/ducklake_metadata_manager.hpp>

#include "pgddb/pgddb_node.hpp"
#include "pgddb/utility/cpp_wrapper.hpp"

extern "C" {
#include "postgres.h"

#include "commands/extension.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "nodes/pg_list.h"
#include "utils/guc.h"
#include "utils/varlena.h"
}

namespace pgducklake {

// Bootstrap entry points wired up by _PG_init; declared here since it is their only caller.
void InitGUCs();
void InitMaintenanceWorker();
void InitDirectInsertStatsShmem();
void InitDuckDBManager();
void RegisterDirectInsertNode();
void InitTableAmHook();
void InitHooks();
void InitRuleutilsHooks();
void InitTypeHooks();
void RegisterXactCallback();
void InitFDW();
void InitSecrets();

} // namespace pgducklake

extern "C" {

#ifdef PG_MODULE_MAGIC_EXT
#ifndef PG_DUCKLAKE_VERSION
// Fallback for static analysis; normally defined by the build system.
#define PG_DUCKLAKE_VERSION "unknown"
#endif
PG_MODULE_MAGIC_EXT(.name = "pg_ducklake", .version = PG_DUCKLAKE_VERSION);
#else
PG_MODULE_MAGIC;
#endif

/*
 * Our planner hook only runs ahead of pg_duckdb's if it is installed after it (hooks chain LIFO).
 * With the opposite shared_preload_libraries order, pg_duckdb's hook runs first and, under
 * duckdb.force_execution, plans a query that mentions a DuckLake table itself, through its own
 * copy of the name resolution that knows nothing about the DuckLake catalog: the table is read as
 * its empty PostgreSQL placeholder and the query silently returns no rows. Load pg_duckdb first
 * whenever it is preloaded, so the order of the list no longer matters.
 */
static void
LoadPgDuckdbBeforeHooks() {
	if (!process_shared_preload_libraries_in_progress)
		return;
	const char *preload = GetConfigOption("shared_preload_libraries", true, false);
	if (!preload || !*preload)
		return;

	char *raw = pstrdup(preload);
	List *elems = NIL;
	if (!SplitDirectoriesString(raw, ',', &elems)) {
		pfree(raw);
		return;
	}
	bool listed = false;
	ListCell *lc;
	foreach (lc, elems) {
		const char *name = (const char *)lfirst(lc);
		const char *slash = strrchr(name, '/');
		if (slash)
			name = slash + 1;
		if (strcmp(name, "pg_duckdb") == 0 || strcmp(name, "pg_duckdb.so") == 0) {
			listed = true;
			break;
		}
	}
	list_free_deep(elems);
	pfree(raw);
	if (listed)
		load_file("pg_duckdb", false);
}

void
_PG_init(void) {
	LoadPgDuckdbBeforeHooks();
	// Register metadata manager factory in DuckLake's process-global registry.
	duckdb::DuckLakeMetadataManager::Register(PGDUCKLAKE_DUCKDB_CATALOG, pgducklake::PgDuckLakeMetadataManager::Create);
	pgducklake::InitGUCs();
	pgducklake::InitMaintenanceWorker();
	pgducklake::InitDirectInsertStatsShmem();
	pgducklake::InitNativeWriterStatsShmem();
	pgducklake::InitNativeWriterQueueShmem();
	pgducklake::InitDuckDBManager();
	pgddb::InitNode("DuckLakeScan");
	pgducklake::RegisterDirectInsertNode();
	pgducklake::InitTableAmHook();
	pgducklake::InitHooks();
	pgducklake::InitRuleutilsHooks();
	pgducklake::InitTypeHooks();
	// Mirror PG transaction events to DuckDB's DuckLake transaction.
	pgducklake::RegisterXactCallback();
	pgducklake::InitFDW();
	pgducklake::InitSecrets();
}

/*
 * ducklake_initialize() -- SQL bootstrap run once during CREATE EXTENSION. It
 * forces DuckDB init (whose OnPostInit attaches the pgducklake DuckLake
 * catalog) and, on DROP+CREATE within one backend, re-attaches it.
 */
DECLARE_PG_FUNCTION(ducklake_initialize) {
	elog(LOG, "ducklake_initialize() called");

	if (!creating_extension) {
		ereport(ERROR,
		        (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("ducklake_initialize() can only be called during "
		                                                          "CREATE EXTENSION")));
	}

	if (pgducklake::PgDuckLakeMetadataManager::IsInitialized()) {
		ereport(ERROR,
		        (errcode(ERRCODE_DUPLICATE_SCHEMA), errmsg("DuckLake reserved schema \"ducklake\" is already in use")));
	}

	// First CREATE: SELECT 1 triggers Initialize() -> OnPostInit() -> attach. On
	// DROP+CREATE in one backend DuckDB is already alive, so OnPostInit does not
	// re-run and we must re-attach (the catalog was detached during DROP).
	bool duckdb_already_initialized = pgducklake::DuckDBManager::IsInitialized();

	pgducklake::DuckDBQueryOrThrow("SELECT 1");

	if (duckdb_already_initialized) {
		ducklake_attach_catalog();
	}

	PG_RETURN_VOID();
}

} // extern "C"
