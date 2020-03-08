#include "target_file.h"

#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <regex.h>
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

extern ml_value_t *ArgifyMethod;
extern ml_value_t *CmdifyMethod;

struct target_file_t {
	target_t Base;
	int Absolute;
	const char *Path;
};

ml_type_t *FileTargetT;

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
	return MLSome;
}

static ml_value_t *target_file_argify(void *Data, int Count, ml_value_t **Args) {
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
	return MLSome;
}

static ml_value_t *target_file_to_string(void *Data, int Count, ml_value_t **Args) {
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
		printf("\e[31mError: file does not exist: %s\e[0m\n", FileName);
		targetset_foreach(Target->Base.Affects, NULL, target_file_affects_fn);
		memcpy(Target->Base.Hash, PreviousHash, SHA256_BLOCK_SIZE);
		return PreviousTime;
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
	ml_value_t *FilterFn;
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
				if (Ls->FilterFn) {
					ml_value_t *Result = ml_inline(Ls->FilterFn, 1, File);
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
		} else if (Args[I]->Type == MLRegexT) {
			Ls->Regex = ml_regex_value(Args[I]);
		} else if (Args[I]->Type == MLMethodT && !strcmp(ml_method_name(Args[I]), "R")) {
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

ml_value_t *target_file_map(void *Data, int Count, ml_value_t **Args) {
	target_file_t *Input = (target_file_t *)Args[0];
	target_file_t *Source = (target_file_t *)Args[1];
	target_file_t *Dest = (target_file_t *)Args[2];
	const char *Relative = match_prefix(Input->Path, Source->Path);
	if (!Relative) return ml_error("PathError", "File is not in source");
	const char *Path = concat(Dest->Path, Relative, NULL);
	target_t *Target = target_file_check(Path, Dest->Absolute);
	return (ml_value_t *)Target;
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

ml_value_t *target_file_rmdir(void *Data, int Count, ml_value_t **Args) {
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

ml_value_t *target_file_chdir(void *Data, int Count, ml_value_t **Args) {
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

ml_value_t *target_file_path(void *Data, int Count, ml_value_t **Args) {
	target_file_t *Target = (target_file_t *)Args[0];
	return ml_string(Target->Path, -1);
}

#define target_file_methods_is(TYPE) \
	ml_method_by_name("is" #TYPE, 0, target_file_is_ ## TYPE, FileTargetT, NULL);

void target_file_init() {
	FileTargetT = ml_type(TargetT, "file-target");
	ml_method_by_value(MLStringBufferAppendMethod, 0, target_file_stringify, MLStringBufferT, FileTargetT, NULL);
	ml_method_by_value(ArgifyMethod, 0, target_file_argify, MLListT, FileTargetT, NULL);
	ml_method_by_value(CmdifyMethod, 0, target_file_cmdify, MLStringBufferT, FileTargetT, NULL);
	ml_method_by_name("string", 0, target_file_to_string, FileTargetT, NULL);
	ml_method_by_name("/", 0, target_file_div, FileTargetT, MLStringT, NULL);
	ml_method_by_name("%", 0, target_file_mod, FileTargetT, MLStringT, NULL);
	ml_method_by_name("dir", 0, target_file_dir, FileTargetT, NULL);
	ml_method_by_name("dirname", 0, target_file_dirname, FileTargetT, NULL);
	ml_method_by_name("basename", 0, target_file_basename, FileTargetT, NULL);
	ml_method_by_name("extension", 0, target_file_extension, FileTargetT, NULL);
	ml_method_by_name("map", 0, target_file_map, FileTargetT, FileTargetT, FileTargetT, NULL);
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
