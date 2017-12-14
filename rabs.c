#include <gc/gc.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>
#include "target.h"
#include "context.h"
#include "util.h"
#include "cache.h"
#include "minilang.h"
#include "ml_file.h"
#include "rabs.h"

const char *SystemName = "/_minibuild_";
const char *RootPath = 0;
ml_value_t *AppendMethod;

static stringmap_t Globals[1] = {STRINGMAP_INIT};

static ml_value_t *rabs_ml_get(void *Data, const char *Name) {
	ml_value_t *Value = context_symb_get(CurrentContext, Name);
	if (Value) {
		target_t *Target = target_symb_new(Name);
		target_depends_auto(Target);
		target_update(Target);
		return Value;
	} else {
		return stringmap_search(Globals, Name) ?: Nil;
	}
}

static ml_value_t *rabs_ml_set(void *Data, const char *Name, ml_value_t *Value) {
	context_symb_set(CurrentContext, Name, Value);
	return Value;
}

static ml_value_t *rabs_ml_global(void *Data, const char *Name) {
	static stringmap_t Cache[1] = {STRINGMAP_INIT};
	ml_value_t *Value = stringmap_search(Cache, Name);
	if (!Value) {
		Value = ml_property(Data, Name, rabs_ml_get, rabs_ml_set, 0, 0);
		stringmap_insert(Cache, Name, Value);
	}
	return Value;
}

static void load_file(const char *FileName) {
	ml_value_t *Closure = ml_load(rabs_ml_global, 0, FileName);
	if (Closure->Type == ErrorT) {
		printf("\e[31mError: %s\n\e[0m", ml_error_message(Closure));
		const char *Source;
		int Line;
		for (int I = 0; ml_error_trace(Closure, I, &Source, &Line); ++I) printf("\e[31m\t%s:%d\n\e[0m", Source, Line);
		exit(1);
	}
	ml_value_t *Result = ml_call(Closure, 0, 0);
	if (Result->Type == ErrorT) {
		printf("\e[31mError: %s\n\e[0m", ml_error_message(Result));
		const char *Source;
		int Line;
		for (int I = 0; ml_error_trace(Result, I, &Source, &Line); ++I) printf("\e[31m\t%s:%d\n\e[0m", Source, Line);
		exit(1);
	}
}

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

ml_value_t *subdir(void *Data, int Count, ml_value_t **Args) {
	const char *Path = ml_string_value(Args[0]);
	Path = concat(CurrentContext->Path, "/", Path, 0);
	//printf("Path = %s\n", Path);
	mkdir_p(concat(RootPath, Path, 0));
	const char *FileName = concat(RootPath, Path, SystemName, 0);
	//printf("FileName = %s\n", FileName);
	FileName = vfs_resolve(CurrentContext->Mounts, FileName);
	target_t *ParentDefault = CurrentContext->Default;
	context_push(Path);
	target_depends_add(ParentDefault, CurrentContext->Default);
	load_file(FileName);
	context_pop();
	return Nil;
}

ml_value_t *scope(void *Data, int Count, ml_value_t **Args) {
	const char *Name = ml_string_value(Args[0]);
	context_scope(Name);
	ml_value_t *Result = ml_call(Args[1], 0, 0);
	if (Result->Type == ErrorT) {
		printf("Error: %s\n", ml_error_message(Result));
		const char *Source;
		int Line;
		for (int I = 0; ml_error_trace(Result, I, &Source, &Line); ++I) printf("\e[31m\t%s:%d\n\e[0m", Source, Line);
		exit(1);
	}
	context_pop();
	return Nil;
}

ml_value_t *include(void *Data, int Count, ml_value_t **Args) {
	const char *FileName = ml_string_value(Args[0]);
	if (FileName[0] != '/') {
		FileName = concat(RootPath, CurrentContext->Path, "/", FileName, 0);
		FileName = vfs_resolve(CurrentContext->Mounts, FileName);
	}
	load_file(FileName);
	return Nil;
}

ml_value_t *vmount(void *Data, int Count, ml_value_t **Args) {
	const char *Path = ml_string_value(Args[0]);
	const char *Target = ml_string_value(Args[1]);
	CurrentContext->Mounts = vfs_mount(CurrentContext->Mounts,
		concat(CurrentContext->Path, "/", Path, 0),
		concat(CurrentContext->Path, "/", Target, 0)
	);
	return Nil;
}

ml_value_t *context(void *Data, int Count, ml_value_t **Args) {
	return ml_string(CurrentContext->Path, -1);
}

ml_value_t *execute(void *Data, int Count, ml_value_t **Args) {
	ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
	for (int I = 0; I < Count; ++I) if (ml_inline(AppendMethod, 2, Buffer, Args[I]) != Nil) ml_stringbuffer_add(Buffer, " ", 1);
	const char *Command = ml_stringbuffer_get(Buffer);
	printf("\e[34m%s: %s\e[0m\n", CurrentContext->FullPath, Command);
	clock_t Start = clock();
	chdir(CurrentContext->FullPath);
	FILE *File = popen(Command, "r");
	pthread_mutex_unlock(GlobalLock);
	char Chars[120];
	while (!feof(File)) {
		ssize_t Size = fread(Chars, 1, 120, File);
		if (Size == -1) break;
		//fwrite(Chars, 1, Size, stdout);
	}
	int Result = pclose(File);
	clock_t End = clock();
	pthread_mutex_lock(GlobalLock);
	printf("\t\e[34m%f seconds.\e[0m\n", ((double)(End - Start)) / CLOCKS_PER_SEC);
	if (WIFEXITED(Result)) {
		if (WEXITSTATUS(Result) != 0) {
			return ml_error("ExecuteError", "process returned non-zero exit code");
		} else {
			return Nil;
		}
	} else {
		return ml_error("ExecuteError", "process exited abnormally");
	}
}

ml_value_t *shell(void *Data, int Count, ml_value_t **Args) {
	ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
	for (int I = 0; I < Count; ++I) if (ml_inline(AppendMethod, 2, Buffer, Args[I]) != Nil) ml_stringbuffer_add(Buffer, " ", 1);
	const char *Command = ml_stringbuffer_get(Buffer);
	printf("\e[34m%s\e[0m\n", Command);
	clock_t Start = clock();
	chdir(CurrentContext->FullPath);
	FILE *File = popen(Command, "r");
	pthread_mutex_unlock(GlobalLock);
	char Chars[ML_STRINGBUFFER_NODE_SIZE];
	while (!feof(File)) {
		ssize_t Size = fread(Chars, 1, ML_STRINGBUFFER_NODE_SIZE, File);
		if (Size == -1) break;
		//pthread_mutex_lock(GlobalLock);
		if (Size > 0) ml_stringbuffer_add(Buffer, Chars, Size);
		//pthread_mutex_unlock(GlobalLock);
	}
	int Result = pclose(File);
	clock_t End = clock();
	pthread_mutex_lock(GlobalLock);
	printf("\t\e[34m%f seconds.\e[0m\n", ((double)(End - Start)) / CLOCKS_PER_SEC);
	if (WIFEXITED(Result)) {
		if (WEXITSTATUS(Result) != 0) {
			return ml_error("ExecuteError", "process returned non-zero exit code");
		} else {
			ml_value_t *Result = ml_string(ml_stringbuffer_get(Buffer), Buffer->Length);
			return Result;
		}
	} else {
		return ml_error("ExecuteError", "process exited abnormally");
	}
}

ml_value_t *rabs_mkdir(void *Data, int Count, ml_value_t **Args) {
	ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
	for (int I = 0; I < Count; ++I) ml_inline(AppendMethod, 2, Buffer, Args[I]);
	char *Path = ml_stringbuffer_get(Buffer);
	if (mkdir_p(Path) < 0) {
		return ml_error("FileError", "error creating directory %s", Path);
	}
	return Nil;
}

static const char *find_root(const char *Path) {
	char *FileName = snew(strlen(Path) + strlen(SystemName));
	char *End = stpcpy(FileName, Path);
	strcpy(End, SystemName);
	char Line[strlen("-- ROOT --\n")];
	FILE *File = 0;
loop:
	File = fopen(FileName, "r");
	if (File) {
		if (fread(Line, 1, sizeof(Line), File) == sizeof(Line)) {
			if (!memcmp(Line, "-- ROOT --\n", sizeof(Line))) {
				fclose(File);
				*End = 0;
				return FileName;
			}
		}
		fclose(File);
	}
	while (--End > FileName) {
		if (*End == '/') {
			strcpy(End, SystemName);
			goto loop;
		}
	}
	return 0;
}

static ml_value_t *print(void *Data, int Count, ml_value_t **Args) {
	ml_value_t *StringMethod = ml_method("string");
	for (int I = 0; I < Count; ++I) {
		ml_value_t *Result = Args[I];
		if (Result->Type != StringT) {
			Result = ml_call(StringMethod, 1, &Result);
			if (Result->Type == ErrorT) return Result;
			if (Result->Type != StringT) return ml_error("ResultError", "string method did not return string");
		}
		fputs(ml_string_value(Result), stdout);
	}
	fflush(stdout);
	return Nil;
}

static ml_value_t *ml_getenv(void *Data, int Count, ml_value_t **Args) {
	const char *Key = ml_string_value(Args[0]);
	const char *Value = getenv(Key);
	if (Value) {
		return ml_string(Value, -1);
	} else {
		return Nil;
	}
}

static ml_value_t *ml_setenv(void *Data, int Count, ml_value_t **Args) {
	const char *Key = ml_string_value(Args[0]);
	const char *Value = ml_string_value(Args[1]);
	setenv(Key, Value, 1);
	return Nil;
}

static void rabs_dump_func(void *Ptr, int Data) {
	void *Base = GC_base(Ptr);
	printf("%d @ %s:%d\n", GC_size(Ptr), ((const char **)Base)[0], ((int *)Base)[1]);
}

static void *rabs_oom_func(size_t Size) {
	GC_dump();
	GC_apply_to_all_blocks(rabs_dump_func, 0);
	return 0;
}

int main(int Argc, const char **Argv) {
	GC_INIT();
	GC_set_oom_fn(rabs_oom_func);
	GC_set_max_heap_size(67108864);
	ml_init();
	AppendMethod = ml_method("append");
	stringmap_insert(Globals, "vmount", ml_function(0, vmount));
	stringmap_insert(Globals, "subdir", ml_function(0, subdir));
	stringmap_insert(Globals, "file", ml_function(0, target_file_new));
	stringmap_insert(Globals, "meta", ml_function(0, target_meta_new));
	stringmap_insert(Globals, "expr", ml_function(0, target_expr_new));
	stringmap_insert(Globals, "include", ml_function(0, include));
	stringmap_insert(Globals, "context", ml_function(0, context));
	stringmap_insert(Globals, "execute", ml_function(0, execute));
	stringmap_insert(Globals, "shell", ml_function(0, shell));
	stringmap_insert(Globals, "mkdir", ml_function(0, rabs_mkdir));
	stringmap_insert(Globals, "scope", ml_function(0, scope));
	stringmap_insert(Globals, "print", ml_function(0, print));
	stringmap_insert(Globals, "open", ml_function(0, ml_file_open));
	stringmap_insert(Globals, "getenv", ml_function(0, ml_getenv));
	stringmap_insert(Globals, "setenv", ml_function(0, ml_setenv));

	const char *TargetName = 0;
	int QueryOnly = 0;
	int ListTargets = 0;
	int NumThreads = 1;
	for (int I = 1; I < Argc; ++I) {
		if (Argv[I][0] == '-') {
			switch (Argv[I][1]) {
			case 'D': {
				char *Define = concat(Argv[I] + 2, 0);
				char *Equals = strchr(Define, '=');
				if (Equals) {
					*Equals = 0;
					stringmap_insert(Globals, Define, ml_string(Equals + 1, -1));
				} else {
					stringmap_insert(Globals, Define, ml_integer(1));
				}
				break;
			};
			case 'q': {
				QueryOnly = 1;
				break;
			}
			case 'l': {
				ListTargets = 1;
				break;
			}
			case 'p': {
				if (Argv[I][2]) {
					NumThreads = atoi(Argv[I] + 2);
				} else {
					NumThreads = atoi(Argv[++I]);
				}
			}
			case 't': {
				GC_disable();
			}
			};
		} else {
			TargetName = Argv[I];
		};
	};
	target_threads_start(NumThreads);

	vfs_init();
	target_init();
	context_init();
	ml_file_init();
#ifdef LINUX
	const char *Path = get_current_dir_name();
#else
	char *Path = snew(1024);
	getcwd(Path, 1024);
#endif
	RootPath = find_root(Path);
	if (!RootPath) {
		puts("\e[31mError: could not find project root\e[0m");
	} else {
		printf("RootPath = %s, Path = %s\n", RootPath, Path);
		cache_open(RootPath);
		context_push("");
		context_symb_set(CurrentContext, "VERSION", ml_integer(CurrentVersion));
		load_file(concat(RootPath, SystemName, 0));
		target_t *Target;
		if (TargetName) {
			Target = target_get(TargetName);
			if (!Target) {
				printf("\e[31mError: invalid target %s\e[0m", TargetName);
				exit(1);
			}
		} else {
			context_t *Context = context_find(match_prefix(Path, RootPath));
			if (!Context) {
				printf("\e[31mError: current directory is not in project\e[0m");
				exit(1);
			}
			Target = Context->Default;
		}
		if (ListTargets) {
			target_list();
		} else if (QueryOnly) {
			target_query(Target);
		} else {
			target_update(Target);
			target_threads_wait(NumThreads);
		}
	}
	return 0;
}
