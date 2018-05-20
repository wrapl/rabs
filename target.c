#include "target.h"
#include "rabs.h"
#include "util.h"
#include "context.h"
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
static stringmap_t TargetCache[1] = {STRINGMAP_INIT};
static int QueuedTargets = 0, BuiltTargets = 0, NumTargets = 0;

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

int depends_hash_fn(const char *Id, target_t *Depend, BYTE Hash[SHA256_BLOCK_SIZE]) {
	for (int I = 0; I < SHA256_BLOCK_SIZE; ++I) Hash[I] ^= Depend->Hash[I];
	//for (int I = 0; I < SHA256_BLOCK_SIZE; ++I) printf(" %x", Depend->Hash[I]);
	return 0;
}

void target_depends_add(target_t *Target, target_t *Depend) {
	if (Target != Depend) stringmap_insert(Target->Depends, Depend->Id, Depend);
}

static int depends_print_fn(const char *DependId, target_t *Depend, int *DependsLastUpdated) {
	printf("\t\e[35m%s[%d]\e[0m\n", Depend->Id, Depend->LastUpdated);
	return 0;
}

static void target_queue_build(target_t *Target) {
	Target->Next = BuildQueue;
	BuildQueue = Target;
}

static int depends_updated_fn(const char *AffectId, target_t *Affect, target_t *Target) {
	if (Target->LastUpdated > Affect->DependsLastUpdated) Affect->DependsLastUpdated = Target->LastUpdated;
	if (--Affect->WaitCount == 0) target_queue_build(Affect);
	return 0;
}

static void target_do_build(int ThreadIndex, target_t *Target) {
	//printf("\e[32m[%d] Building %s (%d targets, %d bytes)\e[0m\n", ThreadIndex, Target->Id, NumTargets, GC_get_heap_size());
	BYTE Previous[SHA256_BLOCK_SIZE];
	int LastUpdated, LastChecked;
	time_t FileTime = 0;
	cache_hash_get(Target->Id, &LastUpdated, &LastChecked, &FileTime, Previous);
	if (Target->Build) {
		CurrentContext = Target->BuildContext;
		if ((Target->DependsLastUpdated > LastChecked) || target_missing(Target, LastChecked)) {
			CurrentTarget = Target;
			target_build(Target);
			cache_depends_set(Target->Id, Target->Depends);
			cache_build_hash_set(Target->Id, Target->BuildHash);
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
	++BuiltTargets;
	printf("\e[35m%d / %d\e[0m #%d Built \e[32m%s\e[0m at version %d\n", BuiltTargets, QueuedTargets, ThreadIndex, Target->Id, Target->LastUpdated);
	stringmap_foreach(Target->Affects, Target, (void *)depends_updated_fn);
	memset(Target->Depends, 0, sizeof(Target->Depends));
	memset(Target->Affects, 0, sizeof(Target->Affects));
	pthread_cond_broadcast(TargetAvailable);
}

int depends_update_fn(const char *DependId, target_t *Depend, target_t *Target) {
	if (Depend->LastUpdated == STATE_CHECKING) {
		printf("\e[31mError: build cycle with %s -> %s\e[0m\n", Target->Id, Depend->Id);
		exit(1);
	}
	if (Depend->LastUpdated == STATE_UNCHECKED) target_update(Depend);
	if (Depend->LastUpdated == STATE_QUEUED) {
		if (!stringmap_insert(Depend->Affects, Target->Id, Target)) ++Target->WaitCount;
	} else {
		if (Depend->LastUpdated > Target->DependsLastUpdated) Target->DependsLastUpdated = Depend->LastUpdated;
	}
	return 0;
}

void target_update(target_t *Target) {
	if (Target->LastUpdated == STATE_CHECKING) {
		printf("\e[31mError: build cycle with %s\e[0m\n", Target->Id);
		exit(1);
	}
	if (Target->LastUpdated == STATE_UNCHECKED) {
		Target->LastUpdated = STATE_CHECKING;
		++QueuedTargets;
		//printf("Added new target to queue: %s\n", Target->Id);
		stringmap_foreach(Target->Depends, Target, (void *)depends_update_fn);
		if (Target->Build && Target->Build->Type == MLClosureT) {
			ml_closure_hash(Target->Build, Target->BuildHash);
			BYTE Previous[SHA256_BLOCK_SIZE];
			cache_build_hash_get(Target->Id, Previous);
			if (memcmp(Previous, Target->BuildHash, SHA256_BLOCK_SIZE)) {
				Target->DependsLastUpdated = CurrentVersion;
			}
		}
		if (Target->DependsLastUpdated < CurrentVersion) {
			stringmap_t *PreviousDetectedDepends = cache_depends_get(Target->Id);
			if (PreviousDetectedDepends) {
				stringmap_foreach(PreviousDetectedDepends, Target, (void *)depends_update_fn);
			}
		}
		Target->LastUpdated = STATE_QUEUED;
		//printf("\e[32m[%d/%d] \e[33mtarget_update(%s) -> waiting on %d\e[0m\n", BuiltTargets, TargetCache->Size, Target->Id, Target->WaitCount);
		if (Target->WaitCount == 0) target_queue_build(Target);
	}
}

int depends_query_fn(const char *Id, target_t *Depends, int *DependsLastUpdated) {
	target_query(Depends);
	if (Depends->LastUpdated > *DependsLastUpdated) *DependsLastUpdated = Depends->LastUpdated;
	return 0;
}

void target_query(target_t *Target) {
	if (Target->LastUpdated == STATE_CHECKING) {
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
			stringmap_t *PreviousDetectedDepends = cache_depends_get(Target->Id);
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
	}
}

void target_depends_auto(target_t *Depend) {
	if (CurrentTarget && CurrentTarget != Depend && !stringmap_search(CurrentTarget->Depends, Depend->Id)) {
		stringmap_insert(CurrentTarget->Depends, Depend->Id, Depend);
	}
}

static target_t *target_alloc(int Size, ml_type_t *Type, const char *Id) {
	++NumTargets;
	target_t *Target = (target_t *)GC_MALLOC(Size);
	Target->Type = Type;
	Target->Id = Id;
	Target->Build = 0;
	Target->LastUpdated = STATE_UNCHECKED;
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
	if (Target->Absolute) {
		ml_stringbuffer_add(Buffer, Target->Path, strlen(Target->Path));
	} else {
		const char *Path = vfs_resolve(Target->BuildContext->Mounts, concat(RootPath, "/", Target->Path, 0));
		ml_stringbuffer_add(Buffer, Path, strlen(Path));
	}
	return MLSome;
}

static ml_value_t *target_file_to_string(void *Data, int Count, ml_value_t **Args) {
	target_file_t *Target = (target_file_t *)Args[0];
	if (Target->Absolute) {
		return ml_string(Target->Path, -1);
	} else {
		const char *Path = vfs_resolve(Target->BuildContext->Mounts, concat(RootPath, "/", Target->Path, 0));
		return ml_string(Path, -1);
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
	pthread_mutex_lock(GlobalLock);
	return Stat->st_mtime;
}

static void target_default_build(target_t *Target) {
	ml_value_t *Result = ml_inline(Target->Build, 1, Target);
	if (Result->Type == MLErrorT) {
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
	Path = concat(Path, NULL);
	const char *Id = concat("file:", Path, NULL);
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
	ML_CHECK_ARG_COUNT(1);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	const char *Path = ml_string_value(Args[0]);
	target_t *Target;
	if (!Path[0]) {
		Path = vfs_unsolve(CurrentContext->Mounts, CurrentContext->Path + 1);
		Target = target_file_check(Path, 0);
	} else if (Path[0] != '/') {
		Path = concat(CurrentContext->Path, "/", Path, 0) + 1;
		Path = vfs_unsolve(CurrentContext->Mounts, Path);
		Target = target_file_check(Path, 0);
	} else {
		Path = vfs_unsolve(CurrentContext->Mounts, Path);
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

typedef struct target_file_ls_iter_node_t target_file_ls_iter_node_t;

struct target_file_ls_iter_node_t {
	target_file_ls_iter_node_t *Next;
	const char *Path;
};

typedef struct target_file_ls_iter_t {
	const ml_type_t *Type;
	regex_t *Regex;
	DIR *Dir;
	target_file_ls_iter_node_t *Node;
	const char *Path;
	target_t *File;
} target_file_ls_iter_t;

static ml_value_t *target_file_ls_iter_deref(ml_value_t *Ref) {
	target_file_ls_iter_t *Iter = (target_file_ls_iter_t *)Ref;
	return (ml_value_t *)Iter->File;
}

static ml_value_t *target_file_ls_iter_next(ml_value_t *Ref) {
	target_file_ls_iter_t *Iter = (target_file_ls_iter_t *)Ref;
	while (Iter->Dir) {
		struct dirent *Entry = readdir(Iter->Dir);
		while (Entry) {
			if (
				strcmp(Entry->d_name, ".") &&
				strcmp(Entry->d_name, "..") &&
				!(Iter->Regex && regexec(Iter->Regex, Entry->d_name, 0, 0, 0))
			) {
				const char *Path = concat(Iter->Path, "/", Entry->d_name, NULL);
				const char *Relative = match_prefix(Path, RootPath);
				if (Relative) {
					Iter->File = target_file_check(Relative + 1, 0);
				} else {
					Iter->File = target_file_check(Path, 1);
				}
				return Ref;
			}
			Entry = readdir(Iter->Dir);
		}
		closedir(Iter->Dir);
		Iter->Dir = 0;
		target_file_ls_iter_node_t *Node = Iter->Node;
		if (Node) {
			Iter->Node = Node->Next;
			Iter->Path = Node->Path;
			Iter->Dir = opendir(Node->Path);
			if (!Iter->Dir) return ml_error("DirError", "failed to open directory %s", Node->Path);
		}
	}
	return MLNil;
}

static ml_value_t *target_file_ls_iter_key(ml_value_t *Ref) {
	target_file_ls_iter_t *Iter = (target_file_ls_iter_t *)Ref;
	return MLNil;
}

static void target_file_ls_iter_finalize(target_file_ls_iter_t *Iter, void *Data) {
	if (Iter->Dir) {
		closedir(Iter->Dir);
		Iter->Dir = NULL;
	}
}

ml_type_t TargetFileLsIter[1] = {{
	MLAnyT, "file-ls-iterator",
	ml_default_hash,
	ml_default_call,
	target_file_ls_iter_deref,
	ml_default_assign,
	target_file_ls_iter_next,
	target_file_ls_iter_key
}};

static int target_file_ls_resolve_fn(target_file_ls_iter_t *Iter, const char *Path) {
	target_file_ls_iter_node_t *Node = new(target_file_ls_iter_node_t);
	Node->Next = Iter->Node;
	Node->Path = Path;
	Iter->Node = Node;
	return 0;
}

ml_value_t *target_file_ls(void *Data, int Count, ml_value_t **Args) {
	target_file_ls_iter_t *Iter = new(target_file_ls_iter_t);
	Iter->Type = TargetFileLsIter;
	target_file_t *Target = (target_file_t *)Args[0];
	if (Target->Absolute) {
		target_file_ls_iter_node_t *Node = Iter->Node = new(target_file_ls_iter_node_t);
		Node->Path = Target->Path;
	} else {
		vfs_resolve_foreach(CurrentContext->Mounts, concat(RootPath, "/", Target->Path, 0), Iter, (void *)target_file_ls_resolve_fn);
	}
	if (Count > 1) {
		const char *Pattern = ml_string_value(Args[1]);
		Iter->Regex = new(regex_t);
		int Error = regcomp(Iter->Regex, Pattern, REG_NOSUB | REG_EXTENDED);
		if (Error) {
			size_t Length = regerror(Error, Iter->Regex, NULL, 0);
			char *Message = snew(Length + 1);
			regerror(Error, Iter->Regex, Message, Length);
			regfree(Iter->Regex);
			return ml_error("RegexError", "%s", Message);
		}
	}
	target_file_ls_iter_node_t *Node = Iter->Node;
	if (Node) {
		Iter->Node = Node->Next;
		Iter->Path = Node->Path;
		Iter->Dir = opendir(Node->Path);
		if (!Iter->Dir) return ml_error("DirError", "failed to open directory %s", Node->Path);
		GC_register_finalizer(Iter, (void *)target_file_ls_iter_finalize, 0, 0, 0);
		return target_file_ls_iter_next((ml_value_t *)Iter);
	} else {
		return MLNil;
	}
}

ml_value_t *target_file_basename(void *Data, int Count, ml_value_t **Args) {
	target_file_t *FileTarget = (target_file_t *)Args[0];
	const char *Path = FileTarget->Path;
	const char *Last = Path;
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
				if (rmdir_p(Buffer, End2)) return 1;
			}
			Entry = readdir(Dir);
		}
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
	ml_stringbuffer_t *Buffer = (ml_stringbuffer_t *)Args[0];
	target_expr_t *Target = (target_expr_t *)Args[1];
	target_wait((target_t *)Target);
	target_depends_auto((target_t *)Target);
	ml_value_t *Value = cache_expr_get(Target->Id);
	return ml_inline(AppendMethod, 2, Buffer, Value);
}

static ml_value_t *target_expr_to_string(void *Data, int Count, ml_value_t **Args) {
	target_expr_t *Target = (target_expr_t *)Args[0];
	target_wait((target_t *)Target);
	target_depends_auto((target_t *)Target);
	ml_value_t *Value = cache_expr_get(Target->Id);
	return ml_inline(StringMethod, 1, Value);
}

static time_t target_expr_hash(target_expr_t *Target, time_t PreviousTime, BYTE PreviousHash[SHA256_BLOCK_SIZE]) {
	ml_value_t *Value = cache_expr_get(Target->Id);
	if (Value) target_value_hash(Value, Target->Hash);
	return 0;
}

static void target_expr_build(target_expr_t *Target) {
	ml_value_t *Result = ml_inline(Target->Build, 1, Target);
	if (Result->Type == MLErrorT) {
		fprintf(stderr, "\e[31mError: %s: %s\n\e[0m", Target->Id, ml_error_message(Result));
		const char *Source;
		int Line;
		for (int I = 0; ml_error_trace(Result, I, &Source, &Line); ++I) printf("\e[31m\t%s:%d\n\e[0m", Source, Line);
		exit(1);
	}
	cache_expr_set(Target->Id, Result);
}

ml_value_t *target_expr_new(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	const char *Name = ml_string_value(Args[0]);
	const char *Id = concat("expr:", CurrentContext->Path, "::", Name, NULL);
	target_expr_t *Target = (target_expr_t *)stringmap_search(TargetCache, Id);
	if (!Target) {
		Target = target_new(target_expr_t, ExprTargetT, Id);
	}
	return (ml_value_t *)Target;
}

static int target_depends_single(ml_value_t *Arg, target_t *Target) {
	if (Arg->Type == MLListT) {
		return ml_list_foreach(Arg, Target, (void *)target_depends_single);
	} else if (Arg->Type == MLStringT) {
		target_t *Depend = target_symb_new(ml_string_value(Arg));
		stringmap_insert(Target->Depends, Depend->Id, Depend);
	} else if (ml_is(Arg, TargetT)) {
		target_t *Depend = (target_t *)Arg;
		stringmap_insert(Target->Depends, Depend->Id, Depend);
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
	//if (Target->Build) return ml_error("ParameterError", "build already defined for %s", Target->Id);
	Target->Build = Args[1];
	Target->BuildContext = CurrentContext;
	Target->LastUpdated = STATE_UNCHECKED;
	return Args[0];
}

struct target_scan_t {
	TARGET_FIELDS
	const char *Name;
	target_t *Source;
	scan_results_t *Results;
	ml_value_t *Scan, *Rebuild;
};

static ml_type_t *ScanTargetT;

struct scan_results_t {
	TARGET_FIELDS
	target_scan_t *Scan;
};

static ml_type_t *ScanResultsT;

static time_t target_scan_hash(target_scan_t *Target, time_t PreviousTime, BYTE PreviousHash[SHA256_BLOCK_SIZE]) {
	stringmap_t *Scans = cache_scan_get(Target->Results->Id);
	if (Scans) stringmap_foreach(Scans, Target->Results, (void *)depends_update_fn);
	memset(Target->Hash, 0, SHA256_BLOCK_SIZE);
	return 0;
}

static int target_scan_missing(target_scan_t *Target, int LastChecked) {
	if (!LastChecked) return 1;
	BYTE Hash[SHA256_BLOCK_SIZE];
	int LastUpdated, ResultsLastChecked;
	time_t Time = 0;
	cache_hash_get(Target->Results->Id, &LastUpdated, &ResultsLastChecked, &Time, Hash);
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
	Target->Build = Args[1];
	Target->BuildContext = CurrentContext;
	Target->LastUpdated = STATE_UNCHECKED;
	return Args[0];
}

static time_t scan_results_hash(scan_results_t *Target, time_t PreviousTime, BYTE PreviousHash[SHA256_BLOCK_SIZE]) {
	stringmap_t *Scans = cache_scan_get(Target->Id);
	memset(Target->Hash, 0, SHA256_BLOCK_SIZE);
	if (Scans) stringmap_foreach(Scans, Target->Hash, (void *)depends_hash_fn);
	return 0;
}

static int build_scan_target_list(target_t *Depend, stringmap_t *Scans) {
	stringmap_insert(Scans, Depend->Id, Depend);
	return 0;
}

static void target_scan_build(target_scan_t *Target) {
	ml_value_t *Result = ml_inline(Target->Build, 1, Target->Source);
	if (Result->Type == MLErrorT) {
		fprintf(stderr, "\e[31mError: %s: %s\n\e[0m", Target->Id, ml_error_message(Result));
		const char *Source;
		int Line;
		for (int I = 0; ml_error_trace(Result, I, &Source, &Line); ++I) printf("\e[31m\t%s:%d\n\e[0m", Source, Line);
		exit(1);
	}
	stringmap_t Scans[1] = {STRINGMAP_INIT};
	ml_list_foreach(Result, Scans, (void *)build_scan_target_list);
	cache_depends_set(Target->Results->Id, Scans);
	cache_scan_set(Target->Results->Id, Scans);
}

static ml_value_t *scan_target_rebuild(target_scan_t *ScanTarget, int Count, ml_value_t **Args) {
	if (ScanTarget->LastUpdated != CurrentVersion) {
		ml_value_t *Result = ml_inline(ScanTarget->Build, 1, ScanTarget->Source);
		if (Result->Type == MLErrorT) {
			fprintf(stderr, "\e[31mError: %s: %s\n\e[0m", ScanTarget->Id, ml_error_message(Result));
			const char *Source;
			int Line;
			for (int I = 0; ml_error_trace(Result, I, &Source, &Line); ++I) printf("\e[31m\t%s:%d\n\e[0m", Source, Line);
			exit(1);
		}
		stringmap_t Scans[1] = {STRINGMAP_INIT};
		ml_list_foreach(Result, Scans, (void *)build_scan_target_list);
		cache_depends_set(ScanTarget->Results->Id, Scans);
		cache_scan_set(ScanTarget->Results->Id, Scans);
		ScanTarget->LastUpdated = CurrentVersion;
		target_t *Target = (target_t *)Args[0];
		if (Target->Build != ScanTarget->Rebuild) {
			ml_value_t *Result = ml_inline(Target->Build, 1, Target);
			if (Result->Type == MLErrorT) {
				fprintf(stderr, "\e[31mError: %s: %s\n\e[0m", Target->Id, ml_error_message(Result));
				const char *Source;
				int Line;
				for (int I = 0; ml_error_trace(Result, I, &Source, &Line); ++I) printf("\e[31m\t%s:%d\n\e[0m", Source, Line);
				exit(1);
			}
			if (Target->Build->Type == MLClosureT) {
				ml_closure_hash(Target->Build, Target->BuildHash);
			} else {
				memset(Target->BuildHash, -1, SHA256_BLOCK_SIZE);
			}
			cache_build_hash_set(Target->Id, Target->BuildHash);
		}
	}
	return MLNil;
}

static int scan_depends_update_fn(const char *DependId, target_t *Depend, scan_results_t *Target) {
	if (!Depend->Build) {
		BYTE BuildHash[SHA256_BLOCK_SIZE];
		cache_build_hash_get(Depend->Id, BuildHash);
		for (int I = 0; I < SHA256_BLOCK_SIZE; ++I) if (BuildHash[I]) {
			Depend->Build = Target->Scan->Rebuild;
			break;
		}
		stringmap_t *Depends = cache_depends_get(Depend->Id);
		if (Depends) stringmap_foreach(Depends, Target, (void *)scan_depends_update_fn);
	}
	return 0;
}

ml_value_t *target_scan_new(void *Data, int Count, ml_value_t **Args) {
	target_t *ParentTarget = (target_t *)Args[0];
	const char *Name = ml_string_value(Args[1]);
	const char *Id = concat("results:", ParentTarget->Id, "::", Name, NULL);
	scan_results_t *Target = (scan_results_t *)stringmap_search(TargetCache, Id);
	if (!Target) {
		Target = target_new(scan_results_t, ScanResultsT, Id);
		const char *ScanId = concat("scan:", ParentTarget->Id, "::", Name, NULL);
		target_scan_t *ScanTarget = Target->Scan = target_new(target_scan_t, ScanTargetT, ScanId);
		stringmap_insert(ScanTarget->Depends, ParentTarget->Id, ParentTarget);
		ScanTarget->Name = Name;
		ScanTarget->Source = ParentTarget;
		ScanTarget->Results = Target;
		ScanTarget->Rebuild = ml_function(ScanTarget, (void *)scan_target_rebuild);
		stringmap_insert(Target->Depends, ScanTarget->Id, ScanTarget);
	}
	stringmap_t *Scans = cache_scan_get(Target->Id);
	if (Scans) stringmap_foreach(Scans, Target, (void *)scan_depends_update_fn);
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

static time_t target_symb_hash(target_symb_t *Target, time_t PreviousTime, BYTE PreviousHash[SHA256_BLOCK_SIZE]) {
	context_t *Context = context_find(Target->Context);
	ml_value_t *Value = context_symb_get(Context, Target->Name) ?: MLNil;
	target_value_hash(Value, Target->Hash);
	return 0;
}

target_t *target_symb_new(const char *Name) {
	const char *Id = concat("symb:", CurrentContext->Name, "/", Name, NULL);
	target_symb_t *Target = (target_symb_t *)stringmap_search(TargetCache, Id);
	if (!Target) {
		Target = target_new(target_symb_t, SymbTargetT, Id);
		Target->Context = CurrentContext->Name;
		Target->Name = Name;
	}
	return (target_t *)Target;
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
	if (Target->Type == FileTargetT) return target_default_build(Target);
	if (Target->Type == MetaTargetT) return target_default_build(Target);
	if (Target->Type == ScanTargetT) return target_scan_build((target_scan_t *)Target);
	if (Target->Type == ExprTargetT) return target_expr_build((target_expr_t *)Target);
	//if (Target->Type == ScanResultsT) return scan_results_build((scan_results_t *)Target);
	//if (Target->Type == SymbTargetT) return target_symb_build((target_symb_t *)Target);
	printf("\e[31mError: not expecting to build %s\e[0m\n", Target->Type->Name);
	exit(1);
}

static int target_missing(target_t *Target, int LastChecked) {
	if (Target->Type == FileTargetT) return target_file_missing((target_file_t *)Target);
	if (Target->Type == ScanTargetT) return target_scan_missing((target_scan_t *)Target, LastChecked);
	return 0;
}

target_t *target_find(const char *Id) {
	target_t *Target = (target_t *)stringmap_search(TargetCache, Id);
	if (Target) return Target;
	if (!memcmp(Id, "file", 4)) return target_file_check(Id + 5, Id[5] == '/');
	if (!memcmp(Id, "symb", 4)) {
		Id = concat(Id, NULL);
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
	if (!memcmp(Id, "expr", 4)) {
		target_expr_t *Target = target_new(target_expr_t, ExprTargetT, Id);
		return (target_t *)Target;
	}
	return 0;
}

target_t *target_get(const char *Id) {
	return (target_t *)stringmap_search(TargetCache, Id);
}

int target_print_fn(const char *TargetId, target_t *Target, void *Data) {
	printf("%s\n", Target->Id);
	return 0;
}

void target_list() {
	stringmap_foreach(TargetCache, 0, (void *)target_print_fn);
}

typedef struct build_thread_t build_thread_t;

struct build_thread_t {
	build_thread_t *Next;
	pthread_t Handle;
};

static build_thread_t *BuildThreads = 0;
int RunningThreads = 1, LastThread = 0;

static void *target_thread_fn(void *Arg) {
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

void target_threads_start(int NumThreads) {
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

void target_threads_wait(int NumThreads) {
	--RunningThreads;
	pthread_cond_broadcast(TargetAvailable);
	pthread_mutex_unlock(GlobalLock);
	while (BuildThreads) {
		GC_pthread_join(BuildThreads->Handle, 0);
		BuildThreads = BuildThreads->Next;
	}
}

#define target_file_methods_is(TYPE) \
	ml_method_by_name("is_" #TYPE, 0, target_file_is_ ## TYPE, FileTargetT, NULL);

void target_init() {
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
	ml_method_by_name("=>", 0, scan_results_set_build, ScanResultsT, MLAnyT, NULL);
	ml_method_by_name("/", 0, target_file_div, FileTargetT, MLStringT, NULL);
	ml_method_by_name("%", 0, target_file_mod, FileTargetT, MLStringT, NULL);
	ml_method_by_name("dir", 0, target_file_dir, FileTargetT, NULL);
	ml_method_by_name("basename", 0, target_file_basename, FileTargetT, NULL);
	ml_method_by_name("extension", 0, target_file_extension, FileTargetT, NULL);
	ml_method_by_name("exists", 0, target_file_exists, FileTargetT, NULL);
	ml_method_by_name("ls", 0, target_file_ls, FileTargetT, NULL);
	ml_method_by_name("copy", 0, target_file_copy, FileTargetT, FileTargetT, NULL);
	ml_method_by_name("open", 0, target_file_open, FileTargetT, MLStringT, NULL);
	ml_method_by_name("mkdir", 0, target_file_mkdir, FileTargetT, NULL);
	ml_method_by_name("rmdir", 0, target_file_rmdir, FileTargetT, NULL);
	target_file_methods_is(dir);
	target_file_methods_is(chr);
	target_file_methods_is(blk);
	target_file_methods_is(reg);
	target_file_methods_is(fifo);
	target_file_methods_is(lnk);
	target_file_methods_is(sock);
}
