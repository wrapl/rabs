#include "target.h"
#include "rabs.h"
#include "util.h"
#include "context.h"
#include "targetcache.h"
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

#ifdef Linux
#include "targetwatch.h"
#endif

enum {
	STATE_UNCHECKED = 0,
	STATE_CHECKING = -1,
	STATE_QUEUED = -2
};

typedef struct target_file_t target_file_t;
typedef struct target_meta_t target_meta_t;
typedef struct target_expr_t target_expr_t;
typedef struct target_scan_t target_scan_t;
typedef struct target_symb_t target_symb_t;

int StatusUpdates = 0;
int MonitorFiles = 0;
int DebugThreads = 0;
int WatchMode = 0;
FILE *DependencyGraph = 0;

pthread_mutex_t InterpreterLock[1] = {PTHREAD_MUTEX_INITIALIZER};
static pthread_cond_t TargetAvailable[1] = {PTHREAD_COND_INITIALIZER};
static pthread_cond_t TargetUpdated[1] = {PTHREAD_COND_INITIALIZER};

static ml_value_t *SHA256Method;
static ml_value_t *MissingMethod;
extern ml_value_t *StringMethod;
extern ml_value_t *AppendMethod;
extern ml_value_t *ArgifyMethod;
extern ml_value_t *CmdifyMethod;

ml_type_t *TargetT;

__thread target_t *CurrentTarget = 0;
__thread context_t *CurrentContext = 0;
__thread const char *CurrentDirectory = 0;

static int QueuedTargets = 0, BuiltTargets = 0, NumTargets = 0;
static target_t *NextTarget = 0;

static void target_value_hash(ml_value_t *Value, unsigned char Hash[SHA256_BLOCK_SIZE]);
static time_t target_hash(target_t *Target, time_t PreviousTime, unsigned char PreviousHash[SHA256_BLOCK_SIZE], int DependsLastUpdated);
static int target_missing(target_t *Target, int LastChecked);

typedef enum {BUILD_IDLE, BUILD_WAIT, BUILD_EXEC} build_thread_status_t;

typedef struct build_thread_t build_thread_t;

struct build_thread_t {
	build_thread_t *Next;
	target_t *Target;
	pthread_t Handle;
	int Id;
	build_thread_status_t Status;
};

static build_thread_t *BuildThreads = 0;
static __thread build_thread_t *CurrentThread = 0;
static int RunningThreads = 0, LastThread = 0;

void target_depends_auto(target_t *Depend) {
	if (CurrentTarget && CurrentTarget != Depend) {
		targetset_insert(CurrentTarget->BuildDepends, Depend);
		target_queue(Depend, 0);
		target_wait(Depend, CurrentTarget);
	}
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
	const char *Path;
	if (Target->Absolute) {
		Path = Target->Path;
	} else if (Target->Path[0]) {
		Path = vfs_resolve(concat(RootPath, "/", Target->Path, NULL));
	} else {
		Path = RootPath;
	}
	const char *I = Path, *J = Path;
	for (;; ++J) switch (*J) {
	case 0: goto done;
	case ' ':
	case '#':
	case '\"':
	case '\'':
	case '&':
	case '(':
	case ')':
	case '\\':
	case '\t':
	case '\r':
	case '\n':
		if (I < J) ml_stringbuffer_add(Buffer, I, J - I);
		ml_stringbuffer_add(Buffer, "\\", 1);
		I = J;
		break;
	}
done:
	if (I < J) ml_stringbuffer_add(Buffer, I, J - I);
	return MLSome;
}

static ml_value_t *target_file_argify(void *Data, int Count, ml_value_t **Args) {
	target_file_t *Target = (target_file_t *)Args[1];
	if (Target->Absolute) {
		ml_list_append(Args[0], ml_string(Target->Path, strlen(Target->Path)));
	} else if (Target->Path[0]) {
		const char *Path = vfs_resolve(concat(RootPath, "/", Target->Path, NULL));
		ml_list_append(Args[0], ml_string(Path, strlen(Path)));
	} else {
		ml_list_append(Args[0], ml_string(RootPath, strlen(RootPath)));
	}
	return MLSome;
}

static ml_value_t *target_file_cmdify(void *Data, int Count, ml_value_t **Args) {
	ml_stringbuffer_t *Buffer = (ml_stringbuffer_t *)Args[0];
	target_file_t *Target = (target_file_t *)Args[1];
	const char *Path;
	if (Target->Absolute) {
		Path = Target->Path;
	} else if (Target->Path[0]) {
		Path = vfs_resolve(concat(RootPath, "/", Target->Path, NULL));
	} else {
		Path = RootPath;
	}
	const char *I = Path, *J = Path;
	for (;; ++J) switch (*J) {
	case 0: goto done;
	case ' ':
	case '#':
	case '\"':
	case '\'':
	case '&':
	case '(':
	case ')':
	case '\\':
	case '\t':
	case '\r':
	case '\n':
		if (I < J) ml_stringbuffer_add(Buffer, I, J - I);
		ml_stringbuffer_add(Buffer, "\\", 1);
		I = J;
		break;
	}
done:
	if (I < J) ml_stringbuffer_add(Buffer, I, J - I);
	return MLSome;
}

static ml_value_t *target_file_to_string(void *Data, int Count, ml_value_t **Args) {
	target_file_t *Target = (target_file_t *)Args[0];
	if (Target->Absolute) {
		return ml_string(Target->Path, -1);
	} else if (Target->Path[0]) {
		const char *Path = vfs_resolve(concat(RootPath, "/", Target->Path, NULL));
		return ml_string(Path, -1);
	} else {
		return ml_string(RootPath, -1);
	}
}

static time_t target_file_hash(target_file_t *Target, time_t PreviousTime, unsigned char PreviousHash[SHA256_BLOCK_SIZE]) {
	const char *FileName;
	if (Target->Absolute) {
		FileName = Target->Path;
	} else {
		FileName = vfs_resolve(concat(RootPath, "/", Target->Path, NULL));
	}
	pthread_mutex_unlock(InterpreterLock);
	struct stat Stat[1];
	if (stat(FileName, Stat)) {
		printf("\e[31mWarning: rule failed to build: %s\e[0m\n", FileName);
		pthread_mutex_lock(InterpreterLock);
		return 0;
	}
	if (Stat->st_mtime == PreviousTime) {
		memcpy(Target->Hash, PreviousHash, SHA256_BLOCK_SIZE);
	} else if (S_ISDIR(Stat->st_mode)) {
		memset(Target->Hash, 0xD0, SHA256_BLOCK_SIZE);
#if defined(__APPLE__)
		memcpy(Target->Hash, &Stat->st_mtimespec, sizeof(Stat->st_mtimespec));
#elif defined(__MINGW32__)
		memcpy(Target->Hash, &Stat->st_mtime, sizeof(Stat->st_mtime));
#else
		memcpy(Target->Hash, &Stat->st_mtim, sizeof(Stat->st_mtim));
#endif
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
	pthread_mutex_lock(InterpreterLock);
	/*if (MonitorFiles && !Target->Build) {
		targetwatch_add(FileName, (target_t *)Target);
	}*/
	return Stat->st_mtime;
}

static int target_file_missing(target_file_t *Target) {
	const char *FileName;
	if (Target->Absolute) {
		FileName = Target->Path;
	} else {
		FileName = vfs_resolve(concat(RootPath, "/", Target->Path, NULL));
	}
	struct stat Stat[1];
	return !!stat(FileName, Stat);
}

target_t *target_file_check(const char *Path, int Absolute) {
	const char *Id = concat("file:", Path, NULL);
	target_t **Slot = targetcache_lookup(Id);
	if (!Slot[0]) {
		target_file_t *Target = target_new(target_file_t, FileTargetT, Id, Slot);
		Target->Absolute = Absolute;
		Target->Path = concat(Path, NULL);
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
	Path = vfs_unsolve(Path);
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
		Path = vfs_resolve(concat(RootPath, "/", FileTarget->Path, NULL));
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
	int Recursive;
};

static int target_file_ls_fn(target_file_ls_t *Ls, const char *Path) {
	DIR *Dir = opendir(Path);
	if (!Dir) {
		Ls->Results = ml_error("DirError", "failed to open directory %s", Path);
		return 1;
	}
	struct dirent *Entry = readdir(Dir);
	while (Entry) {
		if (strcmp(Entry->d_name, ".") && strcmp(Entry->d_name, "..")) {
			if (!(Ls->Regex && regexec(Ls->Regex, Entry->d_name, 0, 0, 0))) {
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
			if (Ls->Recursive && (Entry->d_type == DT_DIR)) {
				const char *Subdir = concat(Path, "/", Entry->d_name, NULL);
				printf("Recursing into %s\n", Subdir);
				target_file_ls_fn(Ls, Subdir);
			}
		}
		Entry = readdir(Dir);
	}
	closedir(Dir);
	return 0;
}

ml_value_t *target_file_ls(void *Data, int Count, ml_value_t **Args) {
	target_file_ls_t Ls[1] = {{ml_list(), NULL, 0}};
	for (int I = 1; I < Count; ++I) {
		if (Args[I]->Type == MLStringT) {
			const char *Pattern = ml_string_value(Args[I]);
			Ls->Regex = new(regex_t);
			int Error = regcomp(Ls->Regex, Pattern, REG_NOSUB | REG_EXTENDED);
			if (Error) {
				size_t Length = regerror(Error, Ls->Regex, NULL, 0);
				char *Message = snew(Length + 1);
				regerror(Error, Ls->Regex, Message, Length);
				regfree(Ls->Regex);
				return ml_error("RegexError", "%s", Message);
			}
		} else if (Args[I]->Type == MLMethodT && !strcmp(ml_method_name(Args[I]), "R")) {
			Ls->Recursive = 1;
		}
	}
	target_file_t *Target = (target_file_t *)Args[0];
	if (Target->Absolute) {
		target_file_ls_fn(Ls, Target->Path);
	} else if (Target->Path[0]) {
		vfs_resolve_foreach(concat(RootPath, "/", Target->Path, NULL), Ls, (void *)target_file_ls_fn);
	} else {
		vfs_resolve_foreach(RootPath, Ls, (void *)target_file_ls_fn);
	}
	return Ls->Results;
}

ml_value_t *target_file_dirname(void *Data, int Count, ml_value_t **Args) {
	target_file_t *Target = (target_file_t *)Args[0];
	char *Path;
	if (Target->Absolute) {
		Path = concat(Target->Path, NULL);
	} else if (Target->Path[0]) {
		Path = vfs_resolve(concat(RootPath, "/", Target->Path, NULL));
	} else {
		Path = concat(RootPath, NULL);
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
	return ml_string(concat(Last + 1, NULL), -1);
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
		return ml_string(concat(LastDot + 1, NULL), -1);
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
		FileName = vfs_resolve(concat(RootPath, "/", Target->Path, NULL));
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
		SourcePath = vfs_resolve(concat(RootPath, "/", Source->Path, NULL));
	}
	if (Dest->Absolute) {
		DestPath = Dest->Path;
	} else {
		DestPath = vfs_resolve(concat(RootPath, "/", Dest->Path, NULL));
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
	const char *Path;
	if (FileTarget->Path[0]) {
		Path = concat(FileTarget->Path, "/", ml_string_value(Args[1]), NULL);
	} else {
		Path = ml_string_value(Args[1]);
	}
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
		FileName = vfs_resolve(concat(RootPath, "/", Target->Path, NULL));
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
		FileName = vfs_resolve(concat(RootPath, "/", Target->Path, NULL)); \
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
#ifdef __MINGW32__
#else
TARGET_FILE_IS(lnk, S_ISLNK);
TARGET_FILE_IS(sock, S_ISSOCK);
#endif

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
#ifdef __MINGW32__
	if (stat(Buffer, Stat)) return 0;
#else
	if (lstat(Buffer, Stat)) return 0;
#endif
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

ml_value_t *target_file_path(void *Data, int Count, ml_value_t **Args) {
	target_file_t *Target = (target_file_t *)Args[0];
	return ml_string(Target->Path, -1);
}

struct target_meta_t {
	TARGET_FIELDS
	const char *Name;
};

static ml_type_t *MetaTargetT;

static time_t target_meta_hash(target_meta_t *Target, time_t PreviousTime, unsigned char PreviousHash[SHA256_BLOCK_SIZE], int DependsLastUpdated) {
	if (DependsLastUpdated == CurrentVersion) {
		memset(Target->Hash, 0, SHA256_BLOCK_SIZE);
		memcpy(Target->Hash, &DependsLastUpdated, sizeof(DependsLastUpdated));
	} else {
		memcpy(Target->Hash, PreviousHash, SHA256_BLOCK_SIZE);
	}
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
	ml_value_t *Value;
};

static ml_type_t *ExprTargetT;

static ml_value_t *target_expr_stringify(void *Data, int Count, ml_value_t **Args) {
	ml_stringbuffer_t *Buffer = (ml_stringbuffer_t *)Args[0];
	target_expr_t *Target = (target_expr_t *)Args[1];
	target_depends_auto((target_t *)Target);
	target_queue((target_t *)Target, 0);
	target_wait((target_t *)Target, CurrentTarget);
	return ml_inline(AppendMethod, 2, Buffer, Target->Value);
}

static ml_value_t *target_expr_argify(void *Data, int Count, ml_value_t **Args) {
	target_expr_t *Target = (target_expr_t *)Args[1];
	target_depends_auto((target_t *)Target);
	target_queue((target_t *)Target, 0);
	target_wait((target_t *)Target, CurrentTarget);
	return ml_inline(ArgifyMethod, 2, Args[0], Target->Value);
}

static ml_value_t *target_expr_cmdify(void *Data, int Count, ml_value_t **Args) {
	ml_stringbuffer_t *Buffer = (ml_stringbuffer_t *)Args[0];
	target_expr_t *Target = (target_expr_t *)Args[1];
	target_depends_auto((target_t *)Target);
	target_queue((target_t *)Target, 0);
	target_wait((target_t *)Target, CurrentTarget);
	return ml_inline(AppendMethod, 2, Buffer, Target->Value);
}

static ml_value_t *target_expr_to_string(void *Data, int Count, ml_value_t **Args) {
	target_expr_t *Target = (target_expr_t *)Args[0];
	target_depends_auto((target_t *)Target);
	target_queue((target_t *)Target, 0);
	target_wait((target_t *)Target, CurrentTarget);
	return ml_inline(StringMethod, 1, Target->Value);
}

static time_t target_expr_hash(target_expr_t *Target, time_t PreviousTime, unsigned char PreviousHash[SHA256_BLOCK_SIZE]) {
	//target_queue((target_t *)Target, 0);
	//target_wait((target_t *)Target, CurrentTarget);
	target_value_hash(Target->Value, Target->Hash);
	return 0;
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
		return 0;
	} else if (ml_is(Arg, TargetT)) {
		target_t *Depend = (target_t *)Arg;
		targetset_insert(Target->Depends, Depend);
		return 0;
	} else if (Arg == MLNil) {
		return 0;
	}
	return 1;
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

struct target_scan_t {
	TARGET_FIELDS
	const char *Name;
	target_t *Source;
	targetset_t *Scans;
	ml_value_t *Rebuild;
};

static ml_type_t *ScanTargetT;

static int depends_hash_fn(target_t *Depend, unsigned char Hash[SHA256_BLOCK_SIZE]) {
	for (int I = 0; I < SHA256_BLOCK_SIZE; ++I) Hash[I] ^= Depend->Hash[I];
	return 0;
}

static time_t target_scan_hash(target_scan_t *Target, time_t PreviousTime, unsigned char PreviousHash[SHA256_BLOCK_SIZE]) {
	targetset_t *Scans = cache_scan_get((target_t *)Target);
	if (Scans) targetset_foreach(Scans, Target->Hash, (void *)depends_hash_fn);
	return 0;
}

static ml_value_t *target_scan_source(void *Data, int Count, ml_value_t **Args) {
	target_scan_t *Target = (target_scan_t *)Args[0];
	return (ml_value_t *)Target->Source;
}

static int build_scan_target_list(target_t *Depend, targetset_t *Scans) {
	targetset_insert(Scans, Depend);
	return 0;
}

ml_value_t *target_scan_new(void *Data, int Count, ml_value_t **Args) {
	target_t *Source = (target_t *)Args[0];
	const char *Name = ml_string_value(Args[1]);
	const char *Id = concat("scan:", Source->Id, "::", Name, NULL);
	target_t **Slot = targetcache_lookup(Id);
	if (!Slot[0]) {
		target_scan_t *Target = target_new(target_scan_t, ScanTargetT, Id, Slot);
		Target->Source = Source;
		Target->Name = Name;
		targetset_insert(Target->Depends, Source);
	}
	return (ml_value_t *)Slot[0];
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

static time_t target_symb_hash(target_symb_t *Target, time_t PreviousTime, unsigned char PreviousHash[SHA256_BLOCK_SIZE]) {
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

void target_symb_update(const char *Name) {
	target_t *Target = target_symb_new(Name);
	unsigned char Previous[SHA256_BLOCK_SIZE];
	int LastUpdated, LastChecked;
	time_t FileTime = 0;
	cache_hash_get(Target, &LastUpdated, &LastChecked, &FileTime, Previous);
	FileTime = target_hash(Target, FileTime, Previous, 0);
	if (!LastUpdated || memcmp(Previous, Target->Hash, SHA256_BLOCK_SIZE)) {
		Target->LastUpdated = CurrentVersion;
		cache_hash_set(Target, FileTime);
	} else {
		Target->LastUpdated = LastUpdated;
		cache_last_check_set(Target, FileTime);
	}
}

static int target_depends_auto_single(ml_value_t *Arg, void *Data) {
	if (Arg->Type == MLListT) {
		return ml_list_foreach(Arg, 0, (void *)target_depends_auto_single);
	} else if (Arg->Type == MLStringT) {
		target_t *Depend = target_symb_new(ml_string_value(Arg));
		target_depends_auto(Depend);
		return 0;
	} else if (ml_is(Arg, TargetT)) {
		target_t *Depend = (target_t *)Arg;
		target_depends_auto((target_t *)Arg);
		return 0;
	} else if (Arg == MLNil) {
		return 0;
	}
	return 1;
}

ml_value_t *target_depends_auto_value(void *Data, int Count, ml_value_t **Args) {
	for (int I = 0; I < Count; ++I) target_depends_auto_single(Args[I], 0);
	return MLNil;
}

static int list_update_hash(ml_value_t *Value, SHA256_CTX *Ctx) {
	unsigned char ChildHash[SHA256_BLOCK_SIZE];
	target_value_hash(Value, ChildHash);
	sha256_update(Ctx, ChildHash, SHA256_BLOCK_SIZE);
	return 0;
}

static int tree_update_hash(ml_value_t *Key, ml_value_t *Value, SHA256_CTX *Ctx) {
	unsigned char ChildHash[SHA256_BLOCK_SIZE];
	target_value_hash(Key, ChildHash);
	sha256_update(Ctx, ChildHash, SHA256_BLOCK_SIZE);
	target_value_hash(Value, ChildHash);
	sha256_update(Ctx, ChildHash, SHA256_BLOCK_SIZE);
	return 0;
}

void target_value_hash(ml_value_t *Value, unsigned char Hash[SHA256_BLOCK_SIZE]) {
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
		ml_closure_sha256(Value, Hash);
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
	} else {
		memset(Hash, -1, SHA256_BLOCK_SIZE);
	}
}

static time_t target_hash(target_t *Target, time_t PreviousTime, unsigned char PreviousHash[SHA256_BLOCK_SIZE], int DependsLastUpdated) {
	if (Target->Type == FileTargetT) return target_file_hash((target_file_t *)Target, PreviousTime, PreviousHash);
	if (Target->Type == MetaTargetT) return target_meta_hash((target_meta_t *)Target, PreviousTime, PreviousHash, DependsLastUpdated);
	if (Target->Type == ScanTargetT) return target_scan_hash((target_scan_t *)Target, PreviousTime, PreviousHash);
	if (Target->Type == SymbTargetT) return target_symb_hash((target_symb_t *)Target, PreviousTime, PreviousHash);
	if (Target->Type == ExprTargetT) return target_expr_hash((target_expr_t *)Target, PreviousTime, PreviousHash);
	return 0;
}

static int target_missing(target_t *Target, int LastChecked) {
	if (Target->Type == FileTargetT) return target_file_missing((target_file_t *)Target);
	return 0;
}

target_t *target_find(const char *Id) {
	target_t **Slot = targetcache_lookup(Id);
	if (Slot[0]) return Slot[0];
	if (!memcmp(Id, "file:", 5)) {
		//return target_file_check(Id + 5, Id[5] == '/');
		target_file_t *Target = target_new(target_file_t, FileTargetT, Id, Slot);
		Target->Absolute = Id[5] == '/';
		Target->Path = Id + 5;
		Target->BuildContext = CurrentContext;
		return (target_t *)Target;
	}
	if (!memcmp(Id, "symb:", 5)) {
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
	if (!memcmp(Id, "expr:", 5)) {
		target_expr_t *Target = target_new(target_expr_t, ExprTargetT, Id, Slot);
		return (target_t *)Target;
	}
	if (!memcmp(Id, "scan*:", 6)) {
		target_scan_t *Target = target_new(target_scan_t, ScanTargetT, Id, Slot);
		const char *Name;
		for (Name = Id + strlen(Id) - 1; --Name > Id + 6;) {
			if (Name[0] == ':' && Name[1] == ':') break;
		}
		size_t ParentIdLength = Name - Id - 6;
		char *ParentId = snew(ParentIdLength + 1);
		memcpy(ParentId, Id + 6, ParentIdLength);
		ParentId[ParentIdLength] = 0;
		Target->Source = target_find(ParentId);
		Target->Name = Name + 2;
		return (target_t *)Target;
	}
	if (!memcmp(Id, "scan:", 5)) {
		target_scan_t *Target = target_new(target_scan_t, ScanTargetT, Id, Slot);
		const char *Name;
		for (Name = Id + strlen(Id) - 1; --Name > Id + 5;) {
			if (Name[0] == ':' && Name[1] == ':') break;
		}
		size_t ParentIdLength = Name - Id - 5;
		char *ParentId = snew(ParentIdLength + 1);
		memcpy(ParentId, Id + 5, ParentIdLength);
		ParentId[ParentIdLength] = 0;
		Target->Source = target_find(ParentId);
		Target->Name = Name + 2;
		return (target_t *)Target;
	}
	if (!memcmp(Id, "meta:", 5)) {
		const char *Name;
		for (Name = Id + strlen(Id) - 1; --Name > Id + 8;) {
			if (Name[0] == ':' && Name[1] == ':') break;
		}
		target_meta_t *Target = target_new(target_meta_t, MetaTargetT, Id, Slot);
		Target->Name = Name;
		return (target_t *)Target;
	}
	fprintf(stderr, "internal error: unknown target type: %s\n", Id);
	return 0;
}

target_t *target_get(const char *Id) {
	return *targetcache_lookup(Id);
}

int target_print(target_t *Target, void *Data) {
	printf("%s\n", Target->Id);
	return 0;
}

int target_queue(target_t *Target, target_t *Parent) {
	/*if (Target->LastUpdated == STATE_UNCHECKED) {
		++QueuedTargets;
		Target->Parent = Parent;
		Target->LastUpdated = STATE_QUEUED;
		Target->Next = NextTarget;
		NextTarget = Target;
		pthread_cond_broadcast(TargetAvailable);
	}*/

	if (Target->LastUpdated > 0) return 0;
	if (Parent) {
		Parent->WaitCount += targetset_insert(Target->Affects, Parent);
	}
	if (Target->LastUpdated == STATE_UNCHECKED) {
		//Target->Parent = Parent;
		targetset_foreach(Target->Depends, Target, (void *)target_queue);
		if (Target->WaitCount == 0) {
			++QueuedTargets;
			Target->LastUpdated = STATE_QUEUED;
			Target->Next = NextTarget;
			NextTarget = Target;
			pthread_cond_broadcast(TargetAvailable);
		}
	}

	return 0;
}

int target_affect(target_t *Target, target_t *Depend) {
	--Target->WaitCount;
	if (Target->LastUpdated == STATE_UNCHECKED && Target->WaitCount == 0) {
		//if (!strcmp(Target->Id, "scan*:file:dev/obj/lib/Markdown/Parser.c::INCLUDES")) asm("int3");
		++QueuedTargets;
		Target->LastUpdated = STATE_QUEUED;
		Target->Next = NextTarget;
		NextTarget = Target;
		pthread_cond_broadcast(TargetAvailable);
	}
	return 0;
}

static int target_depends_fn(target_t *Depend, int *DependsLastUpdated) {
	//printf("\e[34m%s version = %d\e[0m\n", Depend->Id, Depend->LastUpdated);
	if (Depend->LastUpdated > *DependsLastUpdated) *DependsLastUpdated = Depend->LastUpdated;
	return 0;
}

static void target_rebuild(target_t *Target) {
	if (!Target->Build && Target->Parent) target_rebuild(Target->Parent);
	if (Target->Build) {
		target_t *OldTarget = CurrentTarget;
		context_t *OldContext = CurrentContext;
		const char *OldDirectory = CurrentDirectory;

		CurrentContext = Target->BuildContext;
		CurrentTarget = Target;
		CurrentDirectory = CurrentContext ? CurrentContext->FullPath : RootPath;
		ml_value_t *Result = ml_inline(Target->Build, 1, Target);
		if (Result->Type == MLErrorT) {
			fprintf(stderr, "\e[31mError: %s: %s\n\e[0m", Target->Id, ml_error_message(Result));
			const char *Source;
			int Line;
			for (int I = 0; ml_error_trace(Result, I, &Source, &Line); ++I) fprintf(stderr, "\e[31m\t%s:%d\n\e[0m", Source, Line);
			exit(1);
		}

		CurrentDirectory = OldDirectory;
		CurrentContext = OldContext;
		CurrentTarget = OldTarget;
	}
}

int target_find_leaves(target_t *Target, targetset_t *Leaves) {
	if (Target->Build || targetset_size(Target->Depends)) {
		targetset_foreach(Target->Depends, Leaves, (void *)target_find_leaves);
		targetset_foreach(Target->BuildDepends, Leaves, (void *)target_find_leaves);
	} else {
		targetset_insert(Leaves, Target);
	}
	return 0;
}

int target_set_parent(target_t *Target, target_t *Parent) {
	if (!Target->Parent) Target->Parent = Parent;
	return 0;
}

int targetset_print(target_t *Target, void *Data) {
	printf("\t%s\n", Target->Id);
	return 0;
}

static int target_graph_dependencies(target_t *Depend, target_t *Target) {
	fprintf(DependencyGraph, "\tT%x -> T%x;\n", Depend, Target);
	return 0;
}

void target_update(target_t *Target) {
	if (DebugThreads) {
		printf("\033[2J\033[H");
		for (build_thread_t *Thread = BuildThreads; Thread; Thread = Thread->Next) {
			if (Thread == CurrentThread) printf("\e[32m");
			switch (Thread->Status) {
			case BUILD_IDLE:
				printf("Thread %2d IDLE\n", Thread->Id);
				break;
			case BUILD_WAIT:
				printf("Thread %2d WAIT\t%s\n", Thread->Id, Thread->Target->Id);
				break;
			case BUILD_EXEC:
				printf("Thread %2d EXEC\t%s\n", Thread->Id, Thread->Target->Id);
				break;
			}
			printf("\e[0m");
		}
		printf("\n\n");
	}
	if (DependencyGraph) {
		fprintf(DependencyGraph, "\tT%x [label=\"%s\"];\n", Target, Target->Id);
		targetset_foreach(Target->Depends, Target, (void *)target_graph_dependencies);
	}
	Target->LastUpdated = STATE_CHECKING;
	int DependsLastUpdated = 0;
	unsigned char PreviousBuildHash[SHA256_BLOCK_SIZE];
	unsigned char BuildHash[SHA256_BLOCK_SIZE];
	if (Target->Build && Target->Build->Type == MLClosureT) {
		ml_closure_sha256(Target->Build, BuildHash);
		int I = 0;
		for (const unsigned char *P = Target->BuildContext->Path; *P; ++P) {
			BuildHash[I] ^= *P;
			I = (I + 1) % SHA256_BLOCK_SIZE;
		}
		Target->BuildContext->Path;
		cache_build_hash_get(Target, PreviousBuildHash);
		if (memcmp(PreviousBuildHash, BuildHash, SHA256_BLOCK_SIZE)) {
			//printf("\e[33mUpdating %s due to build function\e[0m\n", Target->Id);
			DependsLastUpdated = CurrentVersion;
		}
	} else {
		memset(BuildHash, 0, sizeof(SHA256_BLOCK_SIZE));
	}
	targetset_foreach(Target->Depends, Target, (void *)target_queue);
	targetset_foreach(Target->Depends, Target->Parent, (void *)target_set_parent);
	targetset_foreach(Target->Depends, Target, (void *)target_wait);
	targetset_foreach(Target->Depends, &DependsLastUpdated, (void *)target_depends_fn);

	unsigned char Previous[SHA256_BLOCK_SIZE];
	int LastUpdated, LastChecked, Skipped = 0;
	time_t FileTime = 0;
	cache_hash_get(Target, &LastUpdated, &LastChecked, &FileTime, Previous);
	//printf("\e[34m Update \e[32m%s\e[0m last checked at %d\e[0m\n", Target->Id, LastChecked);
	if (DependsLastUpdated <= LastChecked) {
		targetset_t *Depends = cache_depends_get(Target);
		if (Depends) {
			if (DependencyGraph) {
				targetset_foreach(Depends, Target, (void *)target_graph_dependencies);
			}
			targetset_foreach(Depends, Target, (void *)target_queue);
			targetset_foreach(Depends, Target->Parent, (void *)target_set_parent);
			targetset_foreach(Depends, Target, (void *)target_wait);
			targetset_foreach(Depends, &DependsLastUpdated, (void *)target_depends_fn);
		}
	}

	if ((DependsLastUpdated > LastChecked) || target_missing(Target, LastChecked)) {
		//printf("\e[34m rebuilding %s\e[0m\n", Target->Id);
		if (!Target->Build && Target->Parent) target_rebuild(Target->Parent);
		if (DebugThreads) {
			CurrentThread->Status = BUILD_EXEC;
			CurrentThread->Target = Target;
		}
		if (Target->Build) {
			target_t *OldTarget = CurrentTarget;
			context_t *OldContext = CurrentContext;
			const char *OldDirectory = CurrentDirectory;
			CurrentContext = Target->BuildContext;
			CurrentTarget = Target;
			CurrentDirectory = CurrentContext ? CurrentContext->FullPath : RootPath;
			ml_value_t *Result = ml_inline(Target->Build, 1, Target);
			if (Result->Type == MLErrorT) {
				fprintf(stderr, "\e[31mError: %s: %s\n\e[0m", Target->Id, ml_error_message(Result));
				const char *Source;
				int Line;
				for (int I = 0; ml_error_trace(Result, I, &Source, &Line); ++I) fprintf(stderr, "\e[31m\t%s:%d\n\e[0m", Source, Line);
				exit(1);
			}
			if (Target->Type == ExprTargetT) {
				((target_expr_t *)Target)->Value = Result;
				cache_expr_set(Target, Result);
			} else if (Target->Type == ScanTargetT) {
				targetset_t Scans[1] = {TARGETSET_INIT};
				targetset_init(Scans, ml_list_length(Result));
				ml_list_foreach(Result, Scans, (void *)build_scan_target_list);
				cache_scan_set(Target, Scans);
				if (DependencyGraph) {
					targetset_foreach(Scans, Target, (void *)target_graph_dependencies);
				}
				targetset_foreach(Scans, 0, (void *)target_queue);
				targetset_foreach(Scans, Target, (void *)target_wait);
				targetset_foreach(Scans, Target->BuildDepends, (void *)target_find_leaves);

				//printf("scans(%s) = \n", Target->Id);
				//targetset_foreach(Scans, 0, targetset_print);
				//printf("leaves(%s) = \n", Target->Id);
				//targetset_foreach(Target->BuildDepends, 0, targetset_print);
			}
			cache_build_hash_set(Target, BuildHash);

			CurrentDirectory = OldDirectory;
			CurrentContext = OldContext;
			CurrentTarget = OldTarget;
		}
	} else {
		if (Target->Type == ExprTargetT) {
			((target_expr_t *)Target)->Value = cache_expr_get(Target);
		} else if (Target->Type == ScanTargetT) {
			targetset_t *Scans = cache_scan_get(Target);
			if (DependencyGraph) {
				targetset_foreach(Scans, Target, (void *)target_graph_dependencies);
			}
			targetset_foreach(Scans, Target, (void *)target_queue);
			targetset_foreach(Scans, Target, (void *)target_set_parent);
			targetset_foreach(Scans, Target, (void *)target_wait);

			//printf("scans(%s) = \n", Target->Id);
			//targetset_foreach(Scans, 0, targetset_print);
		}
	}
	FileTime = target_hash(Target, FileTime, Previous, DependsLastUpdated);
	if (!LastUpdated || memcmp(Previous, Target->Hash, SHA256_BLOCK_SIZE)) {
		Target->LastUpdated = CurrentVersion;
		cache_hash_set(Target, FileTime);
		cache_depends_set(Target, Target->BuildDepends);
	} else {
		Target->LastUpdated = LastUpdated;
		cache_last_check_set(Target, FileTime);
	}
	++BuiltTargets;
	if (StatusUpdates) printf("\e[35m%d / %d\e[0m #%d Updated \e[32m%s\e[0m to version %d\n", BuiltTargets, QueuedTargets, CurrentThread->Id, Target->Id, Target->LastUpdated);
	pthread_cond_broadcast(TargetUpdated);

	targetset_foreach(Target->Affects, Target, (void *)target_affect);

#ifdef Linux
	if (WatchMode && !Target->Build && Target->Type == FileTargetT) {
		target_file_t *FileTarget = (target_file_t *)Target;
		if (!FileTarget->Absolute) {
			targetwatch_add(vfs_resolve(FileTarget->Path));
		}
	}
#endif
}

int target_wait(target_t *Target, target_t *Waiter) {
	if (Target->LastUpdated == STATE_UNCHECKED) {
		++QueuedTargets;
		Target->LastUpdated = STATE_QUEUED;
	}
	if (Target->LastUpdated == STATE_QUEUED) {
		target_update(Target);
	} else while (Target->LastUpdated == STATE_CHECKING) {
		if (DebugThreads) {
			CurrentThread->Status = BUILD_WAIT;
			CurrentThread->Target = Target;
		}
		//fprintf(stderr, "\e[31m%s waiting on %s\n\e[0m", Waiter->Id, Target->Id);
		pthread_cond_wait(TargetUpdated, InterpreterLock);
	}
	if (DebugThreads) {
		CurrentThread->Status = BUILD_EXEC;
		CurrentThread->Target = Waiter;
	}
	return 0;
}

static void *target_thread_fn(void *Arg) {
	CurrentThread = (build_thread_t *)Arg;
	const char *Path = getcwd(NULL, 0);
	char *Path2 = GC_malloc_atomic_uncollectable(strlen(Path) + 1);
	CurrentDirectory = strcpy(Path2, Path);
	pthread_mutex_lock(InterpreterLock);
	++RunningThreads;
	for (;;) {
		while (!NextTarget) {
			if (DebugThreads) CurrentThread->Status = BUILD_IDLE;
			if (--RunningThreads == 0) {
				pthread_cond_signal(TargetAvailable);
				pthread_mutex_unlock(InterpreterLock);
				return 0;
			}
			pthread_cond_wait(TargetAvailable, InterpreterLock);
			++RunningThreads;
		}
		target_t *Target = NextTarget;
		NextTarget = Target->Next;
		if (Target->LastUpdated == STATE_QUEUED) target_update(Target);
	}
	return 0;
}

void target_threads_start(int NumThreads) {
	CurrentThread = new(build_thread_t);
	CurrentThread->Id = 0;
	CurrentThread->Status = BUILD_IDLE;
	RunningThreads = 1;
	pthread_mutex_init(InterpreterLock, NULL);
	pthread_mutex_lock(InterpreterLock);
	for (LastThread = 0; LastThread < NumThreads; ++LastThread) {
		build_thread_t *BuildThread = new(build_thread_t);
		BuildThread->Id = LastThread;
		BuildThread->Status = BUILD_IDLE;
		pthread_create(&BuildThread->Handle, 0, target_thread_fn, BuildThread);
		BuildThread->Next = BuildThreads;
		BuildThreads = BuildThread;
	}
}

void target_interactive_start(int NumThreads) {
	CurrentThread = new(build_thread_t);
	CurrentThread->Id = 0;
	CurrentThread->Status = BUILD_IDLE;
	RunningThreads = 0;
	pthread_mutex_init(InterpreterLock, NULL);
	pthread_mutex_lock(InterpreterLock);
	/*for (LastThread = 0; LastThread < NumThreads; ++LastThread) {
		build_thread_t *BuildThread = new(build_thread_t);
		BuildThread->Id = LastThread;
		BuildThread->Status = BUILD_IDLE;
		pthread_create(&BuildThread->Handle, 0, active_mode_thread_fn, BuildThread);
		BuildThread->Next = BuildThreads;
		BuildThreads = BuildThread;
	}*/
	pthread_cond_broadcast(TargetAvailable);
	pthread_mutex_unlock(InterpreterLock);
}

void target_threads_wait(target_t *Target) {
	--RunningThreads;
	target_queue(Target, 0);
	pthread_mutex_unlock(InterpreterLock);
	while (BuildThreads) {
		pthread_join(BuildThreads->Handle, 0);
		BuildThreads = BuildThreads->Next;
	}
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
	SymbTargetT = ml_class(TargetT, "symb-target");
	SymbTargetT->deref = symb_target_deref;
	SymbTargetT->assign = symb_target_assign;
	SHA256Method = ml_method("sha256");
	MissingMethod = ml_method("missing");
	ml_method_by_value(AppendMethod, 0, target_file_stringify, MLStringBufferT, FileTargetT, NULL);
	ml_method_by_value(AppendMethod, 0, target_expr_stringify, MLStringBufferT, ExprTargetT, NULL);
	ml_method_by_value(ArgifyMethod, 0, target_file_argify, MLListT, FileTargetT, NULL);
	ml_method_by_value(ArgifyMethod, 0, target_expr_argify, MLListT, ExprTargetT, NULL);
	ml_method_by_value(CmdifyMethod, 0, target_file_cmdify, MLStringBufferT, FileTargetT, NULL);
	ml_method_by_value(CmdifyMethod, 0, target_expr_cmdify, MLStringBufferT, ExprTargetT, NULL);
	ml_method_by_name("[]", 0, target_depend, TargetT, MLAnyT, NULL);
	ml_method_by_name("scan", 0, target_scan_new, TargetT, NULL);
	ml_method_by_name("string", 0, target_file_to_string, FileTargetT, NULL);
	ml_method_by_name("string", 0, target_expr_to_string, ExprTargetT, NULL);
	ml_method_by_name("=>", 0, target_set_build, TargetT, MLAnyT, NULL);
	ml_method_by_name("build", 0, target_set_build, TargetT, MLAnyT, NULL);
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
	ml_method_by_name("path", 0, target_file_path, FileTargetT, NULL);
	target_file_methods_is(dir);
	target_file_methods_is(chr);
	target_file_methods_is(blk);
	target_file_methods_is(reg);
	target_file_methods_is(fifo);
#ifdef __MINGW32__
#else
	target_file_methods_is(lnk);
	target_file_methods_is(sock);
#endif
}
