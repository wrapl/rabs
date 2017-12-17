#include "target.h"
#include "rabs.h"
#include "util.h"
#include "context.h"
#include "cache.h"
#include "stringbuffer.h"
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <gc.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

enum {
	STATE_UNCHECKED = 0,
	STATE_CHECKING = -1,
	STATE_CHECKED = -2
};

typedef struct target_file_t target_file_t;
typedef struct target_meta_t target_meta_t;
typedef struct target_expr_t target_expr_t;
typedef struct target_scan_t target_scan_t;
typedef struct scan_results_t scan_results_t;
typedef struct target_symb_t target_symb_t;

extern const char *RootPath;
static stringmap_t TargetCache[1] = {STRINGMAP_INIT};
static int BuiltTargets = 0, NumTargets = 0;

pthread_mutex_t GlobalLock[1] = {PTHREAD_MUTEX_INITIALIZER};
static pthread_cond_t TargetAvailable[1] = {PTHREAD_COND_INITIALIZER};

static ml_value_t *SHA256Method;
static ml_value_t *MissingMethod;

static ml_type_t *TargetT;

__thread target_t *CurrentTarget = 0;
__thread stringmap_t *CurrentDetectedDepends = 0;

static target_t *BuildQueue = 0;

void target_value_hash(ml_value_t *Value, int8_t Hash[SHA256_BLOCK_SIZE]);

static time_t target_hash(target_t *Target, time_t PreviousTime, int8_t PreviousHash[SHA256_BLOCK_SIZE]);
static void target_build(target_t *Target);
static int target_missing(target_t *Target);

int depends_hash_fn(const char *Id, target_t *Depend, int8_t Hash[SHA256_BLOCK_SIZE]) {
	for (int I = 0; I < SHA256_BLOCK_SIZE; ++I) Hash[I] ^= Depend->Hash[I];
	//for (int I = 0; I < SHA256_BLOCK_SIZE; ++I) printf(" %x", Depend->Hash[I]);
	return 0;
}

void target_depends_add(target_t *Target, target_t *Depend) {
	if (Target != Depend) stringmap_insert(Target->Depends, Depend->Id, Depend);
}

int depends_update_fn(const char *Key, target_t *Depend, target_t *Target) {
	if (Depend->LastUpdated == STATE_UNCHECKED) target_update(Depend);
	if (Depend->LastUpdated == STATE_CHECKED) {
		++Target->WaitCount;
		stringmap_insert(Depend->Targets, Target->Id, Target);
	} else {
		if (Depend->LastUpdated > Target->DependsLastUpdated) Target->DependsLastUpdated = Depend->LastUpdated;
	}
	return 0;
}

int depends_print_fn(const char *Key, target_t *Depend, int *DependsLastUpdated) {
	printf("\t\e[35m%s[%d]\e[0m\n", Depend->Id, Depend->LastUpdated);
	return 0;
}

static void target_queue_build(target_t *Target) {
	Target->Next = BuildQueue;
	BuildQueue = Target;
}

int depends_updated_fn(const char *Key, target_t *Target, target_t *Depend) {
	if (Depend->LastUpdated > Target->DependsLastUpdated) Target->DependsLastUpdated = Depend->LastUpdated;
	if (--Target->WaitCount == 0) target_queue_build(Target);
	return 0;
}

static void target_do_build(int ThreadIndex, target_t *Target) {
	int8_t Previous[SHA256_BLOCK_SIZE];
	int LastUpdated, LastChecked;
	time_t FileTime = 0;
	cache_hash_get(Target->Id, &LastUpdated, &LastChecked, &FileTime, Previous);
	if (Target->Build) {
		CurrentContext = Target->BuildContext;
		if ((Target->DependsLastUpdated > LastChecked) || target_missing(Target)) {
			CurrentTarget = Target;
			stringmap_t *DetectedDepends = CurrentDetectedDepends = new(stringmap_t);
			target_build(Target);
			cache_depends_set(Target->Id, DetectedDepends);
		}
	}
	FileTime = target_hash(Target, FileTime, Previous);
	if (!LastUpdated || memcmp(Previous, Target->Hash, SHA256_BLOCK_SIZE)) {
		Target->LastUpdated = CurrentVersion;
		cache_hash_set(Target->Id, FileTime, Target->Hash);
	} else {
		Target->LastUpdated = LastUpdated;
		cache_last_check_set(Target->Id);
	}
	//GC_gcollect();
	printf("\e[32m[%d] Built %s @ %d (%d targets, %d bytes)\e[0m\n", ThreadIndex, Target->Id, LastUpdated, NumTargets, GC_get_heap_size());
	stringmap_foreach(Target->Targets, Target, (void *)depends_updated_fn);
	memset(Target->Depends, 0, sizeof(Target->Depends));
	memset(Target->Targets, 0, sizeof(Target->Targets));
	pthread_cond_broadcast(TargetAvailable);
}

void target_update(target_t *Target) {
	if (Target->LastUpdated == STATE_CHECKING) {
		printf("\e[31mError: build cycle with %s\e[0m\n", Target->Id);
		exit(1);
	}
	if (Target->LastUpdated == STATE_UNCHECKED) {
		Target->LastUpdated = STATE_CHECKING;
		++BuiltTargets;
		//printf("\e[32m[%d/%d] \e[33mtarget_update(%s)\e[0m\n", BuiltTargets, TargetCache->items, Target->Id);
		stringmap_foreach(Target->Depends, Target, (void *)depends_update_fn);
		int8_t Previous[SHA256_BLOCK_SIZE];
		int LastUpdated, LastChecked;
		time_t FileTime = 0;
		if (Target->Build) {
			int8_t BuildHash[SHA256_BLOCK_SIZE];
			if (Target->Build->Type == ClosureT) {
				ml_closure_hash(Target->Build, BuildHash);
				//printf("CurrentHash =");
				//for (int I = 0; I < SHA256_BLOCK_SIZE; ++I) printf("%02hhx", BuildHash[I]);
				//printf("\n");
			} else {
				memset(BuildHash, 0, SHA256_BLOCK_SIZE);
			}
			stringmap_t *PreviousDetectedDepends = cache_depends_get(Target->Id);
			const char *BuildId = concat(Target->Id, "::build", 0);
			cache_hash_get(BuildId, &LastUpdated, &LastChecked, &FileTime, Previous);
			//printf("PreviousHash =");
			//for (int I = 0; I < SHA256_BLOCK_SIZE; ++I) printf("%02hhx", Previous[I]);
			//printf("\n");
			if (!LastUpdated || memcmp(Previous, BuildHash, SHA256_BLOCK_SIZE)) {
				cache_hash_set(BuildId, 0, BuildHash);
				Target->DependsLastUpdated = CurrentVersion;
			} else if (PreviousDetectedDepends) {
				stringmap_foreach(PreviousDetectedDepends, Target, (void *)depends_update_fn);
			}
		}
		Target->LastUpdated = STATE_CHECKED;
		if (Target->WaitCount == 0) target_queue_build(Target);
	}
}

int depends_query_fn(const char *Id, target_t *Depends, int *DependsLastUpdated) {
	target_query(Depends);
	if (Depends->LastUpdated > *DependsLastUpdated) *DependsLastUpdated = Depends->LastUpdated;
	return 0;
}

void target_query(target_t *Target) {
	if (Target->LastUpdated == -1) return;
	if (Target->LastUpdated == 0) {
		Target->LastUpdated = -1;
		printf("Target: %s\n", Target->Id);
		int DependsLastUpdated = 0;
		stringmap_foreach(Target->Depends, &DependsLastUpdated, (void *)depends_query_fn);
		int8_t Previous[SHA256_BLOCK_SIZE];
		int LastUpdated, LastChecked;
		time_t FileTime = 0;
		if (Target->Build) {	
			int8_t BuildHash[SHA256_BLOCK_SIZE];
			ml_closure_hash(Target->Build, BuildHash);
			stringmap_t *PreviousDetectedDepends = cache_depends_get(Target->Id);
			const char *BuildId = concat(Target->Id, "::build", 0);
			cache_hash_get(BuildId, &LastUpdated, &LastChecked, &FileTime, Previous);
			if (!LastUpdated || memcmp(Previous, BuildHash, SHA256_BLOCK_SIZE)) {
				DependsLastUpdated = CurrentVersion;
				printf("\t\e[35m<build function updated>\e[0m\n");
			} else if (PreviousDetectedDepends) {
				stringmap_foreach(PreviousDetectedDepends, &DependsLastUpdated, (void *)depends_query_fn);
			}
			cache_hash_get(Target->Id, &LastUpdated, &LastChecked, &FileTime, Previous);
			if ((DependsLastUpdated > LastChecked) || target_missing(Target)) {
				printf("\e[33mtarget_build(%s) Depends = %d, Last Updated = %d\e[0m\n", Target->Id, DependsLastUpdated, LastChecked);
				stringmap_foreach(Target->Depends, &DependsLastUpdated, (void *)depends_print_fn);
				if (PreviousDetectedDepends) {
					stringmap_foreach(PreviousDetectedDepends, &DependsLastUpdated, (void *)depends_print_fn);
				}
			}
		} else {
			cache_hash_get(Target->Id, &LastUpdated, &LastChecked, &FileTime, Previous);	
		}
	}
}

void target_depends_auto(target_t *Depend) {
	if (CurrentTarget && CurrentTarget != Depend && !stringmap_search(CurrentTarget->Depends, Depend->Id)) {
		stringmap_insert(CurrentDetectedDepends, Depend->Id, Depend);
	}
}

static target_t *target_alloc(int Size, ml_type_t *Type, const char *Id) {
	++NumTargets;
	target_t *Target = (target_t *)GC_MALLOC(Size);
	Target->Type = Type;
	Target->Id = Id;
	Target->Build = 0;
	Target->LastUpdated = 0;
	stringmap_insert(TargetCache, Id, Target);
	return Target;
}

#define target_new(type, Type, Id) ((type *)target_alloc(sizeof(type), Type, Id))

struct target_file_t {
	TARGET_FIELDS
	int Absolute;
	const char *Path;
};

static ml_type_t *FileTargetT;

static ml_value_t *target_file_stringify(void *Data, int Count, ml_value_t **Args) {
	ml_stringbuffer_t *Buffer = (ml_stringbuffer_t *)Args[0];
	target_file_t *Target = (target_file_t *)Args[1];
	//target_depends_auto((target_t *)Target);
	if (Target->Absolute) {
		ml_stringbuffer_add(Buffer, Target->Path, strlen(Target->Path));
	} else {
		const char *Path = vfs_resolve(Target->BuildContext->Mounts, concat(RootPath, "/", Target->Path, 0));
		ml_stringbuffer_add(Buffer, Path, strlen(Path));
	}
	return Some;
}

static ml_value_t *target_file_to_string(void *Data, int Count, ml_value_t **Args) {
	target_file_t *Target = (target_file_t *)Args[0];
	//target_depends_auto((target_t *)Target);
	if (Target->Absolute) {
		return ml_string(Target->Path, -1);
	} else {
		const char *Path = vfs_resolve(Target->BuildContext->Mounts, concat(RootPath, "/", Target->Path, 0));
		return ml_string(Path, -1);
	}
}

static time_t target_file_hash(target_file_t *Target, time_t PreviousTime, int8_t PreviousHash[SHA256_BLOCK_SIZE]) {
	const char *FileName;
	if (Target->Absolute) {
		FileName = Target->Path;
	} else {
		FileName = vfs_resolve(Target->BuildContext->Mounts, concat(RootPath, "/", Target->Path, 0));
	}
	struct stat Stat[1];
	if (stat(FileName, Stat)) {
		//printf("\e[31mWarning: rule failed to build: %s\e[0m\n", FileName);
		return 0;
	}
	if (!S_ISREG(Stat->st_mode)) {
		memset(Target->Hash, -1, SHA256_BLOCK_SIZE);
	} else if (Stat->st_mtime == PreviousTime) {
		memcpy(Target->Hash, PreviousHash, SHA256_BLOCK_SIZE);
	} else {
		int File = open(FileName, 0, O_RDONLY);
		SHA256_CTX Ctx[1];
		uint8_t Buffer[8192];
		sha256_init(Ctx);
		for (;;) {
			int Count = read(File, Buffer, 8192);
			if (Count == 0) break;
			if (Count == -1) {
				printf("\e[31mError: read error: %s\e[0m\n", FileName);
				exit(1);
			}
			sha256_update(Ctx, Buffer, Count);
		}
		close(File);
		sha256_final(Ctx, Target->Hash);
	}
	return Stat->st_mtime;
}

static void target_default_build(target_t *Target) {
	ml_value_t *Result = ml_inline(Target->Build, 1, Target);
	if (Result->Type == ErrorT) {
		fprintf(stderr, "\e[31mError: %s: %s\n\e[0m", Target->Id, ml_error_message(Result));
		const char *Source;
		int Line;
		for (int I = 0; ml_error_trace(Result, I, &Source, &Line); ++I) printf("\e[31m\t%s:%d\n\e[0m", Source, Line);
		exit(1);
	}
}

static int target_file_missing(target_file_t *Target) {
	const char *FileName;
	if (Target->Absolute) {
		FileName = Target->Path;
	} else {
		FileName = vfs_resolve(CurrentContext->Mounts, concat(RootPath, "/", Target->Path, 0));
	}
	struct stat Stat[1];
	return !!stat(FileName, Stat);
}

target_t *target_file_check(const char *Path, int Absolute) {
	Path = concat(Path, 0);
	const char *Id = concat("file:", Path, 0);
	target_file_t *Target = (target_file_t *)stringmap_search(TargetCache, Id);
	if (!Target) {
		Target = target_new(target_file_t, FileTargetT, Id);
		Target->Absolute = Absolute;
		Target->Path = Path;
		Target->BuildContext = CurrentContext;
	}
	return (target_t *)Target;
}

ml_value_t *target_file_new(void *Data, int Count, ml_value_t **Args) {
	const char *Path = ml_string_value(Args[0]);
	target_t *Target;
	if (Path[0] != '/') {
		Path = concat(CurrentContext->Path, "/", Path, 0) + 1;
		Target = target_file_check(Path, 0);
	} else {
		const char *Relative = match_prefix(Path, RootPath);
		if (Relative) {
			Target = target_file_check(Relative + 1, 0);
		} else {
			Target = target_file_check(Path, 1);
		}
	}
	return (ml_value_t *)Target;
}

ml_value_t *target_file_dir(void *Data, int Count, ml_value_t **Args) {
	target_file_t *FileTarget = (target_file_t *)Args[0];
	char *Path;
	int Absolute;
	if (Count > 1 && Args[1] != Nil) {
		Path = vfs_resolve(FileTarget->BuildContext->Mounts, concat(RootPath, "/", FileTarget->Path, 0));
		Absolute = 1;
	} else {
		Path = concat(FileTarget->Path, 0);
		Absolute = FileTarget->Absolute;
	}
	char *Last = Path;
	for (char *P = Path; *P; ++P) if (*P == '/') Last = P;
	*Last = 0;
	target_t *Target = target_file_check(Path, Absolute);
	return (ml_value_t *)Target;
}

ml_value_t *target_file_basename(void *Data, int Count, ml_value_t **Args) {
	target_file_t *FileTarget = (target_file_t *)Args[0];
	const char *Path = FileTarget->Path;
	const char *Last = Path;
	for (const char *P = Path; *P; ++P) if (*P == '/') Last = P;
	return ml_string(concat(Last + 1, 0), -1);
}

ml_value_t *target_file_exists(void *Data, int Count, ml_value_t **Args) {
	target_file_t *Target = (target_file_t *)Args[0];
	const char *FileName;
	if (Target->Absolute) {
		FileName = Target->Path;
	} else {
		FileName = vfs_resolve(CurrentContext->Mounts, concat(RootPath, "/", Target->Path, 0));
	}
	struct stat Stat[1];
	if (!stat(FileName, Stat)) {
		return (ml_value_t *)Target;
	} else {
		return Nil;
	}
}

ml_value_t *target_file_copy(void *Data, int Count, ml_value_t **Args) {
	target_file_t *Source = (target_file_t *)Args[0];
	target_file_t *Dest = (target_file_t *)Args[1];
	const char *SourcePath, *DestPath;
	if (Source->Absolute) {
		SourcePath = Source->Path;
	} else {
		SourcePath = vfs_resolve(CurrentContext->Mounts, concat(RootPath, "/", Source->Path, 0));
	}
	if (Dest->Absolute) {
		DestPath = Dest->Path;
	} else {
		DestPath = vfs_resolve(CurrentContext->Mounts, concat(RootPath, "/", Dest->Path, 0));
	}
	int SourceFile = open(SourcePath, O_RDONLY);
	if (SourceFile < 0) return ml_error("FileError", "could not open source %s", SourcePath);
	int DestFile = open(DestPath, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IRGRP| S_IROTH | S_IWUSR | S_IWGRP| S_IWOTH);
	if (DestFile < 0) {
		close(SourceFile);
		return ml_error("FileError", "could not open destination %s", DestPath);
	}
	char *Buffer = snew(4096);
	int Length;
	while ((Length = read(SourceFile, Buffer, 4096)) > 0 && write(DestFile, Buffer, Length) > 0);
	close(SourceFile);
	close(DestFile);
	if (Length < 0) return ml_error("FileError", "file copy failed");
	return Nil;
}

ml_value_t *target_file_div(void *Data, int Count, ml_value_t **Args) {
	target_file_t *FileTarget = (target_file_t *)Args[0];
	const char *Path = concat(FileTarget->Path, "/", ml_string_value(Args[1]), 0);
	target_t *Target = target_file_check(Path, FileTarget->Absolute);
	return (ml_value_t *)Target;
}

ml_value_t *target_file_mod(void *Data, int Count, ml_value_t **Args) {
	target_file_t *FileTarget = (target_file_t *)Args[0];
	const char *Replacement = ml_string_value(Args[1]);
	char *Path = concat(FileTarget->Path, ".", Replacement, 0);
	for (char *End = Path + strlen(FileTarget->Path); --End >= Path;) {
		if (*End == '.') {
			strcpy(End + 1, Replacement);
			break;
		} else if (*End == '/') {
			break;
		}
	}
	target_t *Target = target_file_check(Path, FileTarget->Absolute);
	return (ml_value_t *)Target;
}

struct target_meta_t {
	TARGET_FIELDS
	const char *Name;
};

static ml_type_t *MetaTargetT;

static time_t target_meta_hash(target_meta_t *Target, time_t PreviousTime, int8_t PreviousHash[SHA256_BLOCK_SIZE]) {
	memset(Target->Hash, 0, SHA256_BLOCK_SIZE);
	stringmap_foreach(Target->Depends, Target->Hash, (void *)depends_hash_fn);
	return 0;
}

ml_value_t *target_meta_new(void *Data, int Count, ml_value_t **Args) {
	if (Count < 0) return ml_error("ParamError", "missing parameter <name>");
	const char *Name = ml_string_value(Args[0]);
	const char *Id = concat("meta:", CurrentContext->Path, "::", Name, 0);
	target_meta_t *Target = (target_meta_t *)stringmap_search(TargetCache, Id);
	if (!Target) {
		Target = target_new(target_meta_t, MetaTargetT, Id);
		Target->Name = Name;
	}
	return (ml_value_t *)Target;
}

struct target_expr_t {
	TARGET_FIELDS
};

static ml_type_t *ExprTargetT;

static ml_value_t *target_expr_stringify(void *Data, int Count, ml_value_t **Args) {
	ml_value_t *AppendMethod = ml_method("append");
	ml_stringbuffer_t *Buffer = (ml_stringbuffer_t *)Args[0];
	target_expr_t *Target = (target_expr_t *)Args[1];
	target_depends_auto((target_t *)Target);
	ml_value_t *Value = cache_expr_get(Target->Id);
	return ml_inline(AppendMethod, 2, Buffer, Value);
}

static ml_value_t *target_expr_to_string(void *Data, int Count, ml_value_t **Args) {
	ml_value_t *StringMethod = ml_method("string");
	target_expr_t *Target = (target_expr_t *)Args[0];
	target_depends_auto((target_t *)Target);
	ml_value_t *Value = cache_expr_get(Target->Id);
	return ml_inline(StringMethod, 1, Value);
}

static time_t target_expr_hash(target_expr_t *Target, time_t PreviousTime, int8_t PreviousHash[SHA256_BLOCK_SIZE]) {
	ml_value_t *Value = cache_expr_get(Target->Id);
	target_value_hash(Value, Target->Hash);
	return 0;
}

static void target_expr_build(target_expr_t *Target) {
	ml_value_t *Result = ml_inline(Target->Build, 1, Target);
	if (Result->Type == ErrorT) {
		fprintf(stderr, "\e[31mError: %s: %s\n\e[0m", Target->Id, ml_error_message(Result));
		const char *Source;
		int Line;
		for (int I = 0; ml_error_trace(Result, I, &Source, &Line); ++I) printf("\e[31m\t%s:%d\n\e[0m", Source, Line);
		exit(1);
	}
	cache_expr_set(Target->Id, Result);
}

ml_value_t *target_expr_new(void *Data, int Count, ml_value_t **Args) {
	if (Count < 0) return ml_error("ParamError", "missing parameter <name>");
	const char *Name = ml_string_value(Args[0]);
	const char *Id = concat("expr:", CurrentContext->Path, "::", Name, 0);
	target_expr_t *Target = (target_expr_t *)stringmap_search(TargetCache, Id);
	if (!Target) {
		Target = target_new(target_expr_t, ExprTargetT, Id);
	}
	return (ml_value_t *)Target;
}

ml_value_t *target_depends_list(target_t *Depend, target_t *Target) {
	stringmap_insert(Target->Depends, Depend->Id, Depend);
	return 0;
}

ml_value_t *target_depend(void *Data, int Count, ml_value_t **Args) {
	target_t *Target = (target_t *)Args[0];
	for (int I = 1; I < Count; ++I) {
		ml_value_t *Arg = Args[I];
		if (Arg->Type == ListT) {
			ml_list_foreach(Arg, Target, (void *)target_depends_list);
		} else if (Arg != Nil) {
			target_t *Depend = (target_t *)Arg;
			stringmap_insert(Target->Depends, Depend->Id, Depend);
		}
	}
	return Args[0];
}

ml_value_t *target_set_build(void *Data, int Count, ml_value_t **Args) {
	target_t *Target = (target_t *)Args[0];
	//if (Target->Build) return ml_error("ParameterError", "build already defined for %s", Target->Id);
	Target->Build = Args[1];
	Target->BuildContext = CurrentContext;
	Target->LastUpdated = 0;
	return Args[0];
}

struct target_scan_t {
	TARGET_FIELDS
	const char *Name;
	target_t *Source;
	scan_results_t *Results;
	ml_value_t *Scan;
};

static ml_type_t *ScanTargetT;

struct scan_results_t {
	TARGET_FIELDS
	target_scan_t *Scan;
};

static ml_type_t *ScanResultsT;

static time_t target_scan_hash(target_scan_t *Target, time_t PreviousTime, int8_t PreviousHash[SHA256_BLOCK_SIZE]) {
	stringmap_t *Scans = cache_scan_get(Target->Results->Id);
	if (Scans) stringmap_foreach(Scans, Target->Results, (void *)depends_update_fn);
	return 0;
}

ml_value_t *scan_results_depend(void *Data, int Count, ml_value_t **Args) {
	target_scan_t *Target = ((scan_results_t *)Args[0])->Scan;
	for (int I = 1; I < Count; ++I) {
		ml_value_t *Arg = Args[I];
		if (Arg->Type == ListT) {
			ml_list_foreach(Arg, Target, (void *)target_depends_list);
		} else if (Arg != Nil) {
			target_t *Depend = (target_t *)Arg;
			stringmap_insert(Target->Depends, Depend->Id, Depend);
		}
	}
	return Args[0];
}

ml_value_t *scan_results_set_build(void *Data, int Count, ml_value_t **Args) {
	target_scan_t *Target = ((scan_results_t *)Args[0])->Scan;
	Target->Build = Args[1];
	Target->BuildContext = CurrentContext;
	Target->LastUpdated = 0;
	return Args[0];
}

static time_t scan_results_hash(scan_results_t *Target, time_t PreviousTime, int8_t PreviousHash[SHA256_BLOCK_SIZE]) {
	stringmap_t *Scans = cache_scan_get(Target->Id);
	memset(Target->Hash, 0, SHA256_BLOCK_SIZE);
	//printf("#%s\n", Target->Id);
	//stringmap_foreach(Scans, 0, (void *)depends_print_fn);
	if (Scans) stringmap_foreach(Scans, Target->Hash, (void *)depends_hash_fn);
	return 0;
}

static int build_scan_target_list(target_t *Depend, stringmap_t *Scans) {
	stringmap_insert(Scans, Depend->Id, Depend);
	return 0;
}

static void target_scan_build(target_scan_t *Target) {
	ml_value_t *Result = ml_inline(Target->Build, 1, Target->Source);
	if (Result->Type == ErrorT) {
		fprintf(stderr, "\e[31mError: %s: %s\n\e[0m", Target->Id, ml_error_message(Result));
		const char *Source;
		int Line;
		for (int I = 0; ml_error_trace(Result, I, &Source, &Line); ++I) printf("\e[31m\t%s:%d\n\e[0m", Source, Line);
		exit(1);
	}
	stringmap_t Scans[1] = {STRINGMAP_INIT};
	ml_list_foreach(Result, Scans, (void *)build_scan_target_list);
	cache_scan_set(Target->Results->Id, Scans);
}

ml_value_t *target_scan_new(void *Data, int Count, ml_value_t **Args) {
	target_t *ParentTarget = (target_t *)Args[0];
	const char *Name = ml_string_value(Args[1]);
	const char *Id = concat("results:", ParentTarget->Id, "::", Name, 0);
	scan_results_t *Target = (scan_results_t *)stringmap_search(TargetCache, Id);
	if (!Target) {
		Target = target_new(scan_results_t, ScanResultsT, Id);
		const char *ScanId = concat("scan:", ParentTarget->Id, "::", Name, 0);
		target_scan_t *ScanTarget = Target->Scan = target_new(target_scan_t, ScanTargetT, ScanId);
		stringmap_insert(ScanTarget->Depends, ParentTarget->Id, ParentTarget);
		ScanTarget->Name = Name;
		ScanTarget->Source = ParentTarget;
		ScanTarget->Results = Target;
		stringmap_insert(Target->Depends, ScanTarget->Id, ScanTarget);
	}
	return (ml_value_t *)Target;
}

struct target_symb_t {
	TARGET_FIELDS
	const char *Name, *Context;
};

static ml_type_t *SymbTargetT;

static ml_value_t *symb_target_deref(ml_value_t *Ref) {
	target_symb_t *Target = (target_symb_t *)Ref;
	context_t *Context = context_find(Target->Context);
	return context_symb_get(Context, Target->Name);
}

static ml_value_t *symb_target_assign(ml_value_t *Ref, ml_value_t *Value) {
	target_symb_t *Target = (target_symb_t *)Ref;
	context_t *Context = context_find(Target->Context);
	context_symb_set(Context, Target->Name, Value);
	return Value;
}

static time_t target_symb_hash(target_symb_t *Target, time_t PreviousTime, int8_t PreviousHash[SHA256_BLOCK_SIZE]) {
	context_t *Context = context_find(Target->Context);
	ml_value_t *Value = context_symb_get(Context, Target->Name);
	target_value_hash(Value, Target->Hash);
	return 0;
}

target_t *target_symb_new(const char *Name) {
	const char *Id = concat("symb:", CurrentContext->Name, "/", Name, 0);
	target_symb_t *Target = (target_symb_t *)stringmap_search(TargetCache, Id);
	if (!Target) {
		Target = target_new(target_symb_t, SymbTargetT, Id);
		Target->Context = CurrentContext->Name;
		Target->Name = Name;
	}
	return (target_t *)Target;
}

static int list_update_hash(ml_value_t *Value, SHA256_CTX *Ctx) {
	int8_t ChildHash[SHA256_BLOCK_SIZE];
	target_value_hash(Value, ChildHash);
	sha256_update(Ctx, ChildHash, SHA256_BLOCK_SIZE);
	return 0;
}

static int tree_update_hash(ml_value_t *Key, ml_value_t *Value, SHA256_CTX *Ctx) {
	int8_t ChildHash[SHA256_BLOCK_SIZE];
	target_value_hash(Key, ChildHash);
	sha256_update(Ctx, ChildHash, SHA256_BLOCK_SIZE);
	target_value_hash(Value, ChildHash);
	sha256_update(Ctx, ChildHash, SHA256_BLOCK_SIZE);
	return 0;
}

void target_value_hash(ml_value_t *Value, int8_t Hash[SHA256_BLOCK_SIZE]) {
	if (Value->Type == NilT) {
		memset(Hash, 0, SHA256_BLOCK_SIZE);
	} else if (Value->Type == IntegerT) {
		memset(Hash, 0, SHA256_BLOCK_SIZE);
		*(long *)Hash = ml_integer_value(Value);
		Hash[SHA256_BLOCK_SIZE - 1] = 1;
	} else if (Value->Type == RealT) {
		memset(Hash, 0, SHA256_BLOCK_SIZE);
		*(double *)Hash = ml_real_value(Value);
		Hash[SHA256_BLOCK_SIZE - 1] = 1;
	} else if (Value->Type == StringT) {
		const char *String = ml_string_value(Value);
		size_t Len = strlen(String);
		SHA256_CTX Ctx[1];
		sha256_init(Ctx);
		sha256_update(Ctx, String, Len);
		sha256_final(Ctx, Hash);
	} else if (Value->Type == ListT) {
		SHA256_CTX Ctx[1];
		sha256_init(Ctx);
		ml_list_foreach(Value, Ctx, (void *)list_update_hash);
		sha256_final(Ctx, Hash);
	} else if (Value->Type == TreeT) {
		SHA256_CTX Ctx[1];
		sha256_init(Ctx);
		ml_tree_foreach(Value, Ctx, (void *)tree_update_hash);
		sha256_final(Ctx, Hash);
	} else if (Value->Type == ClosureT) {
		ml_closure_hash(Value, Hash);
	} else if (Value->Type == FileTargetT) {
		target_t *Target = (target_t *)Value;
		SHA256_CTX Ctx[1];
		sha256_init(Ctx);
		sha256_update(Ctx, Target->Id, strlen(Target->Id));
		sha256_final(Ctx, Hash);
	} else if (Value->Type == MetaTargetT) {
		target_t *Target = (target_t *)Value;
		SHA256_CTX Ctx[1];
		sha256_init(Ctx);
		sha256_update(Ctx, Target->Id, strlen(Target->Id));
		sha256_final(Ctx, Hash);
	} else if (Value->Type == ScanTargetT) {
		target_t *Target = (target_t *)Value;
		SHA256_CTX Ctx[1];
		sha256_init(Ctx);
		sha256_update(Ctx, Target->Id, strlen(Target->Id));
		sha256_final(Ctx, Hash);
	} else if (Value->Type == SymbTargetT) {
		target_t *Target = (target_t *)Value;
		SHA256_CTX Ctx[1];
		sha256_init(Ctx);
		sha256_update(Ctx, Target->Id, strlen(Target->Id));
		sha256_final(Ctx, Hash);
	}
}

static time_t target_hash(target_t *Target, time_t PreviousTime, int8_t PreviousHash[SHA256_BLOCK_SIZE]) {
	if (Target->Type == FileTargetT) return target_file_hash((target_file_t *)Target, PreviousTime, PreviousHash);
	if (Target->Type == MetaTargetT) return target_meta_hash((target_meta_t *)Target, PreviousTime, PreviousHash);
	if (Target->Type == ScanTargetT) return target_scan_hash((target_scan_t *)Target, PreviousTime, PreviousHash);
	if (Target->Type == ScanResultsT) return scan_results_hash((scan_results_t *)Target, PreviousTime, PreviousHash);
	if (Target->Type == SymbTargetT) return target_symb_hash((target_symb_t *)Target, PreviousTime, PreviousHash);
	if (Target->Type == ExprTargetT) return target_expr_hash((target_expr_t *)Target, PreviousTime, PreviousHash);
	return 0;
}

static void target_build(target_t *Target) {
	if (Target->Type == FileTargetT) return target_default_build(Target);
	if (Target->Type == MetaTargetT) return target_default_build(Target);
	if (Target->Type == ScanTargetT) return target_scan_build((target_scan_t *)Target);
	if (Target->Type == ExprTargetT) return target_expr_build((target_expr_t *)Target);
	//if (Target->Type == ScanResultsT) return scan_results_build((scan_results_t *)Target);
	//if (Target->Type == SymbTargetT) return target_symb_build((target_symb_t *)Target);
	printf("\e[31mError: not expecting to build %s\e[0m\n", Target->Type->Name);
	exit(1);
}

static int target_missing(target_t *Target) {
	if (Target->Type == FileTargetT) return target_file_missing((target_file_t *)Target);
	return 0;
}

target_t *target_find(const char *Id) {
	target_t *Target = (target_t *)stringmap_search(TargetCache, Id);
	if (Target) return Target;
	if (!memcmp(Id, "file", 4)) return target_file_check(Id + 5, Id[5] == '/');
	if (!memcmp(Id, "symb", 4)) {
		Id = concat(Id, 0);
		target_symb_t *Target = target_new(target_symb_t, SymbTargetT, Id);
		const char *Name;
		for (Name = Id + strlen(Id); --Name > Id + 5;) {
			if (*Name == '/') break;
		}
		size_t PathLength = Name - Id - 5;
		char *Path = snew(PathLength + 1);
		memcpy(Path, Id + 5, PathLength);
		Path[PathLength] = 0;
		Target->Context = Path;
		Target->Name = Name + 1;
		return (target_t *)Target;
	}
	return 0;
}

target_t *target_get(const char *Id) {
	return (target_t *)stringmap_search(TargetCache, Id);
}

int target_print_fn(const char *Key, target_t *Target, void *Data) {
	printf("%s\n", Target->Id);
	return 0;
}

void target_list() {
	stringmap_foreach(TargetCache, 0, (void *)target_print_fn);
}

static pthread_t *Threads;
int RunningThreads;

static void *target_thread_fn(void *Arg) {
	int Index = (int)Arg;
	printf("[%d]: Starting build thread\n", Index);
	pthread_mutex_lock(GlobalLock);
	for (;;) {
		while (!BuildQueue) {
			printf("[%d]: No target in build queue, %d threads running\n", Index, RunningThreads);
			if (--RunningThreads == 0) {
				pthread_cond_signal(TargetAvailable);
				pthread_mutex_unlock(GlobalLock);
				return 0;
			}
			pthread_cond_wait(TargetAvailable, GlobalLock);
			++RunningThreads;
		}
		target_t *Target = BuildQueue;
		BuildQueue = Target->Next;
		Target->Next = 0;
		target_do_build(Index, Target);
	}
	return 0;
}

void target_threads_start(int NumThreads) {
	pthread_mutex_init(GlobalLock, 0);
	pthread_mutex_lock(GlobalLock);
	pthread_cond_init(TargetAvailable, 0);
	RunningThreads = NumThreads + 1;
	Threads = anew(pthread_t, NumThreads);
	for (int I = 0; I < NumThreads; ++I) GC_pthread_create(Threads + I, 0, target_thread_fn, (void *)I);
}

void target_threads_wait(int NumThreads) {
	--RunningThreads;
	pthread_cond_broadcast(TargetAvailable);
	pthread_mutex_unlock(GlobalLock);
	for (int I = 0; I < NumThreads; ++I) GC_pthread_join(Threads[I], 0);
}

void target_init() {
	TargetT = ml_class(AnyT, "target");
	FileTargetT = ml_class(TargetT, "file-target");
	MetaTargetT = ml_class(TargetT, "meta-target");
	ExprTargetT = ml_class(TargetT, "expr-target");
	ScanTargetT = ml_class(TargetT, "scan-target");
	ScanResultsT = ml_class(TargetT, "scan-results");
	SymbTargetT = ml_class(TargetT, "symb-target");
	SymbTargetT->deref = symb_target_deref;
	SymbTargetT->assign = symb_target_assign;
	SHA256Method = ml_method("sha256");
	MissingMethod = ml_method("missing");
	ml_method_by_name("append", 0, target_file_stringify, StringBufferT, FileTargetT, 0);
	ml_method_by_name("append", 0, target_expr_stringify, StringBufferT, ExprTargetT, 0);
	ml_method_by_name("[]", 0, target_depend, TargetT, AnyT, 0);
	ml_method_by_name("[]", 0, scan_results_depend, ScanResultsT, AnyT, 0);
	ml_method_by_name("string", 0, target_file_to_string, FileTargetT, 0);
	ml_method_by_name("string", 0, target_expr_to_string, ExprTargetT, 0);
	ml_method_by_name("=>", 0, target_set_build, TargetT, AnyT, 0);
	ml_method_by_name("=>", 0, scan_results_set_build, ScanResultsT, AnyT, 0);
	ml_method_by_name("/", 0, target_file_div, FileTargetT, StringT, 0);
	ml_method_by_name("%", 0, target_file_mod, FileTargetT, StringT, 0);
	ml_method_by_name("dir", 0, target_file_dir, FileTargetT, 0);
	ml_method_by_name("basename", 0, target_file_basename, FileTargetT, 0);
	ml_method_by_name("exists", 0, target_file_exists, FileTargetT, 0);
	ml_method_by_name("copy", 0, target_file_copy, FileTargetT, FileTargetT, 0);
	ml_method_by_name("scan", 0, target_scan_new, TargetT, 0);
}
