#include "target.h"
#include "rabs.h"
#include "util.h"
#include "context.h"
#include "targetcache.h"
#include "targetwatch.h"
#include "cache.h"
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
#include <regex.h>
#include <dirent.h>
#include <ml_file.h>
#include <limits.h>

enum {
	STATE_UNCHECKED = 0,
	STATE_CHECKING = -1,
	STATE_QUEUED = -2
};

typedef struct target_file_t target_file_t;
typedef struct target_meta_t target_meta_t;
typedef struct target_expr_t target_expr_t;
typedef struct target_scan_t target_scan_t;
typedef struct scan_results_t scan_results_t;
typedef struct target_symb_t target_symb_t;

extern const char *RootPath;
static int QueuedTargets = 0, BuiltTargets = 0, NumTargets = 0;
int StatusUpdates = 0;
int MonitorFiles = 0;

pthread_mutex_t GlobalLock[1] = {PTHREAD_MUTEX_INITIALIZER};
static pthread_cond_t TargetAvailable[1] = {PTHREAD_COND_INITIALIZER};

static ml_value_t *SHA256Method;
static ml_value_t *MissingMethod;
static ml_value_t *StringMethod;
static ml_value_t *AppendMethod;

static ml_type_t *TargetT;

__thread target_t *CurrentTarget = 0;

static target_t *BuildQueue = 0;
static int SpareThreads = 0;

static void target_wait(target_t *Target);

void target_value_hash(ml_value_t *Value, BYTE Hash[SHA256_BLOCK_SIZE]);

static time_t target_hash(target_t *Target, time_t PreviousTime, BYTE PreviousHash[SHA256_BLOCK_SIZE]);
static void target_build(target_t *Target);
static int target_missing(target_t *Target, int LastChecked);

int depends_hash_fn(target_t *Depend, BYTE Hash[SHA256_BLOCK_SIZE]) {
	for (int I = 0; I < SHA256_BLOCK_SIZE; ++I) Hash[I] ^= Depend->Hash[I];
	//for (int I = 0; I < SHA256_BLOCK_SIZE; ++I) printf(" %x", Depend->Hash[I]);
	return 0;
}

void target_depends_add(target_t *Target, target_t *Depend) {
	if (Target != Depend) targetset_insert(Target->Depends, Depend);
}

static int depends_print_fn(const char *DependId, target_t *Depend, int *DependsLastUpdated) {
	printf("\t\e[35m%s[%d]\e[0m\n", Depend->Id, Depend->LastUpdated);
	return 0;
}

static void target_queue_build(target_t *Target) {
	//printf("Adding %s to queue\n", Target->Id);
	Target->Next = BuildQueue;
	BuildQueue = Target;
}

static int depends_updated_fn(target_t *Affect, target_t *Target) {
	if (Target->LastUpdated > Affect->DependsLastUpdated) {
		if (Target->LastUpdated == CurrentVersion) printf("Updating %s due to %s\n", Affect->Id, Target->Id);
		Affect->DependsLastUpdated = Target->LastUpdated;
	}
	if (--Affect->WaitCount == 0) target_queue_build(Affect);
	//printf("\e[35mDecreasing wait count for %s to %d\e[0m\n", Affect->Id, Affect->WaitCount);
	return 0;
}

static void target_do_build(int ThreadIndex, target_t *Target) {
	//printf("\e[32m[%d] Building %s (%d targets, %d bytes)\e[0m\n", ThreadIndex, Target->Id, NumTargets, GC_get_heap_size());
	BYTE Previous[SHA256_BLOCK_SIZE];
	int LastUpdated, LastChecked;
	time_t FileTime = 0;
	cache_hash_get(Target, &LastUpdated, &LastChecked, &FileTime, Previous);
	//if (!strcmp(Target->Id, "scan:file:dev/web/tools/rabs/guide::WEBFILES")) asm("int3");
	//if (!strcmp(Target->Id, "file:web/tools/rabs/guide/chapter3.xhtml")) asm("int3");
	if (Target->Build) {
		CurrentContext = Target->BuildContext;
		if ((Target->DependsLastUpdated > LastChecked) || target_missing(Target, LastChecked)) {
			CurrentTarget = Target;
			target_build(Target);
			cache_build_hash_set(Target);
		}
	}
	FileTime = target_hash(Target, FileTime, Previous);
	if (!LastUpdated || memcmp(Previous, Target->Hash, SHA256_BLOCK_SIZE)) {
		Target->LastUpdated = CurrentVersion;
		cache_hash_set(Target, FileTime);
		cache_depends_set(Target, Target->Depends);
	} else {
		Target->LastUpdated = LastUpdated;
		cache_last_check_set(Target, FileTime);
	}
	//GC_gcollect();
	++BuiltTargets;
	if (StatusUpdates) printf("\e[35m%d / %d\e[0m #%d Built \e[32m%s\e[0m at version %d\n", BuiltTargets, QueuedTargets, ThreadIndex, Target->Id, Target->LastUpdated);
	targetset_foreach(Target->Affects, Target, (void *)depends_updated_fn);
	memset(Target->Depends, 0, sizeof(Target->Depends));
	//memset(Target->Affects, 0, sizeof(Target->Affects));
	pthread_cond_broadcast(TargetAvailable);
}

int depends_update_fn(target_t *Depend, target_t *Target) {
	if (Depend->LastUpdated == STATE_CHECKING) {
		printf("\e[31mError: build cycle with %s -> %s\e[0m\n", Target->Id, Depend->Id);
		exit(1);
	}
	if (Depend->LastUpdated == STATE_UNCHECKED) target_update(Depend);
	if (Depend->LastUpdated == STATE_QUEUED) {
		if (!targetset_insert(Depend->Affects, Target)) ++Target->WaitCount;
	} else {
		targetset_insert(Depend->Affects, Target);
		if (Depend->LastUpdated > Target->DependsLastUpdated) {
			if (Depend->LastUpdated == CurrentVersion) printf("Updating %s due to %s\n", Target->Id, Depend->Id);
			Target->DependsLastUpdated = Depend->LastUpdated;
		}
	}
	return 0;
}

static int affects_refresh_fn(target_t *Affect, target_t *Target) {
	printf("%s -> %s\n", Target->Id, Affect->Id);
	if (Affect->LastUpdated != STATE_QUEUED) {
		Affect->LastUpdated = STATE_QUEUED;
		++QueuedTargets;
		targetset_foreach(Affect->Affects, Affect, (void *)affects_refresh_fn);
	}
	if (!targetset_insert(Affect->Depends, Target)) ++Affect->WaitCount;
	return 0;
}

void target_update(target_t *Target) {
	if (Target->LastUpdated == STATE_CHECKING) {
		fprintf(stderr, "\e[31mError: build cycle with %s\e[0m\n", Target->Id);
		exit(1);
	}
	if (Target->LastUpdated == STATE_UNCHECKED) {
		Target->LastUpdated = STATE_CHECKING;
		++QueuedTargets;
		//printf("Added new target to queue: %s\n", Target->Id);
		targetset_foreach(Target->Depends, Target, (void *)depends_update_fn);
		if (Target->Build && Target->Build->Type == MLClosureT) {
			ml_closure_hash(Target->Build, Target->BuildHash);
			BYTE Previous[SHA256_BLOCK_SIZE];
			cache_build_hash_get(Target, Previous);
			if (memcmp(Previous, Target->BuildHash, SHA256_BLOCK_SIZE)) {
				Target->DependsLastUpdated = CurrentVersion;
			}
		}
		if (Target->DependsLastUpdated < CurrentVersion) {
			targetset_t *PreviousDetectedDepends = cache_depends_get(Target);
			if (PreviousDetectedDepends) {
				targetset_foreach(PreviousDetectedDepends, Target, (void *)depends_update_fn);
			}
		}
		Target->LastUpdated = STATE_QUEUED;
		//printf("\e[32m[%d/%d] \e[33mtarget_update(%s) -> waiting on %d\e[0m\n", BuiltTargets, TargetCache->Size, Target->Id, Target->WaitCount);
		if (Target->WaitCount == 0) target_queue_build(Target);
	}
}

void target_recheck(target_t *Target) {
	if (Target == (target_t *)-1) restart();
	printf("Rechecking %s\n", Target->Id);
	cache_bump_version();
	++QueuedTargets;
	targetset_foreach(Target->Affects, Target, (void *)affects_refresh_fn);
	target_queue_build(Target);
	pthread_cond_broadcast(TargetAvailable);
}

int depends_query_fn(const char *Id, target_t *Depends, int *DependsLastUpdated) {
	target_query(Depends);
	if (Depends->LastUpdated > *DependsLastUpdated) *DependsLastUpdated = Depends->LastUpdated;
	return 0;
}

void target_query(target_t *Target) {
	/*if (Target->LastUpdated == STATE_CHECKING) {
		printf("\e[31mError: build cycle with %s\e[0m\n", Target->Id);
		exit(1);
	}
	if (Target->LastUpdated == STATE_UNCHECKED) {
		Target->LastUpdated = STATE_CHECKING;
		printf("Target: %s\n", Target->Id);
		int DependsLastUpdated = 0;
		stringmap_foreach(Target->Depends, &DependsLastUpdated, (void *)depends_query_fn);
		BYTE Previous[SHA256_BLOCK_SIZE];
		int LastUpdated, LastChecked;
		time_t FileTime = 0;
		if (Target->Build) {
			BYTE BuildHash[SHA256_BLOCK_SIZE];
			ml_closure_hash(Target->Build, BuildHash);
			stringmap_t *PreviousDetectedDepends = cache_depends_get(Target);
			const char *BuildId = concat(Target->Id, "::build", NULL);
			cache_hash_get(BuildId, &LastUpdated, &LastChecked, &FileTime, Previous);
			if (!LastUpdated || memcmp(Previous, BuildHash, SHA256_BLOCK_SIZE)) {
				DependsLastUpdated = CurrentVersion;
				printf("\t\e[35m<build function updated>\e[0m\n");
			} else if (PreviousDetectedDepends) {
				stringmap_foreach(PreviousDetectedDepends, &DependsLastUpdated, (void *)depends_query_fn);
			}
			cache_hash_get(Target->Id, &LastUpdated, &LastChecked, &FileTime, Previous);
			if ((DependsLastUpdated > LastChecked) || target_missing(Target, LastChecked)) {
				printf("\e[33mtarget_build(%s) Depends = %d, Last Updated = %d\e[0m\n", Target->Id, DependsLastUpdated, LastChecked);
				stringmap_foreach(Target->Depends, &DependsLastUpdated, (void *)depends_print_fn);
				if (PreviousDetectedDepends) {
					stringmap_foreach(PreviousDetectedDepends, &DependsLastUpdated, (void *)depends_print_fn);
				}
			}
		} else {
			cache_hash_get(Target->Id, &LastUpdated, &LastChecked, &FileTime, Previous);	
		}
	}*/
}

void target_depends_auto(target_t *Depend) {
	if (CurrentTarget && CurrentTarget != Depend) targetset_insert(CurrentTarget->Depends, Depend);
}

static target_t *target_alloc(int Size, ml_type_t *Type, const char *Id, target_t **Slot) {
	++NumTargets;
	target_t *Target = (target_t *)GC_MALLOC(Size);
	Target->Type = Type;
	Target->Id = Id;
	Target->IdLength = strlen(Id);
	Target->IdHash = stringmap_hash(Id);
	Target->Build = 0;
	Target->LastUpdated = STATE_UNCHECKED;
	Slot[0] = Target;
	return Target;
}

#define target_new(type, Type, Id, Slot) ((type *)target_alloc(sizeof(type), Type, Id, Slot))

struct target_file_t {
	TARGET_FIELDS
	int Absolute;
	const char *Path;
};

static ml_type_t *FileTargetT;

static ml_value_t *target_file_stringify(void *Data, int Count, ml_value_t **Args) {
	ml_stringbuffer_t *Buffer = (ml_stringbuffer_t *)Args[0];
	target_file_t *Target = (target_file_t *)Args[1];
	if (Target->Absolute) {
		ml_stringbuffer_add(Buffer, Target->Path, strlen(Target->Path));
	} else if (Target->Path[0]) {
		const char *Path = vfs_resolve(Target->BuildContext->Mounts, concat(RootPath, "/", Target->Path, 0));
		ml_stringbuffer_add(Buffer, Path, strlen(Path));
	} else {
		ml_stringbuffer_add(Buffer, RootPath, strlen(RootPath));
	}
	return MLSome;
}

static ml_value_t *target_file_to_string(void *Data, int Count, ml_value_t **Args) {
	target_file_t *Target = (target_file_t *)Args[0];
	if (Target->Absolute) {
		return ml_string(Target->Path, -1);
	} else if (Target->Path[0]) {
		const char *Path = vfs_resolve(Target->BuildContext->Mounts, concat(RootPath, "/", Target->Path, 0));
		return ml_string(Path, -1);
	} else {
		return ml_string(RootPath, -1);
	}
}

static time_t target_file_hash(target_file_t *Target, time_t PreviousTime, BYTE PreviousHash[SHA256_BLOCK_SIZE]) {
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
	pthread_mutex_unlock(GlobalLock);
	if (!S_ISREG(Stat->st_mode)) {
		memset(Target->Hash, -1, SHA256_BLOCK_SIZE);
		memcpy(Target->Hash, &Stat->st_mtim, sizeof(Stat->st_mtim));
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
				fprintf(stderr, "\e[31mError: read error: %s\e[0m\n", FileName);
				exit(1);
			}
			sha256_update(Ctx, Buffer, Count);
		}
		close(File);
		sha256_final(Ctx, Target->Hash);
	}
	pthread_mutex_lock(GlobalLock);
	if (MonitorFiles && !Target->Build) {
		targetwatch_add(FileName, (target_t *)Target);
	}
	return Stat->st_mtime;
}

static void target_default_build(target_t *Target) {
	ml_value_t *Result = ml_inline(Target->Build, 1, Target);
	if (Result->Type == MLErrorT) {
		fprintf(stderr, "\e[31mError: %s: %s\n\e[0m", Target->Id, ml_error_message(Result));
		const char *Source;
		int Line;
		for (int I = 0; ml_error_trace(Result, I, &Source, &Line); ++I) fprintf(stderr, "\e[31m\t%s:%d\n\e[0m", Source, Line);
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
	Path = concat(Path, NULL);
	const char *Id = concat("file:", Path, NULL);
	target_t **Slot = targetcache_lookup(Id);
	if (!Slot[0]) {
		target_file_t *Target = target_new(target_file_t, FileTargetT, Id, Slot);
		Target->Absolute = Absolute;
		Target->Path = Path;
		Target->BuildContext = CurrentContext;
	}
	return Slot[0];
}

ml_value_t *target_file_new(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	const char *Path = ml_string_value(Args[0]);
	if (!Path[0]) {
		Path = concat(RootPath, CurrentContext->Path, NULL);
	} else if (Path[0] != '/') {
		Path = concat(RootPath, CurrentContext->Path, "/", Path, NULL);
	}
	Path = vfs_unsolve(CurrentContext->Mounts, Path);
	const char *Relative = match_prefix(Path, RootPath);
	target_t *Target;
	if (Relative) {
		Target = target_file_check(Relative + 1, 0);
	} else {
		Target = target_file_check(Path, 1);
	}
	return (ml_value_t *)Target;
}

ml_value_t *target_file_dir(void *Data, int Count, ml_value_t **Args) {
	target_file_t *FileTarget = (target_file_t *)Args[0];
	char *Path;
	int Absolute;
	if (Count > 1 && Args[1] != MLNil && !FileTarget->Absolute) {
		Path = vfs_resolve(FileTarget->BuildContext->Mounts, concat(RootPath, "/", FileTarget->Path, 0));
		Absolute = 1;
	} else {
		Path = concat(FileTarget->Path, NULL);
		Absolute = FileTarget->Absolute;
	}
	char *Last = Path;
	for (char *P = Path; *P; ++P) if (*P == '/') Last = P;
	*Last = 0;
	target_t *Target = target_file_check(Path, Absolute);
	return (ml_value_t *)Target;
}

typedef struct target_file_ls_t target_file_ls_t;

struct target_file_ls_t {
	ml_value_t *Results;
	regex_t *Regex;
};

static int target_file_ls_fn(target_file_ls_t *Ls, const char *Path) {
	DIR *Dir = opendir(Path);
	if (!Dir) {
		Ls->Results = ml_error("DirError", "failed to open directory %s", Path);
		return 1;
	}
	struct dirent *Entry = readdir(Dir);
	while (Entry) {
		if (
			strcmp(Entry->d_name, ".") &&
			strcmp(Entry->d_name, "..") &&
			!(Ls->Regex && regexec(Ls->Regex, Entry->d_name, 0, 0, 0))
		) {
			const char *Absolute = concat(Path, "/", Entry->d_name, NULL);
			const char *Relative = match_prefix(Absolute, RootPath);
			target_t *File;
			if (Relative) {
				File = target_file_check(Relative + 1, 0);
			} else {
				File = target_file_check(Absolute, 1);
			}
			ml_list_append(Ls->Results, (ml_value_t *)File);
		}
		Entry = readdir(Dir);
	}
	closedir(Dir);
	return 0;
}

ml_value_t *target_file_ls(void *Data, int Count, ml_value_t **Args) {
	target_file_ls_t Ls[1] = {{ml_list(), NULL}};
	if (Count > 1) {
		const char *Pattern = ml_string_value(Args[1]);
		Ls->Regex = new(regex_t);
		int Error = regcomp(Ls->Regex, Pattern, REG_NOSUB | REG_EXTENDED);
		if (Error) {
			size_t Length = regerror(Error, Ls->Regex, NULL, 0);
			char *Message = snew(Length + 1);
			regerror(Error, Ls->Regex, Message, Length);
			regfree(Ls->Regex);
			return ml_error("RegexError", "%s", Message);
		}
	}
	target_file_t *Target = (target_file_t *)Args[0];
	if (Target->Absolute) {
		target_file_ls_fn(Ls, Target->Path);
	} else if (Target->Path[0]) {
		vfs_resolve_foreach(CurrentContext->Mounts, concat(RootPath, "/", Target->Path, 0), Ls, (void *)target_file_ls_fn);
	} else {
		vfs_resolve_foreach(CurrentContext->Mounts, RootPath, Ls, (void *)target_file_ls_fn);
	}
	return Ls->Results;
}

ml_value_t *target_file_dirname(void *Data, int Count, ml_value_t **Args) {
	target_file_t *Target = (target_file_t *)Args[0];
	char *Path;
	if (Target->Absolute) {
		Path = concat(Target->Path, NULL);
	} else if (Target->Path[0]) {
		Path = vfs_resolve(Target->BuildContext->Mounts, concat(RootPath, "/", Target->Path, 0));
	} else {
		Path = concat(RootPath);
	}
	char *Last = Path;
	for (char *P = Path; *P; ++P) if (*P == '/') Last = P;
	*Last = 0;
	return ml_string(Path, -1);
}

ml_value_t *target_file_basename(void *Data, int Count, ml_value_t **Args) {
	target_file_t *Target = (target_file_t *)Args[0];
	const char *Path = Target->Path;
	const char *Last = Path - 1;
	for (const char *P = Path; *P; ++P) if (*P == '/') Last = P;
	return ml_string(concat(Last + 1, 0), -1);
}

ml_value_t *target_file_extension(void *Data, int Count, ml_value_t **Args) {
	target_file_t *FileTarget = (target_file_t *)Args[0];
	const char *Path = FileTarget->Path;
	const char *LastDot = Path;
	const char *LastSlash = Path;
	for (const char *P = Path; *P; ++P) {
		if (*P == '.') LastDot = P;
		if (*P == '/') LastSlash = P;
	}
	if (LastDot > LastSlash) {
		return ml_string(concat(LastDot + 1, 0), -1);
	} else {
		return ml_string("", 0);
	}
}

ml_value_t *target_file_relative(void *Data, int Count, ml_value_t **Args) {
	target_file_t *FileTarget = (target_file_t *)Args[0];
	target_file_t *BaseTarget = (target_file_t *)Args[1];
	const char *Relative = match_prefix(FileTarget->Path, BaseTarget->Path);
	if (Relative) {
		if (Relative[0] == '/') ++Relative;
		return ml_string(Relative, -1);
	} else {
		return MLNil;
	}
}

ml_value_t *target_file_exists(void *Data, int Count, ml_value_t **Args) {
	target_file_t *Target = (target_file_t *)Args[0];
	if (Target->Build /*&& Target->Build->Type == MLClosureT*/) return (ml_value_t *)Target;
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
		return MLNil;
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
	return MLNil;
}

ml_value_t *target_file_div(void *Data, int Count, ml_value_t **Args) {
	target_file_t *FileTarget = (target_file_t *)Args[0];
	if (ml_string_length(Args[1]) == 0) return Args[0];
	const char *Path = concat(FileTarget->Path, "/", ml_string_value(Args[1]), NULL);
	target_t *Target = target_file_check(Path, FileTarget->Absolute);
	return (ml_value_t *)Target;
}

ml_value_t *target_file_mod(void *Data, int Count, ml_value_t **Args) {
	target_file_t *FileTarget = (target_file_t *)Args[0];
	const char *Replacement = ml_string_value(Args[1]);
	char *Path = concat(FileTarget->Path, ".", Replacement, NULL);
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

ml_value_t *target_file_open(void *Data, int Count, ml_value_t **Args) {
	target_file_t *Target = (target_file_t *)Args[0];
	const char *Mode = ml_string_value(Args[1]);
	const char *FileName;
	if (Target->Absolute) {
		FileName = Target->Path;
	} else if (Mode[0] == 'r') {
		FileName = vfs_resolve(CurrentContext->Mounts, concat(RootPath, "/", Target->Path, 0));
	} else {
		FileName = concat(RootPath, "/", Target->Path, NULL);
	}
	FILE *Handle = fopen(FileName, Mode);
	if (!Handle) {
		return ml_error("FileError", "error opening %s", FileName);
	} else {
		return ml_file_new(Handle);
	}
}

#define TARGET_FILE_IS(NAME, TEST) \
ml_value_t *target_file_is_ ## NAME(void *Data, int Count, ml_value_t **Args) { \
	target_file_t *Target = (target_file_t *)Args[0]; \
	const char *FileName; \
	if (Target->Absolute) { \
		FileName = Target->Path; \
	} else { \
		FileName = vfs_resolve(CurrentContext->Mounts, concat(RootPath, "/", Target->Path, 0)); \
	} \
	struct stat Stat[1]; \
	if (!stat(FileName, Stat)) { \
		if (TEST(Stat->st_mode)) { \
			return ml_string(#NAME, -1); \
		} else { \
			return MLNil; \
		} \
	} else { \
		return ml_error("StatError", "could not read file attributes"); \
	} \
}

TARGET_FILE_IS(dir, S_ISDIR);
TARGET_FILE_IS(chr, S_ISCHR);
TARGET_FILE_IS(blk, S_ISBLK);
TARGET_FILE_IS(reg, S_ISREG);
TARGET_FILE_IS(fifo, S_ISFIFO);
TARGET_FILE_IS(lnk, S_ISLNK);
TARGET_FILE_IS(sock, S_ISSOCK);

static int mkdir_p(char *Path) {
	if (!Path[0]) return -1;
	struct stat Stat[1];
	for (char *P = Path + 1; P[0]; ++P) {
		if (P[0] == '/') {
			P[0] = 0;
			if (lstat(Path, Stat) < 0) {
				int Result = mkdir(Path, 0777);
				if (Result < 0) return Result;
			}
			P[0] = '/';
		}
	}
	if (lstat(Path, Stat) < 0) {
		int Result = mkdir(Path, 0777);
		if (Result < 0) return Result;
	}
	return 0;
}

ml_value_t *target_file_mkdir(void *Data, int Count, ml_value_t **Args) {
	target_file_t *Target = (target_file_t *)Args[0];
	char *Path;
	if (Target->Absolute) {
		Path = concat(Target->Path, NULL);
	} else {
		Path = concat(RootPath, "/", Target->Path, NULL);
	}
	if (mkdir_p(Path) < 0) {
		return ml_error("FileError", "error creating directory %s", Path);
	}
	return Args[0];
}

static int rmdir_p(char *Buffer, char *End) {
	if (!Buffer[0]) return -1;
	struct stat Stat[1];
	if (lstat(Buffer, Stat)) return 0;
	if (S_ISDIR(Stat->st_mode)) {
		DIR *Dir = opendir(Buffer);
		if (!Dir) return 1;
		End[0] = '/';
		struct dirent *Entry = readdir(Dir);
		while (Entry) {
			if (strcmp(Entry->d_name, ".") && strcmp(Entry->d_name, "..")) {
				char *End2 = stpcpy(End + 1, Entry->d_name);
				if (rmdir_p(Buffer, End2)) {
					closedir(Dir);
					return 1;
				}
			}
			Entry = readdir(Dir);
		}
		closedir(Dir);
		End[0] = 0;
		if (rmdir(Buffer)) return 1;
	} else {
		if (unlink(Buffer)) return 1;
	}
	return 0;
}

ml_value_t *target_file_rmdir(void *Data, int Count, ml_value_t **Args) {
	target_file_t *Target = (target_file_t *)Args[0];
	const char *Path;
	if (Target->Absolute) {
		Path = concat(Target->Path, NULL);
	} else {
		Path = concat(RootPath, "/", Target->Path, NULL);
	}
	char *Buffer = snew(PATH_MAX);
	char *End = stpcpy(Buffer, Path);
	if (rmdir_p(Buffer, End) < 0) {
		return ml_error("FileError", "error removing file / directory %s", Buffer);
	}
	return Args[0];
}

ml_value_t *target_file_chdir(void *Data, int Count, ml_value_t **Args) {
	target_file_t *Target = (target_file_t *)Args[0];
	if (Target->Absolute) {
		char *Path2 = GC_malloc_atomic_uncollectable(strlen(Target->Path) + 1);
		CurrentDirectory = strcpy(Path2, Target->Path);
	} else {
		const char *Path = concat(RootPath, "/", Target->Path, NULL);
		char *Path2 = GC_malloc_atomic_uncollectable(strlen(Path) + 1);
		CurrentDirectory = strcpy(Path2, Path);
	}
	return Args[0];
}

struct target_meta_t {
	TARGET_FIELDS
	const char *Name;
};

static ml_type_t *MetaTargetT;

static time_t target_meta_hash(target_meta_t *Target, time_t PreviousTime, BYTE PreviousHash[SHA256_BLOCK_SIZE]) {
	memset(Target->Hash, 0, SHA256_BLOCK_SIZE);
	memcpy(Target->Hash, &Target->DependsLastUpdated, sizeof(Target->DependsLastUpdated));
	return 0;
}

ml_value_t *target_meta_new(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	const char *Name = ml_string_value(Args[0]);
	const char *Id = concat("meta:", CurrentContext->Path, "::", Name, NULL);
	target_t **Slot = targetcache_lookup(Id);
	if (!Slot[0]) {
		target_meta_t *Target = target_new(target_meta_t, MetaTargetT, Id, Slot);
		Target->Name = Name;
	}
	return (ml_value_t *)Slot[0];
}

struct target_expr_t {
	TARGET_FIELDS
};

static ml_type_t *ExprTargetT;

static ml_value_t *target_expr_stringify(void *Data, int Count, ml_value_t **Args) {
	ml_stringbuffer_t *Buffer = (ml_stringbuffer_t *)Args[0];
	target_expr_t *Target = (target_expr_t *)Args[1];
	target_wait((target_t *)Target);
	target_depends_auto((target_t *)Target);
	ml_value_t *Value = cache_expr_get((target_t *)Target);
	return ml_inline(AppendMethod, 2, Buffer, Value);
}

static ml_value_t *target_expr_to_string(void *Data, int Count, ml_value_t **Args) {
	target_expr_t *Target = (target_expr_t *)Args[0];
	target_wait((target_t *)Target);
	target_depends_auto((target_t *)Target);
	ml_value_t *Value = cache_expr_get((target_t *)Target);
	return ml_inline(StringMethod, 1, Value);
}

static time_t target_expr_hash(target_expr_t *Target, time_t PreviousTime, BYTE PreviousHash[SHA256_BLOCK_SIZE]) {
	ml_value_t *Value = cache_expr_get((target_t *)Target);
	if (Value) target_value_hash(Value, Target->Hash);
	return 0;
}

static void target_expr_build(target_expr_t *Target) {
	ml_value_t *Result = ml_inline(Target->Build, 1, Target);
	if (Result->Type == MLErrorT) {
		fprintf(stderr, "\e[31mError: %s: %s\n\e[0m", Target->Id, ml_error_message(Result));
		const char *Source;
		int Line;
		for (int I = 0; ml_error_trace(Result, I, &Source, &Line); ++I) fprintf(stderr, "\e[31m\t%s:%d\n\e[0m", Source, Line);
		exit(1);
	}
	cache_expr_set((target_t *)Target, Result);
}

ml_value_t *target_expr_new(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	const char *Name = ml_string_value(Args[0]);
	const char *Id = concat("expr:", CurrentContext->Path, "::", Name, NULL);
	target_t **Slot = targetcache_lookup(Id);
	if (!Slot[0]) {
		target_expr_t *Target = target_new(target_expr_t, ExprTargetT, Id, Slot);
		Slot[0] = (target_t *)Target;
	}
	return (ml_value_t *)Slot[0];
}

static int target_depends_single(ml_value_t *Arg, target_t *Target) {
	if (Arg->Type == MLListT) {
		return ml_list_foreach(Arg, Target, (void *)target_depends_single);
	} else if (Arg->Type == MLStringT) {
		target_t *Depend = target_symb_new(ml_string_value(Arg));
		targetset_insert(Target->Depends, Depend);
	} else if (ml_is(Arg, TargetT)) {
		target_t *Depend = (target_t *)Arg;
		targetset_insert(Target->Depends, Depend);
	} else if (Arg == MLNil) {
		return 0;
	} else {
		return 1;
	}
	return 0;
}

ml_value_t *target_depend(void *Data, int Count, ml_value_t **Args) {
	target_t *Target = (target_t *)Args[0];
	for (int I = 1; I < Count; ++I) {
		int Error = target_depends_single(Args[I], Target);
		if (Error) return ml_error("TypeError", "Invalid value in dependency list");
	}
	return Args[0];
}

ml_value_t *target_set_build(void *Data, int Count, ml_value_t **Args) {
	target_t *Target = (target_t *)Args[0];
	//printf("target_set_build(%s)\n", Target->Id);
	//if (Target->Build) return ml_error("ParameterError", "build already defined for %s", Target->Id);
	Target->Build = Args[1];
	Target->BuildContext = CurrentContext;
	return Args[0];
}

ml_value_t *target_recheck_value(void *Data, int Count, ml_value_t **Args) {
	target_recheck((target_t *)Args[0]);
	return Args[0];
}

struct target_scan_t {
	TARGET_FIELDS
	const char *Name;
	target_t *Source;
	scan_results_t *Results;
	ml_value_t *Scan, *Rebuild;
	int Recursive;
};

static ml_type_t *ScanTargetT;

struct scan_results_t {
	TARGET_FIELDS
	target_scan_t *Scan;
};

static ml_type_t *ScanResultsT;

static int scan_results_affects_fn(target_t *Target, target_t *Scan) {
	targetset_insert(Target->Affects, Scan);
	targetset_insert(Scan->Depends, Target);
	return 0;
}

static time_t target_scan_hash(target_scan_t *Target, time_t PreviousTime, BYTE PreviousHash[SHA256_BLOCK_SIZE]) {
	targetset_t *Scans = cache_scan_get((target_t *)Target->Results);
	if (Scans) targetset_foreach(Scans, Target->Results, (void *)depends_update_fn);
	memset(Target->Hash, 0, SHA256_BLOCK_SIZE);
	return 0;
}

static int target_scan_missing(target_scan_t *Target, int LastChecked) {
	if (!LastChecked) return 1;
	BYTE Hash[SHA256_BLOCK_SIZE];
	int LastUpdated, ResultsLastChecked;
	time_t Time = 0;
	cache_hash_get((target_t *)Target->Results, &LastUpdated, &ResultsLastChecked, &Time, Hash);
	return LastChecked != ResultsLastChecked;
}

ml_value_t *scan_results_depend(void *Data, int Count, ml_value_t **Args) {
	target_t *Target = (target_t *)((scan_results_t *)Args[0])->Scan;
	for (int I = 1; I < Count; ++I) {
		int Error = target_depends_single(Args[I], Target);
		if (Error) return ml_error("TypeError", "Invalid value in dependency list");
	}
	return Args[0];
}

ml_value_t *scan_results_set_build(void *Data, int Count, ml_value_t **Args) {
	target_scan_t *Target = ((scan_results_t *)Args[0])->Scan;
	//printf("scan_results_set_build(%s)\n", Target->Id);
	Target->Build = Args[1];
	Target->BuildContext = CurrentContext;
	Target->LastUpdated = STATE_UNCHECKED;
	return Args[0];
}

ml_value_t *target_scan_source(void *Data, int Count, ml_value_t **Args) {
	target_scan_t *Target = (target_scan_t *)Args[0];
	return (ml_value_t *)Target->Source;
}

static time_t scan_results_hash(scan_results_t *Target, time_t PreviousTime, BYTE PreviousHash[SHA256_BLOCK_SIZE]) {
	targetset_t *Scans = cache_scan_get((target_t *)Target);
	memset(Target->Hash, 0, SHA256_BLOCK_SIZE);
	if (Scans) targetset_foreach(Scans, Target->Hash, (void *)depends_hash_fn);
	return 0;
}

static int build_scan_target_list(target_t *Depend, targetset_t *Scans) {
	targetset_insert(Scans, Depend);
	return 0;
}

static void target_scan_build(target_scan_t *Target) {
	ml_value_t *Result = ml_inline(Target->Build, 1, Target);
	if (Result->Type == MLErrorT) {
		fprintf(stderr, "\e[31mError: %s: %s\n\e[0m", Target->Id, ml_error_message(Result));
		const char *Source;
		int Line;
		for (int I = 0; ml_error_trace(Result, I, &Source, &Line); ++I) fprintf(stderr, "\e[31m\t%s:%d\n\e[0m", Source, Line);
		exit(1);
	}
	targetset_t Scans[1] = {TARGETSET_INIT};
	ml_list_foreach(Result, Scans, (void *)build_scan_target_list);
	cache_depends_set((target_t *)Target->Results, Scans);
	if (Target->Recursive) {
		targetset_foreach(Scans, Target, (void *)scan_results_affects_fn);
	}
	cache_scan_set((target_t *)Target->Results, Scans);
}

static ml_value_t *scan_target_rebuild(target_scan_t *ScanTarget, int Count, ml_value_t **Args) {
	if (ScanTarget->LastUpdated != CurrentVersion) {
		target_t *Target = (target_t *)Args[0];
		//printf("\n\n\nscan_target_rebuild(%s, %s)\n", ScanTarget->Id, Target->Id);
		Target->Build = 0;
		ml_value_t *Result = ml_inline(ScanTarget->Build, 1, ScanTarget);
		if (Result->Type == MLErrorT) {
			fprintf(stderr, "\e[31mError: %s: %s\n\e[0m", Target->Id, ml_error_message(Result));
			const char *Source;
			int Line;
			for (int I = 0; ml_error_trace(Result, I, &Source, &Line); ++I) fprintf(stderr, "\e[31m\t%s:%d\n\e[0m", Source, Line);
			exit(1);
		}
		ScanTarget->LastUpdated = CurrentVersion;
		if (Target->Build) {
			CurrentContext = Target->BuildContext;
			ml_value_t *Result = ml_inline(Target->Build, 1, Target);
			if (Result->Type == MLErrorT) {
				fprintf(stderr, "\e[31mError: %s: %s\n\e[0m", Target->Id, ml_error_message(Result));
				const char *Source;
				int Line;
				for (int I = 0; ml_error_trace(Result, I, &Source, &Line); ++I) fprintf(stderr, "\e[31m\t%s:%d\n\e[0m", Source, Line);
				exit(1);
			}
			if (Target->Build->Type == MLClosureT) {
				ml_closure_hash(Target->Build, Target->BuildHash);
			} else {
				memset(Target->BuildHash, -1, SHA256_BLOCK_SIZE);
			}
			return Result;
		}
	}
	return MLNil;
}

static int scan_depends_update_fn(target_t *Depend, scan_results_t *Target) {
	//if (!strcmp(Depend->Id, "file:web/tools/rabs/guide/chapter3.xhtml")) asm("int3");
	if (Depend == (target_t *)Target->Scan) return 0;
	//printf("scan_depends_update_fn(%s, %s)\n", Depend->Id, Target->Id);
	if (!Depend->Build && !Depend->BuildChecked) {
		BYTE BuildHash[SHA256_BLOCK_SIZE];
		cache_build_hash_get(Depend, BuildHash);
		for (int I = 0; I < SHA256_BLOCK_SIZE; ++I) if (BuildHash[I]) {
			Depend->Build = Target->Scan->Rebuild;
			Depend->BuildContext = CurrentContext;
			break;
		}
		Depend->BuildChecked = 1;
		//targetset_foreach(Depend->Depends, Target, (void *)scan_depends_update_fn);
		targetset_t *Depends = cache_depends_get(Depend);
		if (Depends) targetset_foreach(Depends, Target, (void *)scan_depends_update_fn);
	}
	return 0;
}

static scan_results_t *scan_results_new(target_t *ParentTarget, const char *Name, target_t **Slot, int Recursive) {
	const char *Id = concat(Recursive ? "results!:" : "results:", ParentTarget->Id, "::", Name, NULL);
	if (!Slot) Slot = targetcache_lookup(Id);
	scan_results_t *Target = (scan_results_t *)Slot[0];
	if (!Target) {
		Target = target_new(scan_results_t, ScanResultsT, Id, Slot);
		const char *ScanId = concat("scan:", ParentTarget->Id, "::", Name, NULL);
		target_scan_t *ScanTarget;
		target_t **ScanSlot = targetcache_lookup(ScanId);
		if (!ScanSlot[0]) {
			ScanTarget = Target->Scan = target_new(target_scan_t, ScanTargetT, ScanId, ScanSlot);
		} else {
			ScanTarget = Target->Scan = (target_scan_t *)ScanSlot[0];
		}
		targetset_insert(ScanTarget->Depends, ParentTarget);
		ScanTarget->Name = concat(Name, NULL);
		ScanTarget->Source = ParentTarget;
		ScanTarget->Results = Target;
		ScanTarget->Rebuild = ml_function(ScanTarget, (void *)scan_target_rebuild);
		ScanTarget->Recursive = Recursive;
		targetset_insert(Target->Depends, (target_t *)ScanTarget);
	}
	targetset_t *Scans = cache_scan_get((target_t *)Target);
	if (Scans) targetset_foreach(Scans, Target, (void *)scan_depends_update_fn);
	return Target;
}

ml_value_t *target_scan_new(void *Data, int Count, ml_value_t **Args) {
	return (ml_value_t *)scan_results_new((target_t *)Args[0], ml_string_value(Args[1]), 0, (Count > 2) && Args[2] != MLNil);
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

static time_t target_symb_hash(target_symb_t *Target, time_t PreviousTime, BYTE PreviousHash[SHA256_BLOCK_SIZE]) {
	context_t *Context = context_find(Target->Context);
	ml_value_t *Value = context_symb_get(Context, Target->Name) ?: MLNil;
	target_value_hash(Value, Target->Hash);
	return 0;
}

target_t *target_symb_new(const char *Name) {
	const char *Id = concat("symb:", CurrentContext->Name, "/", Name, NULL);
	target_t **Slot = targetcache_lookup(Id);
	if (!Slot[0]) {
		target_symb_t *Target = target_new(target_symb_t, SymbTargetT, Id, Slot);
		Target->Context = CurrentContext->Name;
		Target->Name = Name;
	}
	return Slot[0];
}

static int list_update_hash(ml_value_t *Value, SHA256_CTX *Ctx) {
	BYTE ChildHash[SHA256_BLOCK_SIZE];
	target_value_hash(Value, ChildHash);
	sha256_update(Ctx, ChildHash, SHA256_BLOCK_SIZE);
	return 0;
}

static int tree_update_hash(ml_value_t *Key, ml_value_t *Value, SHA256_CTX *Ctx) {
	BYTE ChildHash[SHA256_BLOCK_SIZE];
	target_value_hash(Key, ChildHash);
	sha256_update(Ctx, ChildHash, SHA256_BLOCK_SIZE);
	target_value_hash(Value, ChildHash);
	sha256_update(Ctx, ChildHash, SHA256_BLOCK_SIZE);
	return 0;
}

void target_value_hash(ml_value_t *Value, BYTE Hash[SHA256_BLOCK_SIZE]) {
	if (Value->Type == MLNilT) {
		memset(Hash, -1, SHA256_BLOCK_SIZE);
	} else if (Value->Type == MLIntegerT) {
		memset(Hash, 0, SHA256_BLOCK_SIZE);
		*(long *)Hash = ml_integer_value(Value);
		Hash[SHA256_BLOCK_SIZE - 1] = 1;
	} else if (Value->Type == MLRealT) {
		memset(Hash, 0, SHA256_BLOCK_SIZE);
		*(double *)Hash = ml_real_value(Value);
		Hash[SHA256_BLOCK_SIZE - 1] = 1;
	} else if (Value->Type == MLStringT) {
		const char *String = ml_string_value(Value);
		size_t Len = ml_string_length(Value);
		SHA256_CTX Ctx[1];
		sha256_init(Ctx);
		sha256_update(Ctx, String, Len);
		sha256_final(Ctx, Hash);
	} else if (Value->Type == MLListT) {
		SHA256_CTX Ctx[1];
		sha256_init(Ctx);
		ml_list_foreach(Value, Ctx, (void *)list_update_hash);
		sha256_final(Ctx, Hash);
	} else if (Value->Type == MLTreeT) {
		SHA256_CTX Ctx[1];
		sha256_init(Ctx);
		ml_tree_foreach(Value, Ctx, (void *)tree_update_hash);
		sha256_final(Ctx, Hash);
	} else if (Value->Type == MLClosureT) {
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
	} else if (Value->Type == ExprTargetT) {
		target_t *Target = (target_t *)Value;
		SHA256_CTX Ctx[1];
		sha256_init(Ctx);
		sha256_update(Ctx, Target->Id, strlen(Target->Id));
		sha256_final(Ctx, Hash);
	}
}

static time_t target_hash(target_t *Target, time_t PreviousTime, BYTE PreviousHash[SHA256_BLOCK_SIZE]) {
	if (Target->Type == FileTargetT) return target_file_hash((target_file_t *)Target, PreviousTime, PreviousHash);
	if (Target->Type == MetaTargetT) return target_meta_hash((target_meta_t *)Target, PreviousTime, PreviousHash);
	if (Target->Type == ScanTargetT) return target_scan_hash((target_scan_t *)Target, PreviousTime, PreviousHash);
	if (Target->Type == ScanResultsT) return scan_results_hash((scan_results_t *)Target, PreviousTime, PreviousHash);
	if (Target->Type == SymbTargetT) return target_symb_hash((target_symb_t *)Target, PreviousTime, PreviousHash);
	if (Target->Type == ExprTargetT) return target_expr_hash((target_expr_t *)Target, PreviousTime, PreviousHash);
	return 0;
}

static void target_build(target_t *Target) {
	CurrentDirectory = Target->BuildContext->FullPath;
	if (Target->Type == FileTargetT) return target_default_build(Target);
	if (Target->Type == MetaTargetT) return target_default_build(Target);
	if (Target->Type == ScanTargetT) return target_scan_build((target_scan_t *)Target);
	if (Target->Type == ExprTargetT) return target_expr_build((target_expr_t *)Target);
	//if (Target->Type == ScanResultsT) return scan_results_build((scan_results_t *)Target);
	//if (Target->Type == SymbTargetT) return target_symb_build((target_symb_t *)Target);
	fprintf(stderr, "\e[31mError: not expecting to build %s\e[0m\n", Target->Type->Name);
	exit(1);
}

static int target_missing(target_t *Target, int LastChecked) {
	if (Target->Type == FileTargetT) return target_file_missing((target_file_t *)Target);
	if (Target->Type == ScanTargetT) return target_scan_missing((target_scan_t *)Target, LastChecked);
	return 0;
}

target_t *target_find(const char *Id) {
	target_t **Slot = targetcache_lookup(Id);
	if (Slot[0]) return Slot[0];
	if (!memcmp(Id, "file", 4)) {
		//return target_file_check(Id + 5, Id[5] == '/');
		target_file_t *Target = target_new(target_file_t, FileTargetT, Id, Slot);
		Target->Absolute = Id[5] == '/';
		Target->Path = Id + 5;
		Target->BuildContext = CurrentContext;
		return (target_t *)Target;
	}
	if (!memcmp(Id, "symb", 4)) {
		target_symb_t *Target = target_new(target_symb_t, SymbTargetT, Id, Slot);
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
	if (!memcmp(Id, "expr", 4)) {
		target_expr_t *Target = target_new(target_expr_t, ExprTargetT, Id, Slot);
		return (target_t *)Target;
	}
	if (!memcmp(Id, "results!", 8)) {
		const char *Name;
		for (Name = Id + strlen(Id) - 1; --Name > Id + 9;) {
			if (Name[0] == ':' && Name[1] == ':') break;
		}
		size_t ParentIdLength = Name - Id - 9;
		char *ParentId = snew(ParentIdLength + 1);
		memcpy(ParentId, Id + 9, ParentIdLength);
		ParentId[ParentIdLength] = 0;
		target_t *ParentTarget = target_find(ParentId);
		Name += 2;
		return (target_t *)scan_results_new(ParentTarget, Name, Slot, 1);
	}
	if (!memcmp(Id, "results", 7)) {
		const char *Name;
		for (Name = Id + strlen(Id) - 1; --Name > Id + 8;) {
			if (Name[0] == ':' && Name[1] == ':') break;
		}
		size_t ParentIdLength = Name - Id - 8;
		char *ParentId = snew(ParentIdLength + 1);
		memcpy(ParentId, Id + 8, ParentIdLength);
		ParentId[ParentIdLength] = 0;
		target_t *ParentTarget = target_find(ParentId);
		Name += 2;
		return (target_t *)scan_results_new(ParentTarget, Name, Slot, 0);
	}
	return 0;
}

target_t *target_get(const char *Id) {
	return *targetcache_lookup(Id);
}

int target_print_fn(const char *TargetId, target_t *Target, void *Data) {
	printf("%s\n", Target->Id);
	return 0;
}

void target_list() {
	//stringmap_foreach(TargetCache, 0, (void *)target_print_fn);
}

typedef struct build_thread_t build_thread_t;

struct build_thread_t {
	build_thread_t *Next;
	pthread_t Handle;
};

static build_thread_t *BuildThreads = 0;
int RunningThreads = 0, LastThread = 0;

static void *target_thread_fn(void *Arg) {
	const char *Path = get_current_dir_name();
	char *Path2 = GC_malloc_atomic_uncollectable(strlen(Path) + 1);
	CurrentDirectory = strcpy(Path2, Path);
	int Index = (int)(ptrdiff_t)Arg;
	printf("Starting build thread #%d\n", (int)Index);
	pthread_mutex_lock(GlobalLock);
	++RunningThreads;
	for (;;) {
		while (!BuildQueue) {
			//printf("[%d]: No target in build queue, %d threads running\n", Index, RunningThreads);
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
		if (SpareThreads) {
			--RunningThreads;
			--SpareThreads;
			pthread_cond_signal(TargetAvailable);
			pthread_mutex_unlock(GlobalLock);
			return 0;
		}
	}
	return 0;
}

static void *active_mode_thread_fn(void *Arg) {
	const char *Path = get_current_dir_name();
	char *Path2 = GC_malloc_atomic_uncollectable(strlen(Path) + 1);
	CurrentDirectory = strcpy(Path2, Path);
	int Index = (int)(ptrdiff_t)Arg;
	printf("Starting build thread #%d\n", (int)Index);
	pthread_mutex_lock(GlobalLock);
	++RunningThreads;
	for (;;) {
		while (!BuildQueue) {
			//printf("[%d]: No target in build queue, %d threads running\n", Index, RunningThreads);
			pthread_cond_wait(TargetAvailable, GlobalLock);
		}
		target_t *Target = BuildQueue;
		BuildQueue = Target->Next;
		Target->Next = 0;
		target_do_build(Index, Target);
		if (SpareThreads) {
			--RunningThreads;
			--SpareThreads;
			pthread_cond_signal(TargetAvailable);
			pthread_mutex_unlock(GlobalLock);
			return 0;
		}
	}
	return 0;
}

void target_threads_start(int NumThreads) {
	RunningThreads = 1;
	pthread_mutex_init(GlobalLock, NULL);
	pthread_mutex_lock(GlobalLock);
	pthread_cond_init(TargetAvailable, NULL);
	for (LastThread = 0; LastThread < NumThreads; ++LastThread) {
		build_thread_t *BuildThread = new(build_thread_t);
		GC_pthread_create(&BuildThread->Handle, 0, target_thread_fn, (void *)(ptrdiff_t)LastThread);
		BuildThread->Next = BuildThreads;
		BuildThreads = BuildThread;
	}
}

static void target_wait(target_t *Target) {
	if (Target == CurrentTarget) return;
	if (!Target->Build) return;
	if (Target->LastUpdated == STATE_UNCHECKED) target_update((target_t *)Target);
	if (Target->LastUpdated == STATE_QUEUED) {
		//printf("target_wait(%s)\n", Target->Id);
		build_thread_t *BuildThread = new(build_thread_t);
		GC_pthread_create(&BuildThread->Handle, 0, target_thread_fn, (void *)(ptrdiff_t)(LastThread++));
		build_thread_t **Slot = &BuildThreads;
		while (Slot[0]) Slot = &Slot[0]->Next;
		Slot[0] = BuildThread;
		pthread_cond_broadcast(TargetAvailable);
		while (Target->LastUpdated == STATE_QUEUED) pthread_cond_wait(TargetAvailable, GlobalLock);
		++SpareThreads;
		//printf("target_waited(%s)\n", Target->Id);
	}
}

static int print_because_target(target_t *Target, void *Data) {
	printf("\t%s\n", Target->Id);
	return 0;
}

static int print_unbuilt_target(const char *Id, target_t *Target, void *Data) {
	if (Target->LastUpdated == STATE_QUEUED) {
		printf("Not checked: %s: waiting on %d\n", Id, Target->WaitCount);
		targetset_foreach(Target->Depends, 0, (void *)print_because_target);
	}
	return 0;
}

void target_interactive_start(int NumThreads) {
	RunningThreads = 0;
	pthread_mutex_init(GlobalLock, NULL);
	pthread_mutex_lock(GlobalLock);
	pthread_cond_init(TargetAvailable, NULL);
	for (LastThread = 0; LastThread < NumThreads; ++LastThread) {
		build_thread_t *BuildThread = new(build_thread_t);
		GC_pthread_create(&BuildThread->Handle, 0, active_mode_thread_fn, (void *)(ptrdiff_t)LastThread);
		BuildThread->Next = BuildThreads;
		BuildThreads = BuildThread;
	}
	pthread_cond_broadcast(TargetAvailable);
	pthread_mutex_unlock(GlobalLock);
}

void target_threads_wait(int NumThreads) {
	--RunningThreads;
	pthread_cond_broadcast(TargetAvailable);
	pthread_mutex_unlock(GlobalLock);
	while (BuildThreads) {
		GC_pthread_join(BuildThreads->Handle, 0);
		BuildThreads = BuildThreads->Next;
	}
	//stringmap_foreach(TargetCache, 0, (void *)print_unbuilt_target);
}

#define target_file_methods_is(TYPE) \
	ml_method_by_name("is_" #TYPE, 0, target_file_is_ ## TYPE, FileTargetT, NULL);

void target_init() {
	targetcache_init();
	TargetT = ml_class(MLAnyT, "target");
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
	StringMethod = ml_method("string");
	AppendMethod = ml_method("append");
	ml_method_by_name("append", 0, target_file_stringify, MLStringBufferT, FileTargetT, NULL);
	ml_method_by_name("append", 0, target_expr_stringify, MLStringBufferT, ExprTargetT, NULL);
	ml_method_by_name("[]", 0, target_depend, TargetT, MLAnyT, NULL);
	ml_method_by_name("scan", 0, target_scan_new, TargetT, NULL);
	ml_method_by_name("[]", 0, scan_results_depend, ScanResultsT, MLAnyT, NULL);
	ml_method_by_name("string", 0, target_file_to_string, FileTargetT, NULL);
	ml_method_by_name("string", 0, target_expr_to_string, ExprTargetT, NULL);
	ml_method_by_name("=>", 0, target_set_build, TargetT, MLAnyT, NULL);
	ml_method_by_name("refresh", 0, target_recheck_value, TargetT, NULL);
	ml_method_by_name("=>", 0, scan_results_set_build, ScanResultsT, MLAnyT, NULL);
	ml_method_by_name("source", 0, target_scan_source, ScanTargetT, NULL);
	ml_method_by_name("/", 0, target_file_div, FileTargetT, MLStringT, NULL);
	ml_method_by_name("%", 0, target_file_mod, FileTargetT, MLStringT, NULL);
	ml_method_by_name("dir", 0, target_file_dir, FileTargetT, NULL);
	ml_method_by_name("dirname", 0, target_file_dirname, FileTargetT, NULL);
	ml_method_by_name("basename", 0, target_file_basename, FileTargetT, NULL);
	ml_method_by_name("extension", 0, target_file_extension, FileTargetT, NULL);
	ml_method_by_name("-", 0, target_file_relative, FileTargetT, FileTargetT, NULL);
	ml_method_by_name("exists", 0, target_file_exists, FileTargetT, NULL);
	ml_method_by_name("ls", 0, target_file_ls, FileTargetT, NULL);
	ml_method_by_name("copy", 0, target_file_copy, FileTargetT, FileTargetT, NULL);
	ml_method_by_name("open", 0, target_file_open, FileTargetT, MLStringT, NULL);
	ml_method_by_name("mkdir", 0, target_file_mkdir, FileTargetT, NULL);
	ml_method_by_name("rmdir", 0, target_file_rmdir, FileTargetT, NULL);
	ml_method_by_name("chdir", 0, target_file_chdir, FileTargetT, NULL);
	target_file_methods_is(dir);
	target_file_methods_is(chr);
	target_file_methods_is(blk);
	target_file_methods_is(reg);
	target_file_methods_is(fifo);
	target_file_methods_is(lnk);
	target_file_methods_is(sock);
}
