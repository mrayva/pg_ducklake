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

void
_PG_init(void) {
	// Register metadata manager factory in DuckLake's process-global registry.
	duckdb::DuckLakeMetadataManager::Register(PGDUCKLAKE_DUCKDB_CATALOG, pgducklake::PgDuckLakeMetadataManager::Create);
	/* Metadata-manager SPI calls (pgducklake_metadata_manager.cpp) run on whatever
	 * thread DuckDB schedules the originating statement on; DuckDB may hand any
	 * query, not just postgres scans, to one of its own task threads. A pgrx
	 * extension's hook firing on such a thread (vchord, pg_parquet, pg_ripple
	 * observed; others are unaudited) aborts the whole backend, since pgrx asserts
	 * every Postgres FFI call happens on the thread Postgres itself created. Force
	 * single-threaded DuckDB in every process that loads this library -- not just
	 * the maintenance worker, which already did this in its own entry point below
	 * -- so no DuckDB task thread, anywhere, ever calls back into Postgres. This
	 * must run before the first DuckDBManager::Get(), i.e. here in _PG_init, not
	 * later in a GUC or function handler.
	 */
	pgducklake::ForceSingleThreadedDuckDB();
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
