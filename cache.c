#include "cache.h"
#include "util.h"
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
static sqlite3_stmt *DependsSetStatement;
static sqlite3_stmt *ScanGetStatement;
static sqlite3_stmt *ScanSetStatement;
static sqlite3_stmt *ExprGetStatement;
static sqlite3_stmt *ExprSetStatement;
int CurrentVersion = 0;

static int version_callback(void *Data, int NumCols, char **Values, char **Names) {
	CurrentVersion = atoi(Values[0] ?: "0");
	return 0;
}

void cache_open(const char *RootPath) {
	sqlite3_config(SQLITE_CONFIG_SINGLETHREAD);
	const char *CacheFileName = concat(RootPath, "/", SystemName, ".db", NULL);
	if (sqlite3_open_v2(CacheFileName, &Cache, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_exec(Cache, "PRAGMA locking_mode = EXCLUSIVE", 0, 0, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_exec(Cache, "PRAGMA journal_mode = MEMORY", 0, 0, 0) != SQLITE_OK) {
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
	if (sqlite3_exec(Cache, "CREATE TABLE IF NOT EXISTS builds(id TEXT PRIMARY KEY, build BLOB);", 0, 0, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_exec(Cache, "CREATE TABLE IF NOT EXISTS scans(id TEXT PRIMARY KEY, scan BLOB);", 0, 0, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_exec(Cache, "CREATE TABLE IF NOT EXISTS depends(id TEXT PRIMARY KEY, depend BLOB);", 0, 0, 0) != SQLITE_OK) {
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
	if (sqlite3_prepare_v2(Cache, "REPLACE INTO scans(id, scan) VALUES(?, ?)", -1, &ScanSetStatement, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_prepare_v2(Cache, "SELECT depend FROM depends WHERE id = ?", -1, &DependsGetStatement, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_prepare_v2(Cache, "REPLACE INTO depends(id, depend) VALUES(?, ?)", -1, &DependsSetStatement, 0) != SQLITE_OK) {
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
	printf("Build iteration = %d\n", CurrentVersion);
	char Buffer[100];
	sprintf(Buffer, "REPLACE INTO info(key, value) VALUES('version', %d)", CurrentVersion);
	if (sqlite3_exec(Cache, Buffer, 0, 0, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	atexit(cache_close);
}

void cache_close() {
	sqlite3_finalize(HashGetStatement);
	sqlite3_finalize(HashSetStatement);
	sqlite3_finalize(HashBuildGetStatement);
	sqlite3_finalize(HashBuildSetStatement);
	sqlite3_finalize(LastCheckSetStatement);
	sqlite3_finalize(DependsGetStatement);
	sqlite3_finalize(DependsSetStatement);
	sqlite3_finalize(ScanGetStatement);
	sqlite3_finalize(ScanSetStatement);
	sqlite3_finalize(ExprGetStatement);
	sqlite3_finalize(ExprSetStatement);
	sqlite3_close(Cache);
}

void cache_bump_version() {
	++CurrentVersion;
	printf("CurrentVersion = %d\n", CurrentVersion);
	char Buffer[100];
	sprintf(Buffer, "REPLACE INTO info(key, value) VALUES('version', %d)", CurrentVersion);
	if (sqlite3_exec(Cache, Buffer, 0, 0, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
}

void cache_hash_get(target_t *Target, int *LastUpdated, int *LastChecked, time_t *FileTime, BYTE Hash[SHA256_BLOCK_SIZE]) {
	sqlite3_bind_text(HashGetStatement, 1, Target->Id, Target->IdLength, SQLITE_STATIC);
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

void cache_hash_set(target_t *Target, time_t FileTime) {
	sqlite3_bind_text(HashSetStatement, 1, Target->Id, Target->IdLength, SQLITE_STATIC);
	sqlite3_bind_blob(HashSetStatement, 2, Target->Hash, SHA256_BLOCK_SIZE, SQLITE_STATIC);
	sqlite3_bind_int(HashSetStatement, 3, Target->LastUpdated);
	sqlite3_bind_int(HashSetStatement, 4, CurrentVersion);
	sqlite3_bind_int(HashSetStatement, 5, FileTime);
	sqlite3_step(HashSetStatement);
	sqlite3_reset(HashSetStatement);
}

void cache_build_hash_get(target_t *Target, BYTE Hash[SHA256_BLOCK_SIZE]) {
	sqlite3_bind_text(HashBuildGetStatement, 1, Target->Id, Target->IdLength, SQLITE_STATIC);
	if (sqlite3_step(HashBuildGetStatement) == SQLITE_ROW) {
		memcpy(Hash, sqlite3_column_blob(HashBuildGetStatement, 0), SHA256_BLOCK_SIZE);
	} else {
		memset(Hash, 0, SHA256_BLOCK_SIZE);
	}
	sqlite3_reset(HashBuildGetStatement);
}

void cache_build_hash_set(target_t *Target, BYTE BuildHash[SHA256_BLOCK_SIZE]) {
	sqlite3_bind_text(HashBuildSetStatement, 1, Target->Id, Target->IdLength, SQLITE_STATIC);
	sqlite3_bind_blob(HashBuildSetStatement, 2, BuildHash, SHA256_BLOCK_SIZE, SQLITE_STATIC);
	sqlite3_step(HashBuildSetStatement);
	sqlite3_reset(HashBuildSetStatement);
}

void cache_last_check_set(target_t *Target, time_t FileTime) {
	sqlite3_bind_int(LastCheckSetStatement, 1, CurrentVersion);
	sqlite3_bind_text(LastCheckSetStatement, 2, Target->Id, Target->IdLength, SQLITE_STATIC);
	sqlite3_step(LastCheckSetStatement);
	sqlite3_reset(LastCheckSetStatement);
}

#define CACHE_LIST_SIZE 62

typedef struct cache_list_t cache_list_t;

struct cache_list_t {
	cache_list_t *Next;
	const char *Ids[CACHE_LIST_SIZE + 1];
};

static cache_list_t *CacheLists = 0;

static cache_list_t *cache_list_parse(const char *Ids, int *Total) {
	cache_list_t *List = 0;
	int I = CACHE_LIST_SIZE, N = 0;
	const char *Id = Ids;
	while (*Id) {
		size_t Length = strlen(Id) + 1;
		char *Copy = snew(Length);
		memcpy(Copy, Id, Length);
		Id += Length;
		if (I == CACHE_LIST_SIZE) {
			cache_list_t *Temp = CacheLists ?: new(cache_list_t);
			CacheLists = Temp->Next;
			Temp->Next = List;
			List = Temp;
			I = 0;
		}
		List->Ids[I] = Copy;
		++I;
		++N;
	}
	if (List) List->Ids[I] = 0;
	*Total = N;
	return List;
}

static targetset_t *cache_list_to_set(cache_list_t *List, int Total) {
	targetset_t *Set = new(targetset_t);
	targetset_init(Set, Total);
	while (List) {
		for (const char **Temp = List->Ids; *Temp; ++Temp) {
			target_t *Target = target_find(*Temp);
			if (Target) targetset_insert(Set, Target);
		}
		cache_list_t *Next = List->Next;
		List->Next = CacheLists;
		CacheLists = List;
		List = Next;
	}
	return Set;
}

static int cache_target_set_size(target_t *Target, int *Size) {
	*Size += Target->IdLength + 1;
	return 0;
}

static int cache_target_set_append(target_t *Target, char **Buffer) {
	*Buffer = stpcpy(*Buffer, Target->Id) + 1;
	return 0;
}

targetset_t *cache_depends_get(target_t *Target) {
	sqlite3_bind_text(DependsGetStatement, 1, Target->Id, Target->IdLength, SQLITE_STATIC);
	int Total = 0;
	cache_list_t *List = 0;
	if (sqlite3_step(DependsGetStatement) == SQLITE_ROW) {
		List = cache_list_parse(sqlite3_column_blob(DependsGetStatement, 0), &Total);
	}
	sqlite3_reset(DependsGetStatement);
	return cache_list_to_set(List, Total);
}

void cache_depends_set(target_t *Target, targetset_t *Depends) {
	sqlite3_exec(Cache, "BEGIN TRANSACTION", 0, 0, 0);
	int Size = 1;
	targetset_foreach(Depends, &Size, (void *)cache_target_set_size);
	char *Buffer = snew(Size);
	char *Next = Buffer;
	targetset_foreach(Depends, &Next, (void *)cache_target_set_append);
	*Next = 0;
	sqlite3_bind_text(DependsSetStatement, 1, Target->Id, Target->IdLength, SQLITE_STATIC);
	sqlite3_bind_blob(DependsSetStatement, 2, Buffer, Size, SQLITE_STATIC);
	sqlite3_step(DependsSetStatement);
	sqlite3_reset(DependsSetStatement);
	sqlite3_exec(Cache, "COMMIT TRANSACTION", 0, 0, 0);
}

targetset_t *cache_scan_get(target_t *Target) {
	sqlite3_bind_text(ScanGetStatement, 1, Target->Id, Target->IdLength, SQLITE_STATIC);
	int Total = 0;
	cache_list_t *List = 0;
	if (sqlite3_step(ScanGetStatement) == SQLITE_ROW) {
		List = cache_list_parse(sqlite3_column_blob(ScanGetStatement, 0), &Total);
	}
	sqlite3_reset(ScanGetStatement);
	return cache_list_to_set(List, Total);
}

void cache_scan_set(target_t *Target, targetset_t *Scans) {
	sqlite3_exec(Cache, "BEGIN TRANSACTION", 0, 0, 0);
	int Size = 1;
	targetset_foreach(Scans, &Size, (void *)cache_target_set_size);
	char *Buffer = snew(Size);
	char *Next = Buffer;
	targetset_foreach(Scans, &Next, (void *)cache_target_set_append);
	*Next = 0;
	sqlite3_bind_text(ScanSetStatement, 1, Target->Id, Target->IdLength, SQLITE_STATIC);
	sqlite3_bind_blob(ScanSetStatement, 2, Buffer, Size, SQLITE_STATIC);
	sqlite3_step(ScanSetStatement);
	sqlite3_reset(ScanSetStatement);
	sqlite3_exec(Cache, "COMMIT TRANSACTION", 0, 0, 0);
}

ml_value_t *cache_expr_get(target_t *Target) {
	sqlite3_bind_text(ExprGetStatement, 1, Target->Id, Target->IdLength, SQLITE_STATIC);
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

void cache_expr_set(target_t *Target, ml_value_t *Value) {
	if (Value->Type == MLStringT) {
		sqlite3_bind_text(ExprSetStatement, 1, Target->Id, Target->IdLength, SQLITE_STATIC);
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
