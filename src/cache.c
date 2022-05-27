#include "cache.h"
#include "util.h"
#include "rabs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gc/gc.h>
#include <sys/stat.h>
#include <targetcache.h>
#include <radb.h>

#define new(T) ((T *)GC_MALLOC(sizeof(T)))

static string_store_t *MetadataStore;
static string_index_t *TargetsIndex;
static fixed_store_t *DetailsStore;
static string_store_t *DependsStore;
static string_store_t *ScansStore;
static string_store_t *ExprsStore;

int CurrentIteration = 0;

enum {
	CURRENT_VERSION_INDEX,
	CURRENT_ITERATION_INDEX,
	METADATA_SIZE
};

typedef struct cache_details_t {
	uint8_t Hash[SHA256_BLOCK_SIZE];
	uint8_t BuildHash[SHA256_BLOCK_SIZE];
	uint32_t Parent;
	uint32_t LastUpdated;
	uint32_t LastChecked;
	time_t FileTime;
} cache_details_t;

void cache_open(const char *RootPath) {
	const char *CacheFileName = concat(RootPath, "/", SystemName, ".db", NULL);
	struct stat Stat[1];
	if (stat(CacheFileName, Stat)) {
		mkdir(CacheFileName, 0777);
		MetadataStore = string_store_create(concat(CacheFileName, "/metadata", NULL), 16, 0);
		TargetsIndex = string_index_create(concat(CacheFileName, "/targets", NULL), 32, 4096);
		DetailsStore = fixed_store_create(concat(CacheFileName, "/details", NULL), sizeof(cache_details_t), 1024);
		DependsStore = string_store_create(concat(CacheFileName, "/depends", NULL), 32, 4096);
		ScansStore = string_store_create(concat(CacheFileName, "/scans", NULL), 128, 524288);
		ExprsStore = string_store_create(concat(CacheFileName, "/exprs", NULL), 16, 512);
	} else if (!S_ISDIR(Stat->st_mode)) {
		printf("Version error: database was built with an older version of Rabs. Delete %s to force a clean build.\n", CacheFileName);
		exit(1);
	} else {
		MetadataStore = string_store_open(concat(CacheFileName, "/metadata", NULL));
		{
			char Temp[16];
			string_store_get(MetadataStore, CURRENT_VERSION_INDEX, Temp, 16);
			int Current[3], Working[3] = {MINIMAL_VERSION};
			sscanf(Temp, "%d.%d.%d", Current + 0, Current + 1, Current + 2);
			if (Current[0] < Working[0]) {
				printf("Version error: database was built with an older version of Rabs. Delete %s to force a clean build.\n", CacheFileName);
				exit(1);
			} else if (Current[0] == Working[0]) {
				if (Current[1] < Working[1]) {
					printf("Version error: database was built with an older version of Rabs. Delete %s to force a clean build.\n", CacheFileName);
					exit(1);
				} else if (Current[1] == Working[1]) {
					if (Current[2] < Working[2]) {
						printf("Version error: database was built with an older version of Rabs. Delete %s to force a clean build.\n", CacheFileName);
						exit(1);
					}
				}
			}
		}
		TargetsIndex = string_index_open(concat(CacheFileName, "/targets", NULL));
		DetailsStore = fixed_store_open(concat(CacheFileName, "/details", NULL));
		DependsStore = string_store_open(concat(CacheFileName, "/depends", NULL));
		ScansStore = string_store_open(concat(CacheFileName, "/scans", NULL));
		ExprsStore = string_store_open(concat(CacheFileName, "/exprs", NULL));
		{
			uint32_t Temp;
			string_store_get(MetadataStore, CURRENT_ITERATION_INDEX, &Temp, 4);
			CurrentIteration = Temp;
		}
	}
	++CurrentIteration;
	printf("Rabs version = %d.%d.%d\n", CURRENT_VERSION);
	printf("Build iteration = %d\n", CurrentIteration);
	{
		uint32_t Temp = CurrentIteration;
		string_store_set(MetadataStore, CURRENT_ITERATION_INDEX, &Temp, sizeof(uint32_t));
	}
	{
		char Temp[16];
		sprintf(Temp, "%d.%d.%d", CURRENT_VERSION);
		string_store_set(MetadataStore, CURRENT_VERSION_INDEX, Temp, sizeof(Temp));
	}
	targetcache_init();
	atexit(cache_close);
}

void cache_close() {
	string_store_close(MetadataStore);
	string_index_close(TargetsIndex);
	fixed_store_close(DetailsStore);
	string_store_close(DependsStore);
	string_store_close(ScansStore);
	string_store_close(ExprsStore);
}

void cache_bump_iteration() {
	++CurrentIteration;
	printf("Rabs version = %d.%d.%d\n", CURRENT_VERSION);
	printf("Build iteration = %d\n", CurrentIteration);
	{
		uint32_t Temp = CurrentIteration;
		string_store_set(MetadataStore, CURRENT_ITERATION_INDEX, &Temp, sizeof(uint32_t));
	}
	{
		char Temp[16];
		sprintf(Temp, "%d.%d.%d", CURRENT_VERSION);
		string_store_set(MetadataStore, CURRENT_VERSION_INDEX, Temp, sizeof(Temp));
	}
}

void cache_hash_get(target_t *Target, int *LastUpdated, int *LastChecked, time_t *FileTime, unsigned char Hash[SHA256_BLOCK_SIZE]) {
	cache_details_t *Details = fixed_store_get(DetailsStore, Target->CacheIndex);
	memcpy(Hash, Details->Hash, SHA256_BLOCK_SIZE);
	*LastUpdated = Details->LastUpdated;
	*LastChecked = Details->LastChecked;
	*FileTime = Details->FileTime;
}

void cache_hash_set(target_t *Target, time_t FileTime) {
	cache_details_t *Details = fixed_store_get(DetailsStore, Target->CacheIndex);
	memcpy(Details->Hash, Target->Hash, SHA256_BLOCK_SIZE);
	Details->LastUpdated = Target->LastUpdated;
	Details->LastChecked = CurrentIteration;
	Details->FileTime = FileTime;
}

void cache_build_hash_get(target_t *Target, unsigned char Hash[SHA256_BLOCK_SIZE]) {
	cache_details_t *Details = fixed_store_get(DetailsStore, Target->CacheIndex);
	memcpy(Hash, Details->BuildHash, SHA256_BLOCK_SIZE);
}

void cache_build_hash_set(target_t *Target, unsigned char Hash[SHA256_BLOCK_SIZE]) {
	cache_details_t *Details = fixed_store_get(DetailsStore, Target->CacheIndex);
	memcpy(Details->BuildHash, Hash, SHA256_BLOCK_SIZE);
	if (Target->Parent) Details->Parent = Target->Parent->CacheIndex;
}

void cache_last_check_set(target_t *Target, time_t FileTime) {
	cache_details_t *Details = fixed_store_get(DetailsStore, Target->CacheIndex);
	Details->LastUpdated = Target->LastUpdated;
	Details->LastChecked = CurrentIteration;
	Details->FileTime = FileTime;
}

target_t *cache_parent_get(target_t *Target) {
	cache_details_t *Details = fixed_store_get(DetailsStore, Target->CacheIndex);
	size_t Parent = Details->Parent;
	if (!Parent) return NULL;
	target_id_slot R = targetcache_index(Parent);
	return R.Slot[0] ?: target_load(R.Id, Parent, R.Slot);
}

static targetset_t *cache_target_set_parse(uint32_t *Indices) {
	targetset_t *Set = targetset_new();
	if (!Indices) return Set;
	int Size = Indices[0];
	targetset_init(Set, Size);
	while (--Size >= 0) {
		size_t Index = *++Indices;
		target_id_slot R = targetcache_index(Index);
		target_t *Target = R.Slot[0] ?: target_load(R.Id, Index, R.Slot);
		targetset_insert(Set, Target);
	}
	return Set;
}

static int cache_target_set_index(target_t *Target, uint32_t **IndexP) {
	**IndexP = Target->CacheIndex;
	++*IndexP;
	return 0;
}

targetset_t *cache_depends_get(target_t *Target) {
	size_t Length = string_store_size(DependsStore, Target->CacheIndex);
	if (Length) {
		uint32_t *Buffer = GC_MALLOC_ATOMIC(Length);
		string_store_get(DependsStore, Target->CacheIndex, Buffer, Length);
		return cache_target_set_parse(Buffer);
	} else {
		return targetset_new();
	}
}

void cache_depends_set(target_t *Target, targetset_t *Depends) {
	int Size = Depends->Size - Depends->Space;
	uint32_t *Indices = anew(uint32_t, Size + 1);
	Indices[0] = Size;
	uint32_t *IndexP = Indices + 1;
	targetset_foreach(Depends, &IndexP, (void *)cache_target_set_index);
	string_store_set(DependsStore, Target->CacheIndex, Indices, (Size + 1) * sizeof(uint32_t));
}

targetset_t *cache_scan_get(target_t *Target) {
	size_t Length = string_store_size(ScansStore, Target->CacheIndex);
	if (Length) {
		uint32_t *Buffer = GC_MALLOC_ATOMIC(Length);
		string_store_get(ScansStore, Target->CacheIndex, Buffer, Length);
		return cache_target_set_parse(Buffer);
	} else {
		return targetset_new();
	}
}

void cache_scan_set(target_t *Target, targetset_t *Scans) {
	int Size = Scans->Size - Scans->Space;
	uint32_t *Indices = anew(uint32_t, Size + 1);
	Indices[0] = Size;
	uint32_t *IndexP = Indices + 1;
	targetset_foreach(Scans, &IndexP, (void *)cache_target_set_index);
	string_store_set(ScansStore, Target->CacheIndex, Indices, (Size + 1) * sizeof(uint32_t));
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
		ML_LIST_FOREACH(Value, Node) {
			Size += cache_expr_value_size(Node->Value);
		}
		return Size;
	} else if (Value->Type == MLMapT) {
		int Size = 5;
		ML_MAP_FOREACH(Value, Node) {
			Size += cache_expr_value_size(Node->Key);
			Size += cache_expr_value_size(Node->Value);
		}
		return Size;
	} else if (ml_is(Value, TargetT)) {
		target_t *Target = (target_t *)Value;
		return 6 + Target->IdLength;
	}
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
		ML_LIST_FOREACH(Value, Node) {
			Buffer = cache_expr_value_write(Node->Value, Buffer);
		}
	} else if (Value->Type == MLMapT) {
		*Buffer++ = CACHE_EXPR_MAP;
		*(int32_t *)Buffer = ml_map_size(Value);
		Buffer += 4;
		ML_MAP_FOREACH(Value, Node) {
			Buffer = cache_expr_value_write(Node->Key, Buffer);
			Buffer = cache_expr_value_write(Node->Value, Buffer);
		}
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
		char *Chars = GC_MALLOC_ATOMIC(Length + 1);
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
		target_t *Target = target_find(Buffer);
		if (!Target) {
			printf("\e[31mError: target not defined: %s\e[0m\n", Buffer);
			exit(1);
		}
		*Output = (ml_value_t *)Target;
		return Buffer + Length + 1;
	}
	}
	return Buffer;
}

ml_value_t *cache_expr_get(target_t *Target) {
	ml_value_t *Result = 0;
	size_t Length = string_store_size(ExprsStore, Target->CacheIndex);
	if (Length) {
		char *Buffer = GC_MALLOC_ATOMIC(Length);
		string_store_get(ExprsStore, Target->CacheIndex, Buffer, Length);
		cache_expr_value_read(Buffer, &Result);
	}
	return Result;
}

void cache_expr_set(target_t *Target, ml_value_t *Value) {
	size_t Length = cache_expr_value_size(Value);
	char *Buffer = GC_MALLOC_ATOMIC(Length);
	cache_expr_value_write(Value, Buffer);
	string_store_set(ExprsStore, Target->CacheIndex, Buffer, Length);
}

size_t cache_target_id_to_index(const char *Id) {
	string_index_result_t Result = string_index_insert2(TargetsIndex, Id, 0);
	if (Result.Created) {
		cache_details_t *Details = fixed_store_get(DetailsStore, Result.Index);
		memset(Details, 0, sizeof(cache_details_t));
	}
	return Result.Index;
}

size_t cache_target_id_to_index_existing(const char *Id) {
	return string_index_search(TargetsIndex, Id, 0);
}

const char *cache_target_index_to_id(size_t Index) {
	size_t Size = string_index_size(TargetsIndex, Index);
	char *Id = GC_MALLOC_ATOMIC(Size + 1);
	string_index_get(TargetsIndex, Index, Id, Size);
	Id[Size] = 0;
	return Id;
}

size_t cache_target_count() {
	return string_index_num_entries(TargetsIndex);
}
