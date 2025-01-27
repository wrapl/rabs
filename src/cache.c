#include "cache.h"
#include "util.h"
#include "rabs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <gc/gc.h>
#include <sys/stat.h>
#include <targetcache.h>
#include <radb.h>
#include "ml_cbor.h"

#define new(T) ((T *)GC_MALLOC(sizeof(T)))

static string_store_t *MetadataStore;
static string_index0_t *TargetsIndex;
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

static int version_compare(int *A, int *B) {
	if (A[0] < B[0]) return -1;
	if (A[0] > B[0]) return 1;
	if (A[1] < B[1]) return -1;
	if (A[1] > B[1]) return 1;
	if (A[2] < B[2]) return -1;
	if (A[2] > B[2]) return 1;
	return 0;
}

static void cache_delete(const char *Path) {
	DIR *Dir = opendir(Path);
	if (!Dir) {
		fprintf(stderr, "Failed to open cache directory %s: %s", Path, strerror(errno));
		exit(-1);
	}
	struct dirent *Entry = readdir(Dir);
	while (Entry) {
		if (strcmp(Entry->d_name, ".") && strcmp(Entry->d_name, "..")) {
			char *FileName;
			asprintf(&FileName, "%s/%s", Path, Entry->d_name);
			if (unlink(FileName)) {
				fprintf(stderr, "Failed to open delete file %s: %s", FileName, strerror(errno));
				exit(-1);
			}
			free(FileName);
			rewinddir(Dir);
		}
		Entry = readdir(Dir);
	}
	closedir(Dir);
	if (rmdir(Path)) {
		fprintf(stderr, "Failed to open delete directory %s: %s", Path, strerror(errno));
		exit(-1);
	}
}

void cache_open(const char *RootPath) {
	const char *CacheFileName = concat(RootPath, "/", SystemName, ".db", NULL);
	struct stat Stat[1];
	if (stat(CacheFileName, Stat)) {
		mkdir(CacheFileName, 0777);
		int LockFile = open(concat(CacheFileName, "/lock", NULL), O_CREAT | O_WRONLY | O_TRUNC, 0600);
		if (LockFile < 0) {
			fprintf(stderr, "Failed to lock build database: %s", strerror(errno));
			exit(-1);
		}
		struct flock Lock = {0,};
		Lock.l_type = F_WRLCK;
		if (fcntl(LockFile, F_SETLK, &Lock) < 0) {
			fprintf(stderr, "Failed to lock build database: %s", strerror(errno));
			exit(-1);
		}
		MetadataStore = string_store_create(concat(CacheFileName, "/metadata", NULL), 16, 0);
		TargetsIndex = string_index0_create(concat(CacheFileName, "/targets", NULL), 32, 4096);
		DetailsStore = fixed_store_create(concat(CacheFileName, "/details", NULL), sizeof(cache_details_t), 1024);
		DependsStore = string_store_create(concat(CacheFileName, "/depends", NULL), 32, 4096);
		ScansStore = string_store_create(concat(CacheFileName, "/scans", NULL), 128, 524288);
		ExprsStore = string_store_create(concat(CacheFileName, "/exprs", NULL), 16, 512);
	} else if (!S_ISDIR(Stat->st_mode)) {
		printf("Version error: database was built with an incompatible version of Rabs, performing fresh build.\n");
		if (unlink(CacheFileName)) {
			fprintf(stderr, "Failed to open delete file %s: %s", CacheFileName, strerror(errno));
			exit(-1);
		}
		return cache_open(RootPath);
	} else {
		int LockFile = open(concat(CacheFileName, "/lock", NULL), O_CREAT | O_WRONLY | O_TRUNC, 0600);
		if (LockFile < 0) {
			fprintf(stderr, "Failed to lock build database: %s", strerror(errno));
			exit(-1);
		}
		struct flock Lock = {0,};
		Lock.l_type = F_WRLCK;
		if (fcntl(LockFile, F_SETLK, &Lock) < 0) {
			fprintf(stderr, "Failed to lock build database: %s", strerror(errno));
			exit(-1);
		}
		MetadataStore = string_store_open(concat(CacheFileName, "/metadata", NULL));
		{
			char Temp[16];
			string_store_get(MetadataStore, CURRENT_VERSION_INDEX, Temp, 16);
			int Current[3] = {CURRENT_VERSION}, Minimal[3] = {MINIMAL_VERSION}, Actual[3];
			sscanf(Temp, "%d.%d.%d", Actual + 0, Actual + 1, Actual + 2);
			if ((version_compare(Actual, Minimal) < 0) || (version_compare(Current, Actual) < 0)) {
				printf("Version error: database was built with an incompatible version of Rabs, performing fresh build.\n");
				Lock.l_type = F_UNLCK;
				fcntl(LockFile, F_SETLK, &Lock);
				cache_delete(CacheFileName);
				return cache_open(RootPath);
			}
		}
		TargetsIndex = string_index0_open(concat(CacheFileName, "/targets", NULL));
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
	string_index0_close(TargetsIndex);
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

ml_value_t *cache_expr_get(target_t *Target) {
	size_t Length = string_store_size(ExprsStore, Target->CacheIndex);
	if (Length == INVALID_INDEX) return ml_error("IndexError", "Invalid index");
	if (!Length) return NULL;
	ml_cbor_reader_t *Cbor = ml_cbor_reader(NULL, NULL, NULL);
	string_store_reader_t Reader[1];
	string_store_reader_open(Reader, ExprsStore, Target->CacheIndex);
	unsigned char Buffer[16];
	size_t Size;
	do {
		Size = string_store_reader_read(Reader, Buffer, 16);
		ml_cbor_reader_read(Cbor, Buffer, Size);
	} while (Size == 16);
	return ml_cbor_reader_get(Cbor);
}

void cache_expr_set(target_t *Target, ml_value_t *Value) {
	string_store_writer_t Writer[1];
	string_store_writer_open(Writer, ExprsStore, Target->CacheIndex);
	ml_cbor_encode_to(Writer, (void *)string_store_writer_write, NULL, Value);
}

size_t cache_target_id_to_index(const char *Id) {
	index_result_t Result = string_index0_insert2(TargetsIndex, Id, 0);
	if (Result.Created) {
		cache_details_t *Details = fixed_store_get(DetailsStore, Result.Index);
		memset(Details, 0, sizeof(cache_details_t));
	}
	return Result.Index;
}

size_t cache_target_id_to_index_existing(const char *Id) {
	return string_index0_search(TargetsIndex, Id, 0);
}

const char *cache_target_index_to_id(size_t Index) {
	size_t Size = string_index0_size(TargetsIndex, Index);
	char *Id = GC_MALLOC_ATOMIC(Size + 1);
	string_index0_get(TargetsIndex, Index, Id, Size);
	Id[Size] = 0;
	return Id;
}

size_t cache_target_count() {
	return string_index0_num_entries(TargetsIndex);
}
