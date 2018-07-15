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
#include "targetwatch.h"
#include "context.h"
#include "util.h"
#include "cache.h"
#include "minilang.h"
#include "ml_file.h"
#include "ml_console.h"
#include "rabs.h"
#include "minilang/stringmap.h"

#define VERSION_STRING "1.0.3"

const char *SystemName = "/_minibuild_";
const char *RootPath = 0;
ml_value_t *AppendMethod;
static int EchoCommands = 0;

static stringmap_t Globals[1] = {STRINGMAP_INIT};
static stringmap_t Defines[1] = {STRINGMAP_INIT};
static int SavedArgc;
static char **SavedArgv;

static ml_value_t *rabs_ml_get(void *Data, const char *Name) {
	ml_value_t *Value = context_symb_get(CurrentContext, Name);
	if (Value) {
		target_t *Target = target_symb_new(Name);
		target_depends_auto(Target);
		target_update(Target);
		return Value;
	} else {
		return stringmap_search(Globals, Name) ?: ml_error("NameError", "%s undefined", Name);
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
		Value = ml_property(Data, Name, rabs_ml_get, rabs_ml_set, NULL, NULL);
		stringmap_insert(Cache, Name, Value);
	}
	return Value;
}

static void load_file(const char *FileName) {
	if (MonitorFiles) targetwatch_add(FileName, (target_t *)-1);
	ml_value_t *Closure = ml_load(rabs_ml_global, NULL, FileName);
	if (Closure->Type == MLErrorT) {
		printf("\e[31mError: %s\n\e[0m", ml_error_message(Closure));
		const char *Source;
		int Line;
		for (int I = 0; ml_error_trace(Closure, I, &Source, &Line); ++I) printf("\e[31m\t%s:%d\n\e[0m", Source, Line);
		exit(1);
	}
	ml_value_t *Result = ml_call(Closure, 0, NULL);
	if (Result->Type == MLErrorT) {
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
	ML_CHECK_ARG_COUNT(1);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	const char *Path = ml_string_value(Args[0]);
	Path = concat(CurrentContext->Path, "/", Path, NULL);
	//printf("Path = %s\n", Path);
	mkdir_p(concat(RootPath, Path, NULL));
	const char *FileName = concat(RootPath, Path, SystemName, NULL);
	//printf("FileName = %s\n", FileName);
	FileName = vfs_resolve(CurrentContext->Mounts, FileName);
	target_t *ParentDefault = CurrentContext->Default;
	context_push(Path);
	target_depends_add(ParentDefault, CurrentContext->Default);
	load_file(FileName);
	context_pop();
	return MLNil;
}

ml_value_t *scope(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	const char *Name = ml_string_value(Args[0]);
	context_scope(Name);
	ml_value_t *Result = ml_call(Args[1], 0, NULL);
	if (Result->Type == MLErrorT) {
		printf("Error: %s\n", ml_error_message(Result));
		const char *Source;
		int Line;
		for (int I = 0; ml_error_trace(Result, I, &Source, &Line); ++I) printf("\e[31m\t%s:%d\n\e[0m", Source, Line);
		exit(1);
	}
	context_pop();
	return MLNil;
}

ml_value_t *include(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
	for (int I = 0; I < Count; ++I) {
		ml_value_t *Result = ml_inline(AppendMethod, 2, Buffer, Args[I]);
		if (Result->Type == MLErrorT) return Result;
	}
	char *FileName = ml_stringbuffer_get(Buffer);
	/*if (FileName[0] != '/') {
		FileName = concat(RootPath, CurrentContext->Path, "/", FileName, NULL);
		FileName = vfs_resolve(CurrentContext->Mounts, FileName);
	}*/
	load_file(FileName);
	return MLNil;
}

ml_value_t *vmount(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(2);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	ML_CHECK_ARG_TYPE(1, MLStringT);
	const char *Path = ml_string_value(Args[0]);
	const char *Target = ml_string_value(Args[1]);
	CurrentContext->Mounts = vfs_mount(CurrentContext->Mounts,
		concat(CurrentContext->Path, "/", Path, NULL),
		concat(CurrentContext->Path, "/", Target, NULL),
		Target[0] == '/'
	);
	return MLNil;
}

ml_value_t *context(void *Data, int Count, ml_value_t **Args) {
	return ml_string(CurrentContext->Path, -1);
}

ml_value_t *execute(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
	for (int I = 0; I < Count; ++I) {
		ml_value_t *Result = ml_inline(AppendMethod, 2, Buffer, Args[I]);
		if (Result->Type == MLErrorT) return Result;
		if (Result != MLNil) ml_stringbuffer_add(Buffer, " ", 1);
	}
	const char *Command = ml_stringbuffer_get(Buffer);
	if (EchoCommands) printf("\e[34m%s: %s\e[0m\n", CurrentContext->FullPath, Command);
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
	if (EchoCommands) printf("\t\e[34m%f seconds.\e[0m\n", ((double)(End - Start)) / CLOCKS_PER_SEC);
	if (WIFEXITED(Result)) {
		if (WEXITSTATUS(Result) != 0) {
			return ml_error("ExecuteError", "process returned non-zero exit code");
		} else {
			return MLNil;
		}
	} else {
		return ml_error("ExecuteError", "process exited abnormally");
	}
}

ml_value_t *shell(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
	for (int I = 0; I < Count; ++I) {
		ml_value_t *Result = ml_inline(AppendMethod, 2, Buffer, Args[I]);
		if (Result->Type == MLErrorT) return Result;
		if (Result != MLNil) ml_stringbuffer_add(Buffer, " ", 1);
	}
	const char *Command = ml_stringbuffer_get(Buffer);
	if (EchoCommands) printf("\e[34m%s\e[0m\n", Command);
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
	if (EchoCommands) printf("\t\e[34m%f seconds.\e[0m\n", ((double)(End - Start)) / CLOCKS_PER_SEC);
	if (WIFEXITED(Result)) {
		if (WEXITSTATUS(Result) != 0) {
			return ml_error("ExecuteError", "process returned non-zero exit code");
		} else {
			size_t Length = Buffer->Length;
			ml_value_t *Result = ml_string(ml_stringbuffer_get(Buffer), Length);
			return Result;
		}
	} else {
		return ml_error("ExecuteError", "process exited abnormally");
	}
}

ml_value_t *rabs_mkdir(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
	for (int I = 0; I < Count; ++I) {
		ml_value_t *Result = ml_inline(AppendMethod, 2, Buffer, Args[I]);
		if (Result->Type == MLErrorT) return Result;
	}
	char *Path = ml_stringbuffer_get(Buffer);
	if (mkdir_p(Path) < 0) {
		return ml_error("FileError", "error creating directory %s", Path);
	}
	return MLNil;
}

ml_value_t *rabs_open(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(2);
	ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
	ml_value_t *Result = ml_inline(AppendMethod, 2, Buffer, Args[0]);
	if (Result->Type == MLErrorT) return Result;
	char *FileName = ml_stringbuffer_get(Buffer);
	ml_value_t *Args2[] = {ml_string(FileName, -1), Args[1]};
	return ml_file_open(0, 2, Args2);
}

static const char *find_root(const char *Path) {
	char *FileName = snew(strlen(Path) + strlen(SystemName) + 1);
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
		if (Result->Type != MLStringT) {
			Result = ml_call(StringMethod, 1, &Result);
			if (Result->Type == MLErrorT) return Result;
			if (Result->Type != MLStringT) return ml_error("ResultError", "string method did not return string");
		}
		fwrite(ml_string_value(Result), 1, ml_string_length(Result), stdout);
	}
	fflush(stdout);
	return MLNil;
}

static ml_value_t *ml_getenv(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	const char *Key = ml_string_value(Args[0]);
	const char *Value = getenv(Key);
	if (Value) {
		return ml_string(Value, -1);
	} else {
		return MLNil;
	}
}

static ml_value_t *ml_setenv(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(2);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	ML_CHECK_ARG_TYPE(1, MLStringT);
	const char *Key = ml_string_value(Args[0]);
	const char *Value = ml_string_value(Args[1]);
	setenv(Key, Value, 1);
	return MLNil;
}

static ml_value_t *defined(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	const char *Key = ml_string_value(Args[0]);
	return stringmap_search(Defines, Key) ?: MLNil;
}

static ml_value_t *debug(void *Data, int Count, ml_value_t **Args) {
	asm("int3");
	return MLNil;
}

void restart() {
	cache_close();
	execv(SavedArgv[0], SavedArgv);
}

int main(int Argc, char **Argv) {
	SavedArgc = Argc;
	SavedArgv = Argv;
	GC_INIT();
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
	stringmap_insert(Globals, "open", ml_function(0, rabs_open));
	stringmap_insert(Globals, "getenv", ml_function(0, ml_getenv));
	stringmap_insert(Globals, "setenv", ml_function(0, ml_setenv));
	stringmap_insert(Globals, "defined", ml_function(0, defined));
	stringmap_insert(Globals, "debug", ml_function(0, debug));

	vfs_init();
	target_init();
	context_init();
	ml_file_init();

	const char *TargetName = 0;
	int QueryOnly = 0;
	int ListTargets = 0;
	int NumThreads = 1;
	int InteractiveMode = 0;
	for (int I = 1; I < Argc; ++I) {
		if (Argv[I][0] == '-') {
			switch (Argv[I][1]) {
			case 'h': {
				printf("Usage: %s { options } [ target ]\n", Argv[0]);
				puts("    -h              display this message");
				puts("    -v              print version and exit");
				puts("    -Dkey[=value]   add a define");
				puts("    -c              print shell commands");
				puts("    -p n            run n threads");
				puts("    -w              watch for file changes");
				exit(0);
			}
			case 'v': {
				printf("rabs version %s\n", VERSION_STRING);
				exit(0);
			}
			case 'D': {
				char *Define = concat(Argv[I] + 2, NULL);
				char *Equals = strchr(Define, '=');
				if (Equals) {
					*Equals = 0;
					stringmap_insert(Defines, Define, ml_string(Equals + 1, -1));
				} else {
					stringmap_insert(Defines, Define, ml_integer(1));
				}
				break;
			};
			case 'c': {
				EchoCommands = 1;
				break;
			}
			case 's': {
				StatusUpdates = 1;
				break;
			}
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
				break;
			}
			case 'i': {
				InteractiveMode = 1;
				break;
			}
			case 'w': {
				targetwatch_init();
				MonitorFiles = 1;
				break;
			}
			case 't': {
				GC_disable();
				break;
			}
			}
		} else {
			TargetName = Argv[I];
		}
	}

#ifdef LINUX
	const char *Path = get_current_dir_name();
#else
	char *Path = snew(1024);
	getcwd(Path, 1024);
#endif
	RootPath = find_root(Path);
	if (!RootPath) {
		puts("\e[31mError: could not find project root\e[0m");
		exit(1);
	}

	printf("RootPath = %s, Path = %s\n", RootPath, Path);
	cache_open(RootPath);

	context_push("");
	context_symb_set(CurrentContext, "VERSION", ml_integer(CurrentVersion));

	if (InteractiveMode || MonitorFiles) {

	} else {
		target_threads_start(NumThreads);
	}

	load_file(concat(RootPath, SystemName, NULL));
	target_t *Target;
	if (TargetName) {
		int HasPrefix = !strncmp(TargetName, "meta:", strlen("meta:"));
		HasPrefix |= !strncmp(TargetName, "file:", strlen("file:"));
		if (!HasPrefix) {
			TargetName = concat("meta:", match_prefix(Path, RootPath), "::", TargetName, NULL);
		}
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
		if (InteractiveMode) {
			target_interactive_start(NumThreads);
			ml_console(rabs_ml_global, Globals);
		} else if (MonitorFiles) {
			target_interactive_start(NumThreads);
			targetwatch_wait();
		}
	}
	return 0;
}
