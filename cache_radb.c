#include "cache.h"
#include "util.h"
#include "rabs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gc/gc.h>
#include <sys/stat.h>
#include <targetcache.h>
#include "radb/string_index.h"
#include "radb/string_store.h"

#define new(T) ((T *)GC_MALLOC(sizeof(T)))

static string_store_t *MetadataStore;
static string_index_t *TargetsIndex;
static string_store_t *HashStore, *HashDetailsStore;
static string_store_t *BuildHashStore;
static string_store_t *DependsStore;
static string_store_t *ScansStore;
static string_store_t *ExprsStore;

int CurrentIteration = 0;

enum {
	CURRENT_VERSION_INDEX,
	CURRENT_ITERATION_INDEX,
	CACHE_SIZE_INDEX,
	METADATA_SIZE
};

typedef struct cache_hash_details_t {
	uint32_t LastUpdated;
	uint32_t LastChecked;
	time_t FileTime;
} cache_hash_details_t;

void cache_open(const char *RootPath) {
	const char *CacheFileName = concat(RootPath, "/", SystemName, ".db", NULL);
	struct stat Stat[1];
	if (stat(CacheFileName, Stat)) {
		MetadataStore = string_store_create(concat(CacheFileName, "/metadata", NULL), 16);
		string_store_reserve(MetadataStore, METADATA_SIZE);
		TargetsIndex = string_index_create(concat(CacheFileName, "/targets", NULL));
		HashStore = string_store_create(concat(CacheFileName, "/hashes", NULL), SHA256_BLOCK_SIZE);
		HashDetailsStore = string_store_create(concat(CacheFileName, "/details", NULL), sizeof(cache_hash_details_t));
		BuildHashStore = string_store_create(concat(CacheFileName, "/builds", NULL), SHA256_BLOCK_SIZE);
		DependsStore = string_store_create(concat(CacheFileName, "/depends", NULL), 16);
		ScansStore = string_store_create(concat(CacheFileName, "/scans", NULL), 16);
		ExprsStore = string_store_create(concat(CacheFileName, "/exprs", NULL), 16);
	} else {
		MetadataStore = string_store_open(concat(CacheFileName, "/metadata", NULL));
		TargetsIndex = string_index_open(concat(CacheFileName, "/targets", NULL));
		HashStore = string_store_open(concat(CacheFileName, "/hashes", NULL));
		HashDetailsStore = string_store_open(concat(CacheFileName, "/details", NULL));
		BuildHashStore = string_store_open(concat(CacheFileName, "/builds", NULL));
		DependsStore = string_store_open(concat(CacheFileName, "/depends", NULL));
		ScansStore = string_store_open(concat(CacheFileName, "/scans", NULL));
		ExprsStore = string_store_open(concat(CacheFileName, "/exprs", NULL));
	}
	++CurrentIteration;
	printf("Rabs version = %s\n", CURRENT_VERSION);
	printf("Build iteration = %d\n", CurrentIteration);
	{
		uint32_t Temp = CurrentIteration;
		string_store_set(MetadataStore, CURRENT_ITERATION_INDEX, &Temp, sizeof(uint32_t));
	}
	{
		const char Temp[] = CURRENT_VERSION;
		string_store_set(MetadataStore, CURRENT_VERSION_INDEX, Temp, sizeof(Temp));
	}
	uint32_t CacheSize = 1024;
	string_store_get_value(MetadataStore, CACHE_SIZE_INDEX, &CacheSize);
	targetcache_init(CacheSize);
	atexit(cache_close);
}

void cache_close() {
	uint32_t CacheSize = targetcache_size();
	string_store_set(MetadataStore, CACHE_SIZE_INDEX, &CacheSize, sizeof(uint32_t));
	string_store_close(MetadataStore);
	string_index_close(TargetsIndex);
	string_store_close(HashStore);
	string_store_close(HashDetailsStore);
	string_store_close(BuildHashStore);
	string_store_close(DependsStore);
	string_store_close(ScansStore);
	string_store_close(ExprsStore);
}

void cache_bump_iteration() {
	++CurrentIteration;
	printf("Rabs version = %s\n", CURRENT_VERSION);
	printf("Build iteration = %d\n", CurrentIteration);
	{
		uint32_t Temp = CurrentIteration;
		string_store_set(MetadataStore, CURRENT_ITERATION_INDEX, &Temp, sizeof(uint32_t));
	}
	{
		const char Temp[] = CURRENT_VERSION;
		string_store_set(MetadataStore, CURRENT_VERSION_INDEX, Temp, sizeof(Temp));
	}
}

void cache_hash_get(target_t *Target, int *LastUpdated, int *LastChecked, time_t *FileTime, unsigned char Hash[SHA256_BLOCK_SIZE]) {
	cache_hash_details_t Details[1];
	string_store_get_value(HashDetailsStore, Target->CacheIndex, Details);
	string_store_get_value(HashStore, Target->CacheIndex, Hash);
	*LastUpdated = Details->LastUpdated;
	*LastChecked = Details->LastChecked;
	*FileTime = Details->FileTime;
}

void cache_hash_set(target_t *Target, time_t FileTime) {
	cache_hash_details_t Details[1] = {{Target->LastUpdated, CurrentIteration, FileTime}};
	string_store_set(HashDetailsStore, Target->CacheIndex, Details, sizeof(cache_hash_details_t));
	string_store_set(HashStore, Target->CacheIndex, Target->Hash, SHA256_BLOCK_SIZE);
}

void cache_build_hash_get(target_t *Target, unsigned char Hash[SHA256_BLOCK_SIZE]) {
	string_store_get_value(BuildHashStore, Target->CacheIndex, Hash);
}

void cache_build_hash_set(target_t *Target, unsigned char BuildHash[SHA256_BLOCK_SIZE]) {
	string_store_set(BuildHashStore, Target->CacheIndex, BuildHash, SHA256_BLOCK_SIZE);
}

void cache_last_check_set(target_t *Target, time_t FileTime) {
	cache_hash_details_t Details[1] = {{Target->LastUpdated, CurrentIteration, FileTime}};
	string_store_set(HashDetailsStore, Target->CacheIndex, Details, sizeof(cache_hash_details_t));
}

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
		targetset_insert(Set, target_find(Id, End - Id));
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
	size_t Length = string_store_get_size(DependsStore, Target->CacheIndex);
	char *Buffer = GC_malloc_atomic(Length);
	string_store_get_value(DependsStore, Target->CacheIndex, Buffer);
	return cache_target_set_parse(Buffer);
}

void cache_depends_set(target_t *Target, targetset_t *Depends) {
	char LengthBuffer[16];
	int Size = sprintf(LengthBuffer, "%d\n", Depends->Size - Depends->Space) + 1;
	targetset_foreach(Depends, &Size, (void *)cache_target_set_size);
	char *Buffer = snew(Size);
	char *Next = stpcpy(Buffer, LengthBuffer);
	targetset_foreach(Depends, &Next, (void *)cache_target_set_append);
	*Next = '\n';
	string_store_set(DependsStore, Target->CacheIndex, Buffer, Size);
}

targetset_t *cache_scan_get(target_t *Target) {
	size_t Length = string_store_get_size(ScansStore, Target->CacheIndex);
	char *Buffer = GC_malloc_atomic(Length);
	string_store_get_value(ScansStore, Target->CacheIndex, Buffer);
	return cache_target_set_parse(Buffer);
}

void cache_scan_set(target_t *Target, targetset_t *Scans) {
	char LengthBuffer[16];
	int Size = sprintf(LengthBuffer, "%d\n", Scans->Size - Scans->Space) + 1;
	targetset_foreach(Scans, &Size, (void *)cache_target_set_size);
	char *Buffer = snew(Size);
	char *Next = stpcpy(Buffer, LengthBuffer);
	targetset_foreach(Scans, &Next, (void *)cache_target_set_append);
	*Next = '\n';
	string_store_set(ScansStore, Target->CacheIndex, Buffer, Size);
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
		*Output = (ml_value_t *)target_find(Buffer, Length);
		return Buffer + Length + 1;
	}
	}
	return Buffer;
}

ml_value_t *cache_expr_get(target_t *Target) {
	ml_value_t *Result = 0;
	size_t Length = string_store_get_size(ExprsStore, Target->CacheIndex);
	char *Buffer = GC_malloc_atomic(Length);
	string_store_get_value(ExprsStore, Target->CacheIndex, Buffer);
	cache_expr_value_read(Buffer, &Result);
	return Result;
}

void cache_expr_set(target_t *Target, ml_value_t *Value) {
	size_t Length = cache_expr_value_size(Value);
	char *Buffer = GC_malloc_atomic(Length);
	cache_expr_value_write(Value, Buffer);
	string_store_set(ExprsStore, Target->CacheIndex, Buffer, Length);
}

size_t cache_target_index(const char *Id) {
	size_t Index = string_index_insert(TargetsIndex, Id);
	string_store_reserve(HashStore, Index);
	string_store_reserve(HashDetailsStore, Index);
	string_store_reserve(BuildHashStore, Index);
	string_store_reserve(DependsStore, Index);
	string_store_reserve(ScansStore, Index);
	string_store_reserve(ExprsStore, Index);
	return Index;
}
