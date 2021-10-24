#include "target_file.h"

#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <gc/gc.h>
#include "rabs.h"
#include "util.h"
#include "targetcache.h"
#include "ml_file.h"

#ifdef Linux
#include <sys/sendfile.h>
#include "targetwatch.h"
#endif

#undef ML_CATEGORY
#define ML_CATEGORY "file"

extern ml_value_t *ArgifyMethod;

struct target_file_t {
	target_t Base;
	int Absolute;
	const char *Path;
};

ML_TYPE(FileTargetT, (TargetT), "file-target");
// A file target represents a single file or directory in the filesystem.
// File targets are stored relative to the project root whenever possible, taking into account virtual mounts. They are automatically resolving to absolute paths when required.

static int target_file_affects_fn(target_t *Target, void *Data) {
	fprintf(stderr, "\t\e[31mRequired by %s\e[0m\n", Target->Id);
	return 0;
}

time_t target_file_hash(target_file_t *Target, time_t PreviousTime, unsigned char PreviousHash[SHA256_BLOCK_SIZE]) {
	const char *FileName;
	if (Target->Absolute) {
		FileName = Target->Path;
	} else {
		FileName = vfs_resolve(concat(RootPath, "/", Target->Path, NULL));
	}
	pthread_mutex_unlock(InterpreterLock);
	struct stat Stat[1];
	if (stat(FileName, Stat)) {
		pthread_mutex_lock(InterpreterLock);
		printf("\e[33mWarning: file does not exist: %s\e[0m\n", FileName);
		targetset_foreach(Target->Base.Affects, NULL, target_file_affects_fn);
		memset(Target->Base.Hash, 0xF0, SHA256_BLOCK_SIZE);
		return 0;
	}
	if (Stat->st_mtime == PreviousTime) {
		memcpy(Target->Base.Hash, PreviousHash, SHA256_BLOCK_SIZE);
	} else if (S_ISDIR(Stat->st_mode)) {
		memset(Target->Base.Hash, 0xD0, SHA256_BLOCK_SIZE);
#if defined(__APPLE__)
		memcpy(Target->Base.Hash, &Stat->st_mtimespec, sizeof(Stat->st_mtimespec));
#elif defined(__MINGW32__)
		memcpy(Target->Base.Hash, &Stat->st_mtime, sizeof(Stat->st_mtime));
#else
		memcpy(Target->Base.Hash, &Stat->st_mtim, sizeof(Stat->st_mtim));
#endif
	} else {
		int File = open(FileName, 0, O_RDONLY);
		if (!File) {
			fprintf(stderr, "\e[31mError: error opening: %s\e[0m\n", FileName);
			targetset_foreach(Target->Base.Affects, NULL, target_file_affects_fn);
			exit(1);
		}
		SHA256_CTX Ctx[1];
		uint8_t Buffer[8192];
		sha256_init(Ctx);
		for (;;) {
			int Count = read(File, Buffer, 8192);
			if (Count == 0) break;
			if (Count == -1) {
				fprintf(stderr, "\e[31mError: error reading: %s\e[0m\n", FileName);
				targetset_foreach(Target->Base.Affects, NULL, target_file_affects_fn);
				exit(1);
			}
			sha256_update(Ctx, Buffer, Count);
		}
		close(File);
		sha256_final(Ctx, Target->Base.Hash);
	}
	pthread_mutex_lock(InterpreterLock);
	return Stat->st_mtime;
}

int target_file_missing(target_file_t *Target) {
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
	char *Id;
	asprintf(&Id, "file:%s", Path);
	target_index_slot R = targetcache_insert(Id);
	if (!R.Slot[0]) {
		target_file_t *Target = target_new(target_file_t, FileTargetT, Id, R.Index, R.Slot);
		Target->Absolute = Absolute;
		Target->Path = concat(Path, NULL);
		Target->Base.BuildContext = CurrentContext;
	}
	return R.Slot[0];
}

ML_FUNCTION(File) {
//<Path
//>filetarget
// Returns a new file target. If :mini:`Path` does not begin with `/`, it is considered relative to the current context path. If :mini:`Path` is specified as an absolute path but lies inside the project directory, it is converted into a relative path.
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
		Target = target_file_check(Relative + (Relative[0] == '/'), 0);
	} else {
		Target = target_file_check(Path, 1);
	}
	if (Count > 1) {
		Target->Build = Args[1];
		Target->BuildContext = CurrentContext;
	}
	return (ml_value_t *)Target;
}

ML_METHOD(MLStringT, FileTargetT) {
//<Target
//>string
// Returns the absolute resolved path of :mini:`Target` as a string.
	target_file_t *Target = (target_file_t *)Args[0];
	const char *Path;
	if (Target->Absolute) {
		Path = Target->Path;
	} else if (Target->Path[0]) {
		Path = vfs_resolve(concat(RootPath, "/", Target->Path, NULL));
	} else {
		Path = RootPath;
	}
	//Path = relative_path(Path, CurrentDirectory);
	return ml_string(Path, strlen(Path));
}

ML_METHOD(ArgifyMethod, MLListT, FileTargetT) {
//!internal
	target_file_t *Target = (target_file_t *)Args[1];
	const char *Path;
	if (Target->Absolute) {
		Path = Target->Path;
	} else if (Target->Path[0]) {
		Path = vfs_resolve(concat(RootPath, "/", Target->Path, NULL));
	} else {
		Path = RootPath;
	}
	//Path = relative_path(Path, CurrentDirectory);
	ml_list_append(Args[0], ml_string(Path, strlen(Path)));
	return Args[0];
}

ML_METHOD("append", MLStringBufferT, FileTargetT) {
//!internal
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
	//Path = relative_path(Path, CurrentDirectory);
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
	return Args[0];
}

target_t *target_file_create(const char *Id, context_t *BuildContext, size_t Index, target_t **Slot) {
	target_file_t *Target = target_new(target_file_t, FileTargetT, Id, Index, Slot);
	Target->Absolute = Id[5] == '/';
	Target->Path = Id + 5;
	Target->Base.BuildContext = CurrentContext;
	return (target_t *)Target;
}

void target_file_watch(target_file_t *Target) {
#ifdef Linux
	if (!Target->Absolute) targetwatch_add(vfs_resolve(Target->Path));
#endif
}

ML_METHOD("dir", FileTargetT) {
//<Target
//>filetarget
// Returns the directory containing :mini:`Target`.
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
	ml_value_t *FilterFn;
	ml_value_t *Regex;
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
			if (!(Ls->Regex && ml_regex_match(Ls->Regex, Entry->d_name, strlen(Entry->d_name)))) {
				const char *Absolute = concat(Path, "/", Entry->d_name, NULL);
				const char *Relative = match_prefix(Absolute, RootPath);
				target_t *File;
				if (Relative) {
					File = target_file_check(Relative + 1, 0);
				} else {
					File = target_file_check(Absolute, 1);
				}
				if (Ls->FilterFn) {
					ml_value_t *Result = ml_simple_inline(Ls->FilterFn, 1, File);
					if (Result->Type == MLErrorT) {
						Ls->Results = Result;
						return 1;
					} else if (Result != MLNil) {
						ml_list_append(Ls->Results, (ml_value_t *)File);
					}
				} else {
					ml_list_append(Ls->Results, (ml_value_t *)File);
				}
			}
			if (Ls->Recursive && (Entry->d_type == DT_DIR)) {
				const char *Subdir = concat(Path, "/", Entry->d_name, NULL);
				target_file_ls_fn(Ls, Subdir);
			}
		}
		Entry = readdir(Dir);
	}
	closedir(Dir);
	return 0;
}

ML_METHOD_DECL(RecursiveMethod, "R");

ML_METHODV("ls", FileTargetT) {
//<Directory
//<Pattern?:string|regex
//<Recursive?:method
//<Filter?:function
//>list[target]
// Returns a list of the contents of :mini:`Directory`. Passing :mini:`:R` results in a recursive list.
	target_file_ls_t Ls[1] = {{ml_list(), NULL, 0}};
	for (int I = 1; I < Count; ++I) {
		if (Args[I]->Type == MLStringT) {
			const char *Pattern = ml_string_value(Args[I]);
			int Length = ml_string_length(Args[I]);
			Ls->Regex = ml_regex(Pattern, Length);
			if (ml_is_error(Ls->Regex)) return Ls->Regex;
		} else if (Args[I]->Type == MLRegexT) {
			Ls->Regex = Args[I];
		} else if (Args[I] == RecursiveMethod) {
			Ls->Recursive = 1;
		} else {
			Ls->FilterFn = Args[I];
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

ML_METHOD("dirname", FileTargetT) {
//<Target
//>string
// Returns the directory containing :mini:`Target` as a string. Virtual mounts are not applied to the result (unlike :mini:`Target:dir`).
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

ML_METHOD("basename", FileTargetT) {
//<Target
//>string
// Returns the filename component of :mini:`Target`.
	target_file_t *Target = (target_file_t *)Args[0];
	const char *Path = Target->Path;
	const char *Last = Path - 1;
	for (const char *P = Path; *P; ++P) if (*P == '/') Last = P;
	return ml_string(concat(Last + 1, NULL), -1);
}

ML_METHOD("extension", FileTargetT) {
//<Target
//>string
// Returns the file extension of :mini:`Target`.
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

ML_METHOD("map", FileTargetT, FileTargetT, FileTargetT) {
//<Target
//<Source
//<Dest
//>filetarget|error
// Returns the relative path of :mini:`Target` in :mini:`Source` applied to :mini:`Dest`, or an error if :mini:`Target` is not contained in :mini:`Source`.
	target_file_t *Input = (target_file_t *)Args[0];
	target_file_t *Source = (target_file_t *)Args[1];
	target_file_t *Dest = (target_file_t *)Args[2];
	const char *Relative = match_prefix(Input->Path, Source->Path);
	if (!Relative) return ml_error("PathError", "File is not in source");
	const char *Path = concat(Dest->Path, Relative, NULL);
	target_t *Target = target_file_check(Path, Dest->Absolute);
	return (ml_value_t *)Target;
}

ML_METHOD("-", FileTargetT, FileTargetT) {
//<Target
//<Source
//>string|nil
// Returns the relative path of :mini:`Target` in :mini:`Source`, or :mini:`nil` if :mini:`Target` is not contained in :mini:`Source`.
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

ML_METHOD("exists", FileTargetT) {
//<Target
//>filetarget|nil
// Returns :mini:`Target` if the file or directory exists or has a build function defined, otherwise returns :mini:`nil`.
	target_file_t *Target = (target_file_t *)Args[0];
	if (Target->Base.Build /*&& Target->Build->Type == MLClosureT*/) return (ml_value_t *)Target;
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

ML_METHOD("copy", FileTargetT, FileTargetT) {
//<Source
//<Dest
//>nil
// Copies the contents of :mini:`Source` to :mini:`Dest`.
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
	struct stat Stat[1];
	if (fstat(SourceFile, Stat)) {
		close(SourceFile);
		return ml_error("FileError", "could not open get source details %s", SourcePath);
	}
	int DestFile = open(DestPath, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IRGRP| S_IROTH | S_IWUSR | S_IWGRP| S_IWOTH);
	if (DestFile < 0) {
		close(SourceFile);
		return ml_error("FileError", "could not open destination %s", DestPath);
	}
#ifdef Linux
	int Length = sendfile(DestFile, SourceFile, NULL, Stat->st_size);
#else
	char *Buffer = snew(4096);
	int Length;
	while ((Length = read(SourceFile, Buffer, 4096)) > 0 && write(DestFile, Buffer, Length) > 0);
#endif
	close(SourceFile);
	close(DestFile);
	if (Length < 0) return ml_error("FileError", "file copy failed");
	return MLNil;
}

ML_METHOD("/", FileTargetT, MLStringT) {
//<Directory
//<Name
//>filetarget
// Returns a new file target at :mini:`Directory`\ ``/``\ :mini:`Name`.
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

ML_METHOD("%", FileTargetT, MLStringT) {
//<File
//<Extension
//>filetarget
// Returns a new file target by replacing the extension of :mini:`File` with :mini:`Extension`.
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

ML_METHOD("open", FileTargetT, MLStringT) {
//<Target
//<Mode
//>file
// Opens the file at :mini:`Target` with mode :mini:`Mode`.
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

ML_METHOD("mkdir", FileTargetT) {
//<Directory
//>filetarget
// Creates the all directories in the path of :mini:`Directory`. Returns :mini:`Directory`.
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

typedef struct rmdir_t {
	char *Path;
	size_t Size;
} rmdir_t;

static int rmdir_p(rmdir_t *Info, size_t End) {
	struct stat Stat[1];
#ifdef __MINGW32__
	if (stat(Buffer, Stat)) return 0;
#else
	if (lstat(Info->Path, Stat)) return 0;
#endif
	if (S_ISDIR(Stat->st_mode)) {
		DIR *Dir = opendir(Info->Path);
		if (!Dir) return 1;
		Info->Path[End] = '/';
		struct dirent *Entry = readdir(Dir);
		while (Entry) {
			if (strcmp(Entry->d_name, ".") && strcmp(Entry->d_name, "..")) {
				size_t End2 = End + 1 + strlen(Entry->d_name);
				if (End2 >= Info->Size) {
					size_t Size = ((End2 + 64) / 64) * 64;
					char *Path = snew(Size);
					memcpy(Path, Info->Path, End + 1);
					Info->Path = Path;
					Info->Size = Size;
				}
				strcpy(Info->Path + End + 1, Entry->d_name);
				if (rmdir_p(Info, End2)) {
					closedir(Dir);
					return 1;
				}
			}
			Entry = readdir(Dir);
		}
		closedir(Dir);
		Info->Path[End] = 0;
		if (rmdir(Info->Path)) return 1;
	} else {
		if (unlink(Info->Path)) return 1;
	}
	return 0;
}

ML_METHOD("rmdir", FileTargetT) {
//<Target
//>filetarget
// Removes :mini:`Target` recursively. Returns :mini:`Directory`.
	target_file_t *Target = (target_file_t *)Args[0];
	const char *Path;
	if (Target->Absolute) {
		Path = concat(Target->Path, NULL);
	} else {
		Path = concat(RootPath, "/", Target->Path, NULL);
	}
	size_t End = strlen(Path);
	if (!End) return Args[0];
	size_t Size = ((End + 64) / 64) * 64;
	char *Buffer = snew(Size);
	strcpy(Buffer, Path);
	rmdir_t Info = {Buffer, Size};
	if (rmdir_p(&Info, End) < 0) {
		return ml_error("FileError", "error removing file / directory %s", Buffer);
	}
	return Args[0];
}

ML_METHOD("chdir", FileTargetT) {
//<Directory
//>filetarget
// Changes the current directory to :mini:`Directory`. Returns :mini:`Directory`.
	target_file_t *Target = (target_file_t *)Args[0];
	if (Target->Absolute) {
		char *Path2 = GC_MALLOC_ATOMIC(strlen(Target->Path) + 1);
		CurrentDirectory = strcpy(Path2, Target->Path);
	} else {
		const char *Path = concat(RootPath, "/", Target->Path, NULL);
		char *Path2 = GC_MALLOC_ATOMIC(strlen(Path) + 1);
		CurrentDirectory = strcpy(Path2, Path);
	}
	return Args[0];
}

ML_METHOD("path", FileTargetT) {
//<Target
//>string
// Returns the internal (possibly unresolved and relative to project root) path of :mini:`Target`.
	target_file_t *Target = (target_file_t *)Args[0];
	return ml_string(Target->Path, -1);
}

#define target_file_methods_is(TYPE) \
	ml_method_by_name("is" #TYPE, 0, target_file_is_ ## TYPE, FileTargetT, NULL);

void target_file_init() {
#include "target_file_init.c"
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
