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
#define anew(T, N) ((T *)GC_MALLOC((N) * sizeof(T)))
#define snew(N) ((char *)GC_MALLOC_ATOMIC(N))
#define xnew(T, N, U) ((T *)GC_MALLOC(sizeof(T) + (N) * sizeof(U)))

static sqlite3 *Cache;
static sqlite3_stmt *HashGetStatement;
static sqlite3_stmt *HashSetStatement;
static sqlite3_stmt *LastCheckSetStatement;
static sqlite3_stmt *DependsGetStatement;
static sqlite3_stmt *DependsDeleteStatement;
static sqlite3_stmt *DependsInsertStatement;
static sqlite3_stmt *ScanGetStatement;
static sqlite3_stmt *ScanDeleteStatement;
static sqlite3_stmt *ScanInsertStatement;
int CurrentVersion = 1;

static int version_callback(void *Data, int NumCols, char **Values, char **Names) {
	CurrentVersion = atoi(Values[0] ?: "0");
	return 0;
}

void cache_open(const char *RootPath) {
	const char *CacheFileName = concat(RootPath, "/", SystemName, ".db", 0);
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
	if (sqlite3_exec(Cache, "CREATE TABLE IF NOT EXISTS hashes(id TEXT PRIMARY KEY, last_updated INTEGER, last_checked INTEGER, hash BLOB, file_time INTEGER);", 0, 0, 0) != SQLITE_OK) {
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
	if (sqlite3_prepare_v2(Cache, "SELECT hash, last_updated, last_checked, file_time FROM hashes WHERE id = ?", -1, &HashGetStatement, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_prepare_v2(Cache, "REPLACE INTO hashes(id, hash, last_updated, last_checked, file_time) VALUES(?, ?, ?, ?, ?)", -1, &HashSetStatement, 0) != SQLITE_OK) {
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


void cache_hash_get(const char *Id, int *LastUpdated, int *LastChecked, time_t *FileTime, BYTE Digest[SHA256_BLOCK_SIZE]) {
	sqlite3_bind_text(HashGetStatement, 1, Id, -1, SQLITE_STATIC);
	int Version = 0;
	if (sqlite3_step(HashGetStatement) == SQLITE_ROW) {
		memcpy(Digest, sqlite3_column_blob(HashGetStatement, 0), SHA256_BLOCK_SIZE);
		*LastUpdated = sqlite3_column_int(HashGetStatement, 1);
		*LastChecked = sqlite3_column_int(HashGetStatement, 2);
		*FileTime = sqlite3_column_int(HashGetStatement, 3);
	} else {
		memset(Digest, 0, SHA256_BLOCK_SIZE);
		*LastUpdated = 0;
		*LastChecked = 0;
		*FileTime = 0;
	}
	sqlite3_reset(HashGetStatement);
}

void cache_hash_set(const char *Id, time_t FileTime, BYTE Digest[SHA256_BLOCK_SIZE]) {
	sqlite3_bind_text(HashSetStatement, 1, Id, -1, SQLITE_STATIC);
	sqlite3_bind_blob(HashSetStatement, 2, Digest, SHA256_BLOCK_SIZE, SQLITE_STATIC);
	sqlite3_bind_int(HashSetStatement, 3, CurrentVersion);
	sqlite3_bind_int(HashSetStatement, 4, CurrentVersion);
	sqlite3_bind_int(HashSetStatement, 5, FileTime);
	sqlite3_step(HashSetStatement);
	sqlite3_reset(HashSetStatement);
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
		stringmap_insert(Depends, DependId, target_find(DependId));
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
		stringmap_insert(Scans, ScanId, target_find(ScanId));
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
