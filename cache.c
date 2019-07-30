#include "cache.h"
#include "util.h"
#include "rabs.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gc/gc.h>
#include <sys/stat.h>

#define new(T) ((T *)GC_MALLOC(sizeof(T)))

static sqlite3 *Cache;
static sqlite3_stmt *HashGetStatement;
static sqlite3_stmt *HashSetStatement;
static sqlite3_stmt *HashBuildGetStatement;
static sqlite3_stmt *HashBuildSetStatement;
static sqlite3_stmt *LastCheckSetStatement;
//static sqlite3_stmt *ParentGetStatement;
//static sqlite3_stmt *ParentSetStatement;
static sqlite3_stmt *DependsGetStatement;
static sqlite3_stmt *DependsSetStatement;
static sqlite3_stmt *ScanGetStatement;
static sqlite3_stmt *ScanSetStatement;
static sqlite3_stmt *ExprGetStatement;
static sqlite3_stmt *ExprSetStatement;
int CurrentIteration = 0;

static int version_callback(char *Result, int NumCols, char **Values, char **Names) {
	strcpy(Result, Values[0] ?: "0.0.0");
	return 0;
}

static int iteration_callback(int *Result, int NumCols, char **Values, char **Names) {
	*Result = atoi(Values[0] ?: "0");
	return 0;
}

void cache_open(const char *RootPath) {
	sqlite3_config(SQLITE_CONFIG_SINGLETHREAD);
	const char *CacheFileName = concat(RootPath, "/", SystemName, ".db", NULL);
	struct stat Stat[1];
	int CreateCache = !!stat(CacheFileName, Stat);
	if (sqlite3_open_v2(CacheFileName, &Cache, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	sqlite3_exec(Cache,
		"PRAGMA locking_mode = EXCLUSIVE;"
		"PRAGMA journal_mode = MEMORY;"
		"PRAGMA synchronous = OFF;",
		0, 0, 0
	);
	if (CreateCache) {
		if (sqlite3_exec(Cache,
			"CREATE TABLE info(key TEXT PRIMARY KEY, value);"
			"INSERT INTO info(key, value) VALUES('version', '" CURRENT_VERSION "');"
			"CREATE TABLE hashes(id TEXT PRIMARY KEY, last_updated INTEGER, last_checked INTEGER, hash BLOB, file_time INTEGER);"
			//"CREATE TABLE parents(id TEXT PRIMARY KEY, parent TEXT);"
			"CREATE TABLE builds(id TEXT PRIMARY KEY, build BLOB);"
			"CREATE TABLE scans(id TEXT PRIMARY KEY, scan TEXT);"
			"CREATE TABLE depends(id TEXT PRIMARY KEY, depend TEXT);"
			"CREATE TABLE exprs(id TEXT PRIMARY KEY, value TEXT);",
			0, 0, 0) != SQLITE_OK
		) {
			printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
			exit(1);
		}
	} else {
		char PreviousVersion[10] = {'0', '.', '0', '.', '0', 0,};
		if (sqlite3_exec(Cache, "SELECT value FROM info WHERE key = \'version\'", (void *)version_callback, &PreviousVersion, 0) != SQLITE_OK) {
			printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
			exit(1);
		}
		if (strcmp(PreviousVersion, WORKING_VERSION) < 0) {
			printf("Version error: database was built with version %s but this version of Rabs will only work with version %s or higher. Delete %s to force a clean build.\n", PreviousVersion, WORKING_VERSION, CacheFileName);
			exit(1);
		}
		if (sqlite3_exec(Cache, "SELECT value FROM info WHERE key = \'iteration\'", (void *)iteration_callback, &CurrentIteration, 0) != SQLITE_OK) {
			printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
			exit(1);
		}
	}
	if (sqlite3_prepare_v2(Cache, "SELECT hash, last_updated, last_checked, file_time FROM hashes WHERE id = ?", -1, &HashGetStatement, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_prepare_v2(Cache, "REPLACE INTO hashes(id, hash, last_updated, last_checked, file_time) VALUES(?, ?, ?, ?, ?)", -1, &HashSetStatement, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	/*if (sqlite3_prepare_v2(Cache, "SELECT parent FROM parents WHERE id = ?", -1, &ParentGetStatement, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_prepare_v2(Cache, "REPLACE INTO parents(id, parent) VALUES(?, ?)", -1, &ParentSetStatement, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}*/
	if (sqlite3_prepare_v2(Cache, "SELECT build FROM builds WHERE id = ?", -1, &HashBuildGetStatement, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_prepare_v2(Cache, "REPLACE INTO builds(id, build) VALUES(?, ?)", -1, &HashBuildSetStatement, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	if (sqlite3_prepare_v2(Cache, "UPDATE hashes SET last_checked = ?, file_time = ? WHERE id = ?", -1, &LastCheckSetStatement, 0) != SQLITE_OK) {
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
	++CurrentIteration;
	printf("Rabs version = %s\n", CURRENT_VERSION);
	printf("Build iteration = %d\n", CurrentIteration);
	char Buffer[100];
	sprintf(Buffer, "REPLACE INTO info(key, value) VALUES('version', '%s')", CURRENT_VERSION);
	if (sqlite3_exec(Cache, Buffer, 0, 0, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	sprintf(Buffer, "REPLACE INTO info(key, value) VALUES('iteration', %d)", CurrentIteration);
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

void cache_bump_iteration() {
	++CurrentIteration;
	printf("Rabs version = %s\n", CURRENT_VERSION);
	printf("Build iteration = %d\n", CurrentIteration);
	char Buffer[100];
	sprintf(Buffer, "REPLACE INTO info(key, value) VALUES('version', '%s')", CURRENT_VERSION);
	if (sqlite3_exec(Cache, Buffer, 0, 0, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
	sprintf(Buffer, "REPLACE INTO info(key, value) VALUES('iteration', %d)", CurrentIteration);
	if (sqlite3_exec(Cache, Buffer, 0, 0, 0) != SQLITE_OK) {
		printf("Sqlite error: %s\n", sqlite3_errmsg(Cache));
		exit(1);
	}
}

void cache_hash_get(target_t *Target, int *LastUpdated, int *LastChecked, time_t *FileTime, unsigned char Hash[SHA256_BLOCK_SIZE]) {
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
	sqlite3_bind_int(HashSetStatement, 4, CurrentIteration);
	sqlite3_bind_int(HashSetStatement, 5, FileTime);
	sqlite3_step(HashSetStatement);
	sqlite3_reset(HashSetStatement);
}

void cache_build_hash_get(target_t *Target, unsigned char Hash[SHA256_BLOCK_SIZE]) {
	sqlite3_bind_text(HashBuildGetStatement, 1, Target->Id, Target->IdLength, SQLITE_STATIC);
	if (sqlite3_step(HashBuildGetStatement) == SQLITE_ROW) {
		memcpy(Hash, sqlite3_column_blob(HashBuildGetStatement, 0), SHA256_BLOCK_SIZE);
	} else {
		memset(Hash, 0, SHA256_BLOCK_SIZE);
	}
	sqlite3_reset(HashBuildGetStatement);
}

void cache_build_hash_set(target_t *Target, unsigned char BuildHash[SHA256_BLOCK_SIZE]) {
	sqlite3_bind_text(HashBuildSetStatement, 1, Target->Id, Target->IdLength, SQLITE_STATIC);
	sqlite3_bind_blob(HashBuildSetStatement, 2, BuildHash, SHA256_BLOCK_SIZE, SQLITE_STATIC);
	sqlite3_step(HashBuildSetStatement);
	sqlite3_reset(HashBuildSetStatement);
}

void cache_last_check_set(target_t *Target, time_t FileTime) {
	sqlite3_bind_int(LastCheckSetStatement, 1, CurrentIteration);
	sqlite3_bind_int(LastCheckSetStatement, 2, FileTime);
	sqlite3_bind_text(LastCheckSetStatement, 3, Target->Id, Target->IdLength, SQLITE_STATIC);
	sqlite3_step(LastCheckSetStatement);
	sqlite3_reset(LastCheckSetStatement);
}

/*void cache_parent_get(target_t *Target) {
	sqlite3_bind_text(ParentGetStatement, 1, Target->Id, Target->IdLength, SQLITE_STATIC);
	if (sqlite3_step(ParentGetStatement) == SQLITE_ROW) {
		const char *Text = sqlite3_column_text(ParentGetStatement, 0);
		int Length = sqlite3_column_bytes(ParentGetStatement, 0);
		if (Length) {
			char *Parent = GC_malloc_atomic(Length);
			memcpy(Parent, Text, Length);
			Target->Parent = target_find(Parent);
		}
	}
	sqlite3_reset(ParentGetStatement);
}

void cache_parent_set(target_t *Target) {
	sqlite3_bind_text(ParentSetStatement, 1, Target->Id, Target->IdLength, SQLITE_STATIC);
	sqlite3_bind_blob(ParentSetStatement, 2, Target->Parent->Id, Target->Parent->IdLength, SQLITE_STATIC);
	sqlite3_step(ParentSetStatement);
	sqlite3_reset(ParentSetStatement);
}*/

static targetset_t *cache_target_set_parse(char *Id) {
	targetset_t *Set = targetset_new();
	if (!Id) return Set;
	int Size = strtol(Id, &Id, 10);
	targetset_init(Set, Size);
	++Id;
	while (*Id > '\n') {
		char *End = Id + 1;
		while (*End > '\n') ++End;
		*End = 0;
		target_t *Target = target_find(Id);
		targetset_insert(Set, Target);
		Id = End + 1;
	}
	return Set;
}

static int cache_target_set_size(target_t *Target, int *Size) {
	*Size += Target->IdLength + 1;
	return 0;
}

static int cache_target_set_append(target_t *Target, char **Buffer) {
	char *Result = stpcpy(*Buffer, Target->Id);
	Result[0] = '\n';
	*Buffer = Result + 1;
	return 0;
}

targetset_t *cache_depends_get(target_t *Target) {
	sqlite3_bind_text(DependsGetStatement, 1, Target->Id, Target->IdLength, SQLITE_STATIC);
	char *Depends = 0;
	if (sqlite3_step(DependsGetStatement) == SQLITE_ROW) {
		const char *Text = (const char *)sqlite3_column_text(DependsGetStatement, 0);
		int Length = sqlite3_column_bytes(DependsGetStatement, 0);
		Depends = GC_malloc_atomic(Length);
		memcpy(Depends, Text, Length);
	}
	sqlite3_reset(DependsGetStatement);
	return cache_target_set_parse(Depends);
}

void cache_depends_set(target_t *Target, targetset_t *Depends) {
	sqlite3_exec(Cache, "BEGIN TRANSACTION", 0, 0, 0);
	char LengthBuffer[16];
	int Size = sprintf(LengthBuffer, "%d\n", Depends->Size - Depends->Space) + 1;
	targetset_foreach(Depends, &Size, (void *)cache_target_set_size);
	char *Buffer = snew(Size);
	char *Next = stpcpy(Buffer, LengthBuffer);
	targetset_foreach(Depends, &Next, (void *)cache_target_set_append);
	*Next = '\n';
	sqlite3_bind_text(DependsSetStatement, 1, Target->Id, Target->IdLength, SQLITE_STATIC);
	sqlite3_bind_text(DependsSetStatement, 2, Buffer, Size, SQLITE_STATIC);
	sqlite3_step(DependsSetStatement);
	sqlite3_reset(DependsSetStatement);
	sqlite3_exec(Cache, "COMMIT TRANSACTION", 0, 0, 0);
}

targetset_t *cache_scan_get(target_t *Target) {
	sqlite3_bind_text(ScanGetStatement, 1, Target->Id, Target->IdLength, SQLITE_STATIC);
	char *Scans = 0;
	if (sqlite3_step(ScanGetStatement) == SQLITE_ROW) {
		const char *Text = (const char *)sqlite3_column_text(ScanGetStatement, 0);
		int Length = sqlite3_column_bytes(ScanGetStatement, 0);
		Scans = GC_malloc_atomic(Length);
		memcpy(Scans, Text, Length);
	}
	sqlite3_reset(ScanGetStatement);
	return cache_target_set_parse(Scans);
}

void cache_scan_set(target_t *Target, targetset_t *Scans) {
	sqlite3_exec(Cache, "BEGIN TRANSACTION", 0, 0, 0);
	char LengthBuffer[16];
	int Size = sprintf(LengthBuffer, "%d\n", Scans->Size - Scans->Space) + 1;
	targetset_foreach(Scans, &Size, (void *)cache_target_set_size);
	char *Buffer = snew(Size);
	char *Next = stpcpy(Buffer, LengthBuffer);
	targetset_foreach(Scans, &Next, (void *)cache_target_set_append);
	*Next = '\n';
	sqlite3_bind_text(ScanSetStatement, 1, Target->Id, Target->IdLength, SQLITE_STATIC);
	sqlite3_bind_text(ScanSetStatement, 2, Buffer, Size, SQLITE_STATIC);
	sqlite3_step(ScanSetStatement);
	sqlite3_reset(ScanSetStatement);
	sqlite3_exec(Cache, "COMMIT TRANSACTION", 0, 0, 0);
}

static int cache_expr_value_size(ml_value_t *Value);

static int cache_expr_value_size_fn(ml_value_t *Key, ml_value_t *Value, int *Size) {
	*Size += cache_expr_value_size(Key);
	*Size += cache_expr_value_size(Value);
	return 0;
}

static int cache_expr_value_size(ml_value_t *Value) {
	if (Value->Type == MLStringT) {
		return 6 + ml_string_length(Value);
	} else if (Value->Type == MLIntegerT) {
		return 9;
	} else if (Value->Type == MLRealT) {
		return 9;
	} else if (Value == MLNil) {
		return 1;
	} else if (Value->Type == MLListT) {
		int Size = 5;
		for (ml_list_node_t *Node = ml_list_head(Value); Node; Node = Node->Next) {
			Size += cache_expr_value_size(Node->Value);
		}
		return Size;
	} else if (Value->Type == MLMapT) {
		int Size = 5;
		ml_map_foreach(Value, &Size, (void *)cache_expr_value_size_fn);
	} else if (ml_is(Value, TargetT)) {
		target_t *Target = (target_t *)Value;
		return 6 + Target->IdLength;
	}
	return 0;
}

static char *cache_expr_value_write(ml_value_t *Value, char *Buffer);

static int cache_expr_value_write_fn(ml_value_t *Key, ml_value_t *Value, char **Buffer) {
	*Buffer = cache_expr_value_write(Key, *Buffer);
	*Buffer = cache_expr_value_write(Value, *Buffer);
	return 0;
}

#define CACHE_EXPR_NIL			0
#define CACHE_EXPR_STRING		1
#define CACHE_EXPR_INTEGER		2
#define CACHE_EXPR_REAL			3
#define CACHE_EXPR_LIST			4
#define CACHE_EXPR_MAP			5
#define CACHE_EXPR_TARGET		6

static char *cache_expr_value_write(ml_value_t *Value, char *Buffer) {
	if (Value->Type == MLStringT) {
		int Length = ml_string_length(Value);
		*Buffer++ = CACHE_EXPR_STRING;
		*(int32_t *)Buffer = Length;
		Buffer += 4;
		memcpy(Buffer, ml_string_value(Value), Length);
		Buffer[Length] = 0;
		Buffer += Length + 1;
	} else if (Value->Type == MLIntegerT) {
		*Buffer++ = CACHE_EXPR_INTEGER;
		*(int64_t *)Buffer = ml_integer_value(Value);
		Buffer += 8;
	} else if (Value->Type == MLRealT) {
		*Buffer++ = CACHE_EXPR_REAL;
		*(double *)Buffer = ml_real_value(Value);
		Buffer += 8;
	} else if (Value == MLNil) {
		*Buffer++ = CACHE_EXPR_NIL;
	} else if (Value->Type == MLListT) {
		*Buffer++ = CACHE_EXPR_LIST;
		*(int32_t *)Buffer = ml_list_length(Value);
		Buffer += 4;
		for (ml_list_node_t *Node = ml_list_head(Value); Node; Node = Node->Next) {
			Buffer = cache_expr_value_write(Node->Value, Buffer);
		}
	} else if (Value->Type == MLMapT) {
		*Buffer++ = CACHE_EXPR_MAP;
		*(int32_t *)Buffer = ml_map_size(Value);
		Buffer += 4;
		ml_map_foreach(Value, &Buffer, (void *)cache_expr_value_write_fn);
	} else if (ml_is(Value, TargetT)) {
		*Buffer++ = CACHE_EXPR_TARGET;
		target_t *Target = (target_t *)Value;
		*(int32_t *)Buffer = Target->IdLength;
		memcpy(Buffer, Target->Id, Target->IdLength);
		Buffer[Target->IdLength] = 0;
		Buffer += Target->IdLength + 1;
	}
	return Buffer;
}

static const char *cache_expr_value_read(const char *Buffer, ml_value_t **Output) {
	switch (*Buffer++) {
	case CACHE_EXPR_NIL: {
		*Output = MLNil;
		return Buffer;
	}
	case CACHE_EXPR_STRING: {
		int Length = *(int32_t *)Buffer;
		Buffer += 4;
		char *Chars = GC_malloc_atomic(Length + 1);
		memcpy(Chars, Buffer, Length);
		Chars[Length] = 0;
		*Output = ml_string(Chars, Length);
		return Buffer + Length + 1;
	}
	case CACHE_EXPR_INTEGER: {
		*Output = ml_integer(*(int64_t *)Buffer);
		return Buffer + 8;
	}
	case CACHE_EXPR_REAL: {
		*Output = ml_real(*(double *)Buffer);
		return Buffer + 8;
	}
	case CACHE_EXPR_LIST: {
		int Length = *(int32_t *)Buffer;
		Buffer += 4;
		ml_value_t *List = *Output = ml_list();
		for (int I = 0; I < Length; ++I) {
			ml_value_t *Value;
			Buffer = cache_expr_value_read(Buffer, &Value);
			ml_list_append(List, Value);
		}
		return Buffer;
	}
	case CACHE_EXPR_MAP: {
		int Length = *(int32_t *)Buffer;
		Buffer += 4;
		ml_value_t *Map = *Output = ml_map();
		for (int I = 0; I < Length; ++I) {
			ml_value_t *Key, *Value;
			Buffer = cache_expr_value_read(Buffer, &Key);
			Buffer = cache_expr_value_read(Buffer, &Value);
			ml_map_insert(Map, Key, Value);
		}
		return Buffer;
	}
	case CACHE_EXPR_TARGET: {
		int Length = *(int32_t *)Buffer;
		Buffer += 4;
		*Output = (ml_value_t *)target_find(Buffer);
		return Buffer + Length + 1;
	}
	}
	return Buffer;
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
		case SQLITE_BLOB: {
			const char *Buffer = (const char *)sqlite3_column_blob(ExprGetStatement, 0);
			cache_expr_value_read(Buffer, &Result);
			break;
		}
		}
	}
	sqlite3_reset(ExprGetStatement);
	return Result;
}

void cache_expr_set(target_t *Target, ml_value_t *Value) {
	sqlite3_bind_text(ExprSetStatement, 1, Target->Id, Target->IdLength, SQLITE_STATIC);
	if (Value->Type == MLStringT) {
		sqlite3_bind_text(ExprSetStatement, 2, ml_string_value(Value), ml_string_length(Value), SQLITE_STATIC);
	} else if (Value->Type == MLIntegerT) {
		sqlite3_bind_int64(ExprSetStatement, 2, ml_integer_value(Value));
	} else if (Value->Type == MLRealT) {
		sqlite3_bind_double(ExprSetStatement, 2, ml_real_value(Value));
	} else if (Value == MLNil) {
		sqlite3_bind_null(ExprSetStatement, 2);
	} else {
		int Length = cache_expr_value_size(Value);
		char *Buffer = GC_malloc_atomic(Length);
		cache_expr_value_write(Value, Buffer);
		sqlite3_bind_blob(ExprSetStatement, 2, Buffer, Length, SQLITE_STATIC);
	}
	sqlite3_step(ExprSetStatement);
	sqlite3_reset(ExprSetStatement);
}
