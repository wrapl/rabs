
#define _GNU_SOURCE
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

#include <libHX/io.h>

const char *SystemName = "/_minibuild_";
const char *RootPath = 0;
ml_value_t *AppendMethod;

static void load_file(const char *FileName) {
	printf("Loading: %s\n", FileName);
	ml_value_t *Closure = ml_load(FileName);
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

ml_value_t *subdir(void *Data, int Count, ml_value_t **Args) {
	const char *Path = ml_string_value(Args[0]);
	Path = concat(CurrentContext->Path, "/", Path, 0);
	//printf("Path = %s\n", Path);
	HX_mkdir(concat(RootPath, Path, 0), 0777);
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
	clock_t Start = clock();
	printf("\e[34m%s\e[0m\n", Command);
	if (system(Command)) {
		return ml_error("ExecuteError", "process returned non-zero exit code");
	} else {
		clock_t End = clock();
		printf("\t\e[34m%f seconds.\e[0m\n", ((double)(End - Start)) / CLOCKS_PER_SEC);
		return Nil;
	}
}

ml_value_t *shell(void *Data, int Count, ml_value_t **Args) {
	ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
	for (int I = 0; I < Count; ++I) if (ml_inline(AppendMethod, 2, Buffer, Args[I]) != Nil) ml_stringbuffer_add(Buffer, " ", 1);
	const char *Command = ml_stringbuffer_get(Buffer);
	printf("\e[34m%s\e[0m\n", Command);
	clock_t Start = clock();
	FILE *File = popen(Command, "r");
	char Chars[120];
	while (!feof(File)) {
		ssize_t Count = fread(Chars, 1, 120, File);
		if (Count > 0) ml_stringbuffer_add(Buffer, Chars, Count);
	}
	pclose(File);
	clock_t End = clock();
	printf("\t\e[34m%f seconds.\e[0m\n", ((double)(End - Start)) / CLOCKS_PER_SEC);
	size_t Length = Buffer->Length;
	return ml_string(ml_stringbuffer_get(Buffer), Length);
}

ml_value_t *rabs_mkdir(void *Data, int Count, ml_value_t **Args) {
	ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
	for (int I = 0; I < Count; ++I) ml_inline(AppendMethod, 2, Buffer, Args[I]);
	const char *Path = ml_stringbuffer_get(Buffer);
	if (HX_mkdir(Path, 0777) < 0) {
		return ml_error("OSError", "failed to create directory");
	}
	return Nil;
}

static const char *find_root(const char *Path) {
	char *FileName = (char *)GC_malloc(strlen(Path) + strlen(SystemName) + 1);
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

int main(int Argc, const char **Argv) {
	ml_init(rabs_ml_global);
	AppendMethod = ml_method("append");
	stringmap_insert(Globals, "vmount", ml_function(0, vmount));
	stringmap_insert(Globals, "subdir", ml_function(0, subdir));
	stringmap_insert(Globals, "file", ml_function(0, target_file_new));
	stringmap_insert(Globals, "meta", ml_function(0, target_meta_new));
	stringmap_insert(Globals, "include", ml_function(0, include));
	stringmap_insert(Globals, "context", ml_function(0, context));
	stringmap_insert(Globals, "execute", ml_function(0, execute));
	stringmap_insert(Globals, "shell", ml_function(0, shell));
	stringmap_insert(Globals, "mkdir", ml_function(0, rabs_mkdir));
	stringmap_insert(Globals, "scope", ml_function(0, scope));
	stringmap_insert(Globals, "print", ml_function(0, print));
	stringmap_insert(Globals, "open", ml_function(0, ml_file_open));

	const char *TargetName = 0;
	int QueryOnly = 0;
	int ListTargets = 0;
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
			};
		} else {
			TargetName = Argv[I];
		};
	};

	vfs_init();
	target_init();
	context_init();
	ml_file_init();
#ifdef LINUX
	const char *Path = get_current_dir_name();
#else
	char *Path = (char *)GC_malloc_atomic(1024);
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
		}
	}
	return 0;
}
