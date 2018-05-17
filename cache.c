#include "cache.h"
#include "util.h"
#include "target.h"
#include "rabs.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gc.h>

#define new(T) ((T *)GC_MALLOC(sizeof(T)))

static sqlite3 *Cache;
static sqlite3_stmt *HashGetStatement;
static sqlite3_stmt *HashSetStatement;
static sqlite3_stmt *HashBuildGetStatement;
static sqlite3_stmt *HashBuildSetStatement;
static sqlite3_stmt *LastCheckSetStatement;
static sqlite3_stmt *DependsGetStatement;
static sqlite3_stmt *DependsDeleteStatement;
static sqlite3_stmt *DependsInsertStatement;
static sqlite3_stmt *ScanGetStatement;
static sqlite3_stmt *ScanDeleteStatement;
static sqlite3_stmt *ScanInsertStatement;
static sqlite3_stmt *ExprGetStatement;
static sqlite3_stmt *ExprSetStatement;
int CurrentVersion = 0;

static int version_callback(void *Data, int NumCols, char **Values, char **Names) {
	CurrentVersion = atoi(Values[0] ?: "0");
	return 0;
}

void cache_open(const char *RootPath) {
	const char *CacheFileName = concat(RootPath, "/", SystemName, ".db", NULL);
	if (sqlite3_open_v2(CacheFileName, &Cache, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_exec(Cache, "PRAGMA locking_mode = EXCLUSIVE", 0, 0, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_exec(Cache, "PRAGMA journal_mode = PERSIST", 0, 0, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_exec(Cache, "PRAGMA synchronous = OFF", 0, 0, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_exec(Cache, "CREATE TABLE IF NOT EXISTS info(key TEXT PRIMARY KEY, value);", 0, 0, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_exec(Cache, "CREATE TABLE IF NOT EXISTS hashes(id TEXT PRIMARY KEY, last_updated INTEGER, last_checked INTEGER, hash BLOB, file_time INTEGER, build BLOB);", 0, 0, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_exec(Cache, "CREATE INDEX IF NOT EXISTS hashes_idx ON hashes(id);", 0, 0, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_exec(Cache, "CREATE TABLE IF NOT EXISTS builds(id TEXT PRIMARY KEY, build BLOB);", 0, 0, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_exec(Cache, "CREATE INDEX IF NOT EXISTS builds_idx ON builds(id);", 0, 0, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_exec(Cache, "CREATE TABLE IF NOT EXISTS scans(id TEXT, scan TEXT);", 0, 0, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_exec(Cache, "CREATE INDEX IF NOT EXISTS scans_idx ON scans(id);", 0, 0, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_exec(Cache, "CREATE TABLE IF NOT EXISTS depends(id TEXT, depend TEXT);", 0, 0, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_exec(Cache, "CREATE INDEX IF NOT EXISTS depends_idx ON depends(id);", 0, 0, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_exec(Cache, "CREATE TABLE IF NOT EXISTS exprs(id TEXT PRIMARY KEY, value TEXT);", 0, 0, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_prepare_v2(Cache, "SELECT hash, last_updated, last_checked, file_time FROM hashes WHERE id = ?", -1, &HashGetStatement, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_prepare_v2(Cache, "REPLACE INTO hashes(id, hash, last_updated, last_checked, file_time) VALUES(?, ?, ?, ?, ?)", -1, &HashSetStatement, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_prepare_v2(Cache, "SELECT build FROM builds WHERE id = ?", -1, &HashBuildGetStatement, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_prepare_v2(Cache, "REPLACE INTO builds(id, build) VALUES(?, ?)", -1, &HashBuildSetStatement, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_prepare_v2(Cache, "UPDATE hashes SET last_checked = ? WHERE id = ?", -1, &LastCheckSetStatement, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_prepare_v2(Cache, "SELECT scan FROM scans WHERE id = ?", -1, &ScanGetStatement, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_prepare_v2(Cache, "DELETE FROM scans WHERE id = ?", -1, &ScanDeleteStatement, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_prepare_v2(Cache, "INSERT INTO scans(id, scan) VALUES(?, ?)", -1, &ScanInsertStatement, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_prepare_v2(Cache, "SELECT depend FROM depends WHERE id = ?", -1, &DependsGetStatement, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_prepare_v2(Cache, "DELETE FROM depends WHERE id = ?", -1, &DependsDeleteStatement, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_prepare_v2(Cache, "INSERT INTO depends(id, depend) VALUES(?, ?)", -1, &DependsInsertStatement, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_prepare_v2(Cache, "SELECT value FROM exprs WHERE id = ?", -1, &ExprGetStatement, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_prepare_v2(Cache, "REPLACE INTO exprs(id, value) VALUES(?, ?)", -1, &ExprSetStatement, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_exec(Cache, "SELECT value FROM info WHERE key = \'version\'", version_callback, 0, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	++CurrentVersion;
	printf("CurrentVersion = %d\n", CurrentVersion);
	atexit(cache_close);
}

void cache_close() {
	char Buffer[100];
	sprintf(Buffer, "REPLACE INTO info(key, value) VALUES('version', %d)", CurrentVersion);
	if (sqlite3_exec(Cache, Buffer, 0, 0, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	sqlite3_finalize(HashGetStatement);
	sqlite3_finalize(HashSetStatement);
	sqlite3_finalize(DependsGetStatement);
	sqlite3_finalize(DependsDeleteStatement);
	sqlite3_finalize(DependsInsertStatement);
	sqlite3_finalize(ScanGetStatement);
	sqlite3_finalize(ScanDeleteStatement);
	sqlite3_finalize(ScanInsertStatement);
	sqlite3_close(Cache);
}


void cache_hash_get(const char *Id, int *LastUpdated, int *LastChecked, time_t *FileTime, BYTE Hash[SHA256_BLOCK_SIZE]) {
	sqlite3_bind_text(HashGetStatement, 1, Id, -1, SQLITE_STATIC);
	if (sqlite3_step(HashGetStatement) == SQLITE_ROW) {
		memcpy(Hash, sqlite3_column_blob(HashGetStatement, 0), SHA256_BLOCK_SIZE);
		*LastUpdated = sqlite3_column_int(HashGetStatement, 1);
		*LastChecked = sqlite3_column_int(HashGetStatement, 2);
		*FileTime = sqlite3_column_int(HashGetStatement, 3);
	} else {
		memset(Hash, 0, SHA256_BLOCK_SIZE);
		*LastUpdated = 0;
		*LastChecked = 0;
		*FileTime = 0;
	}
	sqlite3_reset(HashGetStatement);
}

void cache_hash_set(const char *Id, time_t FileTime, BYTE Hash[SHA256_BLOCK_SIZE]) {
	sqlite3_bind_text(HashSetStatement, 1, Id, -1, SQLITE_STATIC);
	sqlite3_bind_blob(HashSetStatement, 2, Hash, SHA256_BLOCK_SIZE, SQLITE_STATIC);
	sqlite3_bind_int(HashSetStatement, 3, CurrentVersion);
	sqlite3_bind_int(HashSetStatement, 4, CurrentVersion);
	sqlite3_bind_int(HashSetStatement, 5, FileTime);
	sqlite3_step(HashSetStatement);
	sqlite3_reset(HashSetStatement);
}

void cache_build_hash_get(const char *Id, BYTE Hash[SHA256_BLOCK_SIZE]) {
	sqlite3_bind_text(HashBuildGetStatement, 1, Id, -1, SQLITE_STATIC);
	if (sqlite3_step(HashBuildGetStatement) == SQLITE_ROW) {
		memcpy(Hash, sqlite3_column_blob(HashBuildGetStatement, 0), SHA256_BLOCK_SIZE);
	} else {
		memset(Hash, 0, SHA256_BLOCK_SIZE);
	}
	sqlite3_reset(HashBuildGetStatement);
}

void cache_build_hash_set(const char *Id, BYTE Hash[SHA256_BLOCK_SIZE]) {
	sqlite3_bind_text(HashBuildSetStatement, 1, Id, -1, SQLITE_STATIC);
	sqlite3_bind_blob(HashBuildSetStatement, 2, Hash, SHA256_BLOCK_SIZE, SQLITE_STATIC);
	sqlite3_step(HashBuildSetStatement);
	sqlite3_reset(HashBuildSetStatement);
}

void cache_last_check_set(const char *Id) {
	sqlite3_bind_int(LastCheckSetStatement, 1, CurrentVersion);
	sqlite3_bind_text(LastCheckSetStatement, 2, Id, -1, SQLITE_STATIC);
	sqlite3_step(LastCheckSetStatement);
	sqlite3_reset(LastCheckSetStatement);
}

stringmap_t *cache_depends_get(const char *Id) {
	sqlite3_bind_text(DependsGetStatement, 1, Id, -1, SQLITE_STATIC);
	stringmap_t *Depends = 0;
	while (sqlite3_step(DependsGetStatement) == SQLITE_ROW) {
		if (Depends == 0) Depends = new(stringmap_t);
		const char *DependId = sqlite3_column_text(DependsGetStatement, 0);
		target_t *Depend = target_find(DependId);
		if (Depend) stringmap_insert(Depends, Depend->Id, Depend);
	}
	sqlite3_reset(DependsGetStatement);
	return Depends;
}

int cache_depends_set_fn(const char *Id, target_t *Target, void *Arg) {
	sqlite3_bind_text(DependsInsertStatement, 2, Id, -1, SQLITE_STATIC);
	sqlite3_step(DependsInsertStatement);
	sqlite3_reset(DependsInsertStatement);
	return 0;
}

void cache_depends_set(const char *Id, stringmap_t *Depends) {
	sqlite3_exec(Cache, "BEGIN TRANSACTION", 0, 0, 0);
	sqlite3_bind_text(DependsDeleteStatement, 1, Id, -1, SQLITE_STATIC);
	sqlite3_step(DependsDeleteStatement);
	sqlite3_reset(DependsDeleteStatement);
	sqlite3_bind_text(DependsInsertStatement, 1, Id, -1, SQLITE_STATIC);
	stringmap_foreach(Depends, 0, (void *)cache_depends_set_fn);
	sqlite3_exec(Cache, "COMMIT TRANSACTION", 0, 0, 0);
}

stringmap_t *cache_scan_get(const char *Id) {
	sqlite3_bind_text(ScanGetStatement, 1, Id, -1, SQLITE_STATIC);
	stringmap_t *Scans = 0;
	while (sqlite3_step(ScanGetStatement) == SQLITE_ROW) {
		if (Scans == 0) Scans = new(stringmap_t);
		const char *ScanId = sqlite3_column_text(ScanGetStatement, 0);
		target_t *Target = target_find(ScanId);
		if (Target) stringmap_insert(Scans, Target->Id, Target);
	}
	sqlite3_reset(ScanGetStatement);
	return Scans;
}

int cache_scan_set_fn(const char *Id, target_t *Target, void *Arg) {
	sqlite3_bind_text(ScanInsertStatement, 2, Target->Id, -1, SQLITE_STATIC);
	sqlite3_step(ScanInsertStatement);
	sqlite3_reset(ScanInsertStatement);
	return 0;
}

void cache_scan_set(const char *Id, stringmap_t *Scans) {
	sqlite3_exec(Cache, "BEGIN TRANSACTION", 0, 0, 0);
	sqlite3_bind_text(ScanDeleteStatement, 1, Id, -1, SQLITE_STATIC);
	sqlite3_step(ScanDeleteStatement);
	sqlite3_reset(ScanDeleteStatement);
	sqlite3_bind_text(ScanInsertStatement, 1, Id, -1, SQLITE_STATIC);
	stringmap_foreach(Scans, 0, (void *)cache_scan_set_fn);
	sqlite3_exec(Cache, "COMMIT TRANSACTION", 0, 0, 0);
}

ml_value_t *cache_expr_get(const char *Id) {
	sqlite3_bind_text(ExprGetStatement, 1, Id, -1, SQLITE_STATIC);
	ml_value_t *Result = 0;
	if (sqlite3_step(ExprGetStatement) == SQLITE_ROW) {
		switch (sqlite3_column_type(ExprGetStatement, 0)) {
		case SQLITE_TEXT: {
			int Length = sqlite3_column_bytes(ExprGetStatement, 0);
			char *Chars = snew(Length + 1);
			memcpy(Chars, sqlite3_column_text(ExprGetStatement, 0), Length);
			Chars[Length] = 0;
			Result = ml_string(Chars, Length);
			break;
		}
		case SQLITE_INTEGER:
			Result = ml_integer(sqlite3_column_int64(ExprGetStatement, 0));
			break;
		case SQLITE_FLOAT:
			Result = ml_real(sqlite3_column_double(ExprGetStatement, 0));
			break;
		case SQLITE_NULL:
			Result = MLNil;
			break;
		}
	}
	sqlite3_reset(ExprGetStatement);
	return Result;
}

void cache_expr_set(const char *Id, ml_value_t *Value) {
	if (Value->Type == MLStringT) {
		sqlite3_bind_text(ExprSetStatement, 1, Id, -1, SQLITE_STATIC);
		sqlite3_bind_text(ExprSetStatement, 2, ml_string_value(Value), ml_string_length(Value), SQLITE_STATIC);
	} else if (Value->Type == MLIntegerT) {
		sqlite3_bind_int64(ExprSetStatement, 2, ml_integer_value(Value));
	} else if (Value->Type == MLRealT) {
		sqlite3_bind_double(ExprSetStatement, 2, ml_real_value(Value));
	} else if (Value == MLNil) {
		sqlite3_bind_null(ExprSetStatement, 2);
	}
	sqlite3_step(ExprSetStatement);
	sqlite3_reset(ExprSetStatement);
}
