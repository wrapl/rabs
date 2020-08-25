#include <gc/gc.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>
#include  <signal.h>
#include "target.h"
#include "context.h"
#include "util.h"
#include "cache.h"
#include "minilang.h"
#include "ml_object.h"
#include "ml_iterfns.h"
#include "ml_file.h"
#include "rabs.h"
#include "stringmap.h"
#include "library.h"
#include "ml_console.h"
#include "whereami.h"

#ifdef Linux
#include "targetwatch.h"
#endif

#ifndef Mingw
#include <sys/wait.h>
#endif

const char *SystemName = "build.rabs";
const char *RootPath;
ml_value_t *ArgifyMethod;
static int EchoCommands = 0;

static stringmap_t Globals[1] = {STRINGMAP_INIT};
static stringmap_t Defines[1] = {STRINGMAP_INIT};
static int SavedArgc;
static char **SavedArgv;

ml_value_t *rabs_global(const char *Name) {
	return stringmap_search(Globals, Name) ?: MLNil;
}

extern target_t *target_symb_new(context_t *Context, const char *Name);

typedef struct rabs_property_t {
	const ml_type_t *Type;
	const char *Name;
} rabs_property_t;

static ml_value_t *rabs_property_deref(rabs_property_t *Property) {
	ml_value_t *Value = context_symb_get(CurrentContext, Property->Name);
	if (Value) {
		if (CurrentTarget) {
			target_depends_auto(target_symb_new(CurrentContext, Property->Name));
		}
		return Value;
	} else {
		return stringmap_search(Globals, Property->Name) ?: MLNil; //ml_error("NameError", "%s undefined", Name);
	}
}

static ml_value_t *rabs_property_assign(rabs_property_t *Property, ml_value_t *Value) {
	context_symb_set(CurrentContext, Property->Name, Value);
	//target_symb_update(Name);
	return Value;
}

ML_TYPE(RabsPropertyT, MLAnyT, "property",
	.deref = (void *)rabs_property_deref,
	.assign = (void *)rabs_property_assign
);

static ml_value_t *rabs_ml_global(void *Data, const char *Name) {
	static stringmap_t Cache[1] = {STRINGMAP_INIT};
	ml_value_t **Slot = (ml_value_t **)stringmap_slot(Cache, Name);
	if (!Slot[0]) {
		rabs_property_t *Property = new(rabs_property_t);
		Property->Type = RabsPropertyT;
		Property->Name = Name;
		Slot[0] = (ml_value_t *)Property;
	}
	return Slot[0];
}

typedef struct preprocessor_node_t preprocessor_node_t;

struct preprocessor_node_t {
	preprocessor_node_t *Next;
	const char *FileName;
	FILE *File;
	ml_source_t Source;
};

typedef struct preprocessor_t {
	preprocessor_node_t *Nodes;
	mlc_scanner_t *Scanner;
} preprocessor_t;

static const char *preprocessor_read(preprocessor_t *Preprocessor) {
	preprocessor_node_t *Node = Preprocessor->Nodes;
	while (Node) {
		if (!Node->File) {
			Node->File = fopen(Node->FileName, "r");
			if (!Node->File) {
				ml_scanner_error(Preprocessor->Scanner, "LoadError", "error opening %s", Node->FileName);
			}
		}
		char *Line = NULL;
		size_t Length = 0;
		ssize_t Actual = getline(&Line, &Length, Node->File);
		if (Actual < 0) {
			free(Line);
			fclose(Node->File);
			Node = Preprocessor->Nodes = Node->Next;
			if (Node) ml_scanner_source(Preprocessor->Scanner, Node->Source);
		} else if (!strncmp(Line, "%include ", strlen("%include "))) {
			while (Line[Actual] <= ' ') Line[Actual--] = 0;
			const char *FileName = Line + strlen("%include ");
			if (FileName[0] != '/') {
				FileName = vfs_resolve(concat(CurrentContext->FullPath, "/", FileName, NULL));
			}
			free(Line);
			preprocessor_node_t *NewNode = new(preprocessor_node_t);
			NewNode->Next = Node;
			NewNode->FileName = FileName;
			Node->Source = ml_scanner_source(Preprocessor->Scanner, (ml_source_t){FileName, 0});
			++Node->Source.Line;
			Node = Preprocessor->Nodes = NewNode;
		} else if (Line[Actual - 1] != '\n') {
			char *NewLine = GC_MALLOC_ATOMIC(Actual + 1);
			memcpy(NewLine, Line, Actual);
			NewLine[Actual] = '\n';
			NewLine[Actual + 1] = 0;
			free(Line);
			return NewLine;
		} else {
			char *NewLine = GC_MALLOC_ATOMIC(Actual);
			memcpy(NewLine, Line, Actual);
			NewLine[Actual] = 0;
			free(Line);
			return NewLine;
		}
	}
	return NULL;
}

typedef struct {
	ml_state_t Base;
	ml_value_t *Result;
} load_file_state_t;

static void load_file_result(load_file_state_t *State, ml_value_t *Result) {
	State->Result = Result;
}

static void load_file_loaded(load_file_state_t *State, ml_value_t *Closure) {
	if (Closure->Type != MLErrorT) {
		State->Base.run = (void *)load_file_result;
		Closure->Type->call((ml_state_t *)State, Closure, 0, NULL);
	} else {
		State->Result = Closure;
	}
}

static ml_value_t *load_file(const char *FileName) {
#ifdef Linux
	if (WatchMode) targetwatch_add(FileName);
#endif
	preprocessor_node_t *Node = new(preprocessor_node_t);
	Node->FileName = FileName;
	preprocessor_t Preprocessor[1] = {{Node, NULL,}};
	Preprocessor->Scanner = ml_scanner(FileName, Preprocessor, (void *)preprocessor_read, (ml_getter_t)rabs_ml_global, NULL);

	load_file_state_t State[1];
	State->Base.run = (void *)load_file_loaded;
	State->Base.Context = &MLRootContext;
	State->Result = MLNil;
	ml_function_compile((ml_state_t *)State, Preprocessor->Scanner, NULL);
	return State->Result;
}

static ml_value_t *subdir(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	const char *Path = ml_string_value(Args[0]);
	Path = concat(CurrentContext->Path, "/", Path, NULL);
	//printf("Path = %s\n", Path);
	mkdir_p(concat(RootPath, Path, NULL));
	const char *FileName = concat(RootPath, Path, "/", SystemName, NULL);
	//printf("FileName = %s\n", FileName);
	FileName = vfs_resolve(FileName);
	target_t *ParentDefault = CurrentContext->Default;
	context_t *Context = context_push(Path);
	if (Count < 2 || Args[1] != MLNil) targetset_insert(ParentDefault->Depends, CurrentContext->Default);
	ml_value_t *Result = load_file(FileName);
	context_pop();
	if (Result->Type == MLErrorT) {
		return Result;
	} else {
		return (ml_value_t *)Context;
	}
}

static ml_value_t *scope(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(2);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	const char *Name = ml_string_value(Args[0]);
	context_scope(Name);
	ml_value_t *Result = ml_simple_call(Args[1], 0, NULL);
	context_pop();
	return Result;
}

static ml_value_t *symbol(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	return rabs_ml_global(NULL, ml_string_value(Args[0]));
}

#if defined(Darwin)
#define LIB_EXTENSION ".dylib"
#elif defined(Mingw)
#define LIB_EXTENSION ".dll"
#else
#define LIB_EXTENSION ".so"
#endif

#define MAX_EXTENSION 10

static ml_value_t *include(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
	for (int I = 0; I < Count; ++I) {
		ml_value_t *Result = ml_simple_inline(MLStringBufferAppendMethod, 2, Buffer, Args[I]);
		if (Result->Type == MLErrorT) return Result;
	}
	size_t Length = Buffer->Length;
	char *FileName0 = GC_MALLOC_ATOMIC(Length + MAX_EXTENSION);
	memcpy(FileName0, ml_stringbuffer_get(Buffer), Length);
	char *FileName = FileName0;
	struct stat Stat[1];

	strcpy(FileName0 + Length, ".rabs");
	if (FileName0[0] != '/') {
		FileName = vfs_resolve(concat(CurrentContext->FullPath, "/", FileName, NULL));
	}
	if (!stat(FileName, Stat)) return load_file(FileName);

	FileName = FileName0;
	strcpy(FileName0 + Length, LIB_EXTENSION);
	if (FileName0[0] != '/') {
		FileName = vfs_resolve(concat(CurrentContext->FullPath, "/", FileName, NULL));
	}
	if (!stat(FileName, Stat)) return library_load(FileName, Globals);

	return ml_error("IncludeError", "Unable to include: %s", FileName);
}

static ml_value_t *vmount(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(2);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	ML_CHECK_ARG_TYPE(1, MLStringT);
	const char *Path = ml_string_value(Args[0]);
	const char *Target = ml_string_value(Args[1]);
	vfs_mount(
		concat(CurrentContext->Path, "/", Path, NULL),
		concat(CurrentContext->Path, "/", Target, NULL),
		Target[0] == '/'
	);
	return MLNil;
}

static ml_value_t *context(void *Data, int Count, ml_value_t **Args) {
	if (Count > 0) {
		ML_CHECK_ARG_TYPE(0, MLStringT);
		return (ml_value_t *)context_find(ml_string_value(Args[0])) ?: MLNil;
	} else {
		return (ml_value_t *)CurrentContext;
	}
}

#ifdef __MINGW32__
#define WIFEXITED(Status) (((Status) & 0x7f) == 0)
#define WEXITSTATUS(Status) (((Status) & 0xff00) >> 8)
#endif

static ml_value_t *cmdify_nil(void *Data, int Count, ml_value_t **Args) {
	return Args[0];
}

static ml_value_t *cmdify_integer(void *Data, int Count, ml_value_t **Args) {
	ml_stringbuffer_t *Buffer = (ml_stringbuffer_t *)Args[0];
	ml_stringbuffer_addf(Buffer, "%ld", ml_integer_value(Args[1]));
	return Args[0];
}

static ml_value_t *cmdify_real(void *Data, int Count, ml_value_t **Args) {
	ml_stringbuffer_t *Buffer = (ml_stringbuffer_t *)Args[0];
	ml_stringbuffer_addf(Buffer, "%f", ml_real_value(Args[1]));
	return Args[0];
}

static ml_value_t *cmdify_string(void *Data, int Count, ml_value_t **Args) {
	ml_stringbuffer_t *Buffer = (ml_stringbuffer_t *)Args[0];
	ml_stringbuffer_add(Buffer, ml_string_value(Args[1]), ml_string_length(Args[1]));
	return Args[0];
}

static ml_value_t *cmdify_method(void *Data, int Count, ml_value_t **Args) {
	ml_stringbuffer_t *Buffer = (ml_stringbuffer_t *)Args[0];
	const char *Name = ml_method_name(Args[1]);
	ml_stringbuffer_add(Buffer, Name, strlen(Name));
	return Args[0];
}

static ml_value_t *cmdify_list(void *Data, int Count, ml_value_t **Args) {
	ml_stringbuffer_t *Buffer = (ml_stringbuffer_t *)Args[0];
	int Last = Buffer->Length;
	ML_LIST_FOREACH(Args[1], Node) {
		if (Buffer->Length > Last) ml_stringbuffer_add(Buffer, " ", 1);
		Last = Buffer->Length;
		ml_simple_inline(MLStringBufferAppendMethod, 2, Buffer, Node->Value);
	}
	return Args[0];
}

static ml_value_t *cmdify_map(void *Data, int Count, ml_value_t **Args) {
	ml_stringbuffer_t *Buffer = (ml_stringbuffer_t *)Args[0];
	int Last = Buffer->Length;
	ML_MAP_FOREACH(Args[1], Iter) {
		if (Buffer->Length > Last) ml_stringbuffer_add(Buffer, " ", 1);
		Last = Buffer->Length;
		ml_value_t *Result = ml_simple_inline(MLStringBufferAppendMethod, 2, Buffer, Iter->Key);
		if (ml_is_error(Result)) return Result;
		if (Iter->Value != MLNil && Iter->Value != MLSome) {
			ml_stringbuffer_add(Buffer, "=", 1);
			Result = ml_simple_inline(MLStringBufferAppendMethod, 2, Buffer, Iter->Value);
			if (ml_is_error(Result)) return Result;
		}
	}
	return Args[0];
}

static int ErrorLogFile = STDERR_FILENO;

static ml_value_t *command(int Capture, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
	int Last = Buffer->Length;
	for (int I = 0; I < Count; ++I) {
		if (Buffer->Length > Last) ml_stringbuffer_add(Buffer, " ", 1);
		Last = Buffer->Length;
		ml_value_t *Result = ml_simple_inline(MLStringBufferAppendMethod, 2, Buffer, Args[I]);
		if (Result->Type == MLErrorT) return Result;
	}
	const char *Command = ml_stringbuffer_get(Buffer);
	if (EchoCommands) printf("\e[34m%s: %s\e[0m\n", CurrentDirectory, Command);
	if (DebugThreads && CurrentThread) {
		strncpy(CurrentThread->Command, Command, sizeof(CurrentThread->Command) - 1);
		display_threads();
	}
	clock_t Start = clock();
	const char *WorkingDirectory = CurrentDirectory;
	int Pipe[2];
	if (pipe(Pipe) == -1) return ml_error("PipeError", "failed to create pipe");
	pid_t Child = fork();
	if (!Child) {
		setpgid(0, 0);
		if (chdir(WorkingDirectory)) exit(-1);
		close(Pipe[0]);
		dup2(Pipe[1], STDOUT_FILENO);
		dup2(ErrorLogFile, STDERR_FILENO);
		execl("/bin/sh", "sh", "-c", Command, NULL);
		exit(-1);
	}
	close(Pipe[1]);
	ml_value_t *Result = MLNil;
	if (CurrentThread) CurrentThread->Child = Child;
	pthread_mutex_unlock(InterpreterLock);
	if (Capture) {
		ml_stringbuffer_t Output[1] = {ML_STRINGBUFFER_INIT};
		char Chars[ML_STRINGBUFFER_NODE_SIZE];
		for (;;) {
			ssize_t Size = read(Pipe[0], Chars, ML_STRINGBUFFER_NODE_SIZE);
			if (Size <= 0) break;
			//pthread_mutex_lock(GlobalLock);
			if (Size > 0) ml_stringbuffer_add(Output, Chars, Size);
			//pthread_mutex_unlock(GlobalLock);
		}
		Result = ml_stringbuffer_value(Output);
	} else {
		char Chars[ML_STRINGBUFFER_NODE_SIZE];
		for (;;) {
			ssize_t Size = read(Pipe[0], Chars, ML_STRINGBUFFER_NODE_SIZE);
			if (Size <= 0) break;
		}
	}
	close(Pipe[0]);
	int Status;
	if (waitpid(Child, &Status, 0) == -1) {
		Result = ml_error("WaitError", "error waiting for child process");
	}
	clock_t End = clock();
	pthread_mutex_lock(InterpreterLock);
	if (CurrentThread) CurrentThread->Child = 0;
	if (EchoCommands) printf("\t\e[33m%f seconds.\e[0m\n", ((double)(End - Start)) / CLOCKS_PER_SEC);
	if (WIFEXITED(Status)) {
		if (WEXITSTATUS(Status) != 0) {
			return ml_error("ExecuteError", "process returned non-zero exit code");
		} else {
			return Result;
		}
	} else {
		return ml_error("ExecuteError", "process exited abnormally");
	}
}

static ml_value_t *execute(void *Data, int Count, ml_value_t **Args) {
	return command(0, Count, Args);
}

static ml_value_t *shell(void *Data, int Count, ml_value_t **Args) {
	return command(1, Count, Args);
}

static ml_value_t *argify_nil(void *Data, int Count, ml_value_t **Args) {
	return Args[0];
}

static ml_value_t *argify_integer(void *Data, int Count, ml_value_t **Args) {
	char *Chars;
	size_t Length = asprintf(&Chars, "%ld", ml_integer_value(Args[1]));
	ml_list_append(Args[0], ml_string(Chars, Length));
	return Args[0];
}

static ml_value_t *argify_real(void *Data, int Count, ml_value_t **Args) {
	char *Chars;
	size_t Length = asprintf(&Chars, "%f", ml_real_value(Args[1]));
	ml_list_append(Args[0], ml_string(Chars, Length));
	return Args[0];
}

static ml_value_t *argify_string(void *Data, int Count, ml_value_t **Args) {
	ml_list_append(Args[0], Args[1]);
	return Args[0];
}

static ml_value_t *argify_method(void *Data, int Count, ml_value_t **Args) {
	ml_list_append(Args[0], ml_string(ml_method_name(Args[1]), -1));
	return Args[0];
}

static ml_value_t *argify_list(void *Data, int Count, ml_value_t **Args) {
	ML_LIST_FOREACH(Args[1], Node) {
		ml_value_t *Result = ml_simple_inline(ArgifyMethod, 2, Args[0], Node->Value);
		if (ml_is_error(Result)) return Result;
	}
	return Args[0];
}

static ml_value_t *argify_map(void *Data, int Count, ml_value_t **Args) {
	ML_MAP_FOREACH(Args[1], Iter) {
		ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
		ml_value_t *Result = ml_simple_inline(MLStringBufferAppendMethod, 2, Buffer, Iter->Key);
		if (ml_is_error(Result)) return Result;
		if (Iter->Value != MLNil && Iter->Value != MLSome) {
			ml_stringbuffer_add(Buffer, "=", 1);
			Result = ml_simple_inline(MLStringBufferAppendMethod, 2, Buffer, Iter->Value);
			if (ml_is_error(Result)) return Result;
		}
		ml_list_append(Args[0], ml_stringbuffer_value(Buffer));
	}
	return Args[0];
}

#ifdef Mingw
#define rabs_execv execute
#define rabs_shellv shell
#else
static ml_value_t *rabs_execv(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ml_value_t *ArgList = ml_list();
	for (int I = 0; I < Count; ++I) {
		ml_value_t *Result = ml_simple_inline(ArgifyMethod, 2, ArgList, Args[I]);
		if (Result->Type == MLErrorT) return Result;
	}
	int Argc = ml_list_length(ArgList);
	const char *Argv[Argc + 1];
	const char **Argp = Argv;
	ML_LIST_FOREACH(ArgList, Node) {
		*Argp = ml_string_value(Node->Value);
		++Argp;
	}
	*Argp = NULL;
	const char *WorkingDirectory = CurrentDirectory;
	if (EchoCommands) {
		printf("\e[34m%s:", WorkingDirectory);
		for (int I = 0; I < Argc; ++I) printf(" %s", Argv[I]);
		printf("\e[0m\n");
	}
	clock_t Start = clock();
	pid_t Child = fork();
	if (!Child) {
		if (chdir(WorkingDirectory)) exit(-1);
		int DevNull = open("/dev/null", O_WRONLY | O_CREAT, 0666);
		dup2(DevNull, STDOUT_FILENO);
		close(DevNull);
		if (execvp(Argv[0], (char * const *)Argv) == -1) exit(-1);
	}
	pthread_mutex_unlock(InterpreterLock);
	int Status;
	if (waitpid(Child, &Status, 0) == -1) return ml_error("WaitError", "error waiting for child process");
	pthread_mutex_lock(InterpreterLock);
	if (EchoCommands) {
		clock_t End = clock();
		printf("\t\e[33m%f seconds.\e[0m\n", ((double)(End - Start)) / CLOCKS_PER_SEC);
	}
	if (WIFEXITED(Status)) {
		if (WEXITSTATUS(Status) != 0) {
			return ml_error("ExecuteError", "process returned non-zero exit code");
		} else {
			return MLNil;
		}
	} else {
		return ml_error("ExecuteError", "process exited abnormally");
	}
}

static ml_value_t *rabs_shellv(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ml_value_t *ArgList = ml_list();
	for (int I = 0; I < Count; ++I) {
		ml_value_t *Result = ml_simple_inline(ArgifyMethod, 2, ArgList, Args[I]);
		if (Result->Type == MLErrorT) return Result;
	}
	int Argc = ml_list_length(ArgList);
	const char *Argv[Argc + 1];
	const char **Argp = Argv;
	ML_LIST_FOREACH(ArgList, Node) {
		*Argp = ml_string_value(Node->Value);
		++Argp;
	}
	*Argp = NULL;
	const char *WorkingDirectory = CurrentDirectory;
	if (EchoCommands) {
		printf("\e[34m%s:", WorkingDirectory);
		for (int I = 0; I < Argc; ++I) printf(" %s", Argv[I]);
		printf("\e[0m\n");
	}
	clock_t Start = clock();
	int Pipe[2];
	if (pipe(Pipe) == -1) return ml_error("PipeError", "failed to create pipe");
	pid_t Child = fork();
	if (!Child) {
		if (chdir(WorkingDirectory)) exit(-1);
		close(Pipe[0]);
		dup2(Pipe[1], STDOUT_FILENO);
		if (execvp(Argv[0], (char * const *)Argv) == -1) exit(-1);
	}
	close(Pipe[1]);
	ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
	pthread_mutex_unlock(InterpreterLock);
	char Chars[ML_STRINGBUFFER_NODE_SIZE];
	for (;;) {
		ssize_t Size = read(Pipe[0], Chars, ML_STRINGBUFFER_NODE_SIZE);
		if (Size <= 0) break;
		//pthread_mutex_lock(GlobalLock);
		if (Size > 0) ml_stringbuffer_add(Buffer, Chars, Size);
		//pthread_mutex_unlock(GlobalLock);
	}
	int Status;
	if (waitpid(Child, &Status, 0) == -1) return ml_error("WaitError", "error waiting for child process");
	pthread_mutex_lock(InterpreterLock);
	if (EchoCommands) {
		clock_t End = clock();
		printf("\t\e[33m%f seconds.\e[0m\n", ((double)(End - Start)) / CLOCKS_PER_SEC);
	}
	if (WIFEXITED(Status)) {
		if (WEXITSTATUS(Status) != 0) {
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
#endif

static ml_value_t *rabs_mkdir(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
	for (int I = 0; I < Count; ++I) {
		ml_value_t *Result = ml_simple_inline(MLStringBufferAppendMethod, 2, Buffer, Args[I]);
		if (Result->Type == MLErrorT) return Result;
	}
	char *Path = ml_stringbuffer_get(Buffer);
	if (mkdir_p(Path) < 0) {
		return ml_error("FileError", "error creating directory %s", Path);
	}
	return MLNil;
}

static ml_value_t *rabs_chdir(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
	for (int I = 0; I < Count; ++I) {
		ml_value_t *Result = ml_simple_inline(MLStringBufferAppendMethod, 2, Buffer, Args[I]);
		if (Result->Type == MLErrorT) return Result;
	}
	if (CurrentDirectory) GC_free((void *)CurrentDirectory);
	CurrentDirectory = ml_stringbuffer_get_uncollectable(Buffer);
	return Args[0];
}

static ml_value_t *rabs_open(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(2);
	ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
	ml_value_t *Result = ml_simple_inline(MLStringBufferAppendMethod, 2, Buffer, Args[0]);
	if (Result->Type == MLErrorT) return Result;
	char *FileName = ml_stringbuffer_get(Buffer);
	return ml_simple_inline((ml_value_t *)MLFileOpen, 2, ml_string(FileName, -1), Args[1]);
}

static const char *find_root(const char *Path) {
	char *FileName = snew(strlen(Path) + strlen(SystemName) + 2);
	char *End = stpcpy(FileName, Path);
	End[0] = '/';
	strcpy(End + 1, SystemName);
	char Line[strlen(":< ROOT >:\n")];
	FILE *File = NULL;
loop:
	File = fopen(FileName, "r");
	if (File) {
		if (fread(Line, 1, sizeof(Line), File) == sizeof(Line)) {
			if (!memcmp(Line, ":< ROOT >:\n", sizeof(Line))) {
				fclose(File);
				*End = 0;
				return FileName;
			}
			if (!memcmp(Line, "-- ROOT --\n", sizeof(Line))) {
				fclose(File);
				printf("Version error: Build scripts were written for an older version of Rabs:\n\tLine comments must be changed from -- to :>\n\tThe root build.rabs file should start with :< ROOT >:\n");
				exit(1);
			}
		}
		fclose(File);
	}
	while (--End > FileName) {
		if (*End == '/') {
			strcpy(End + 1, SystemName);
			goto loop;
		}
	}
	return NULL;
}

static int ml_stringbuffer_print(FILE *File, const char *String, size_t Length) {
	fwrite(String, 1, Length, File);
	return 0;
}

static ml_value_t *print(void *Data, int Count, ml_value_t **Args) {
	ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
	for (int I = 0; I < Count; ++I) {
		ml_value_t *Result = ml_simple_inline(MLStringBufferAppendMethod, 2, Buffer, Args[I]);
		if (ml_is_error(Result)) return Result;
	}
	ml_stringbuffer_foreach(Buffer, stdout, (void *)ml_stringbuffer_print);
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
#ifdef __MINGW32__
	char *Buffer;
	asprintf(&Buffer, "%s=%s", Key, Value);
	putenv(Buffer);
#else
	setenv(Key, Value, 1);
#endif
	return MLNil;
}

static ml_value_t *ml_target_find(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	const char *Id = ml_string_value(Args[0]);
	target_t *Target = target_find(Id);
	if (!Target) return ml_error("ValueError", "Target %s not found", Id);
	return (ml_value_t *)Target;
}

static ml_value_t *defined(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	const char *Key = ml_string_value(Args[0]);
	return stringmap_search(Defines, Key) ?: MLNil;
}

static ml_value_t *type(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	return ml_string(Args[0]->Type->Name, -1);
}

static ml_value_t *error(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(2);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	ML_CHECK_ARG_TYPE(1, MLStringT);
	return ml_error(ml_string_value(Args[0]), "%s", ml_string_value(Args[1]));
}

static ml_value_t *lib_path(void) {
	int ExecutablePathLength = wai_getExecutablePath(NULL, 0, NULL);
	char *ExecutablePath = GC_MALLOC_ATOMIC(ExecutablePathLength + 1);
	wai_getExecutablePath(ExecutablePath, ExecutablePathLength + 1, &ExecutablePathLength);
	ExecutablePath[ExecutablePathLength] = 0;
	for (int I = ExecutablePathLength - 1; I > 0; --I) {
		if (ExecutablePath[I] == '/') {
			ExecutablePath[I] = 0;
			ExecutablePathLength = I;
			break;
		}
	}
	int LibPathLength = ExecutablePathLength + strlen("/lib/rabs");
	char *LibPath = GC_MALLOC_ATOMIC(LibPathLength + 1);
	memcpy(LibPath, ExecutablePath, ExecutablePathLength);
	strcpy(LibPath + ExecutablePathLength, "/lib/rabs");
	printf("Looking for library path at %s\n", LibPath);
	struct stat Stat[1];
	if (lstat(LibPath, Stat) || !S_ISDIR(Stat->st_mode)) {
		return MLNil;
	} else {
		return ml_string(LibPath, LibPathLength);
	}
}

static void restart(void) {
	cache_close();
	execv("/proc/self/exe", SavedArgv);
}

int main(int Argc, char **Argv) {
	CurrentDirectory = "<random>";
	SavedArgc = Argc;
	SavedArgv = Argv;
	GC_INIT();
	ml_init();
	ml_types_init(Globals);
	ml_object_init(Globals);
	ml_iterfns_init(Globals);
	ml_file_init(Globals);
	stringmap_insert(Globals, "vmount", ml_cfunction(NULL, vmount));
	stringmap_insert(Globals, "subdir", ml_cfunction(NULL, subdir));
	stringmap_insert(Globals, "target", ml_cfunction(NULL, ml_target_find));
	stringmap_insert(Globals, "file", ml_cfunction(NULL, target_file_new));
	stringmap_insert(Globals, "meta", ml_cfunction(NULL, target_meta_new));
	stringmap_insert(Globals, "expr", ml_cfunction(NULL, target_expr_new));
	// TODO: add functions to register and create udf targets
	stringmap_insert(Globals, "symbol", ml_cfunction(NULL, symbol));
	stringmap_insert(Globals, "include", ml_cfunction(NULL, include));
	stringmap_insert(Globals, "context", ml_cfunction(NULL, context));
	stringmap_insert(Globals, "execute", ml_cfunction(NULL, execute));
	stringmap_insert(Globals, "shell", ml_cfunction(NULL, shell));
	stringmap_insert(Globals, "execv", ml_cfunction(NULL, rabs_execv));
	stringmap_insert(Globals, "shellv", ml_cfunction(NULL, rabs_shellv));
	stringmap_insert(Globals, "mkdir", ml_cfunction(NULL, rabs_mkdir));
	stringmap_insert(Globals, "chdir", ml_cfunction(NULL, rabs_chdir));
	stringmap_insert(Globals, "scope", ml_cfunction(NULL, scope));
	stringmap_insert(Globals, "print", ml_cfunction(NULL, print));
	stringmap_insert(Globals, "open", ml_cfunction(NULL, rabs_open));
	stringmap_insert(Globals, "getenv", ml_cfunction(NULL, ml_getenv));
	stringmap_insert(Globals, "setenv", ml_cfunction(NULL, ml_setenv));
	stringmap_insert(Globals, "defined", ml_cfunction(NULL, defined));
	stringmap_insert(Globals, "check", ml_cfunction(NULL, target_depends_auto_value));
	stringmap_insert(Globals, "type", ml_cfunction(NULL, type));
	stringmap_insert(Globals, "error", ml_cfunction(NULL, error));
	stringmap_insert(Globals, "LIBPATH", lib_path());

	ArgifyMethod = ml_method("argify");
	ml_method_by_value(ArgifyMethod, NULL, argify_nil, MLListT, MLNilT, NULL);
	ml_method_by_value(ArgifyMethod, NULL, argify_integer, MLListT, MLIntegerT, NULL);
	ml_method_by_value(ArgifyMethod, NULL, argify_real, MLListT, MLRealT, NULL);
	ml_method_by_value(ArgifyMethod, NULL, argify_string, MLListT, MLStringT, NULL);
	ml_method_by_value(ArgifyMethod, NULL, argify_method, MLListT, MLMethodT, NULL);
	ml_method_by_value(ArgifyMethod, NULL, argify_list, MLListT, MLListT, NULL);
	ml_method_by_value(ArgifyMethod, NULL, argify_map, MLListT, MLMapT, NULL);

	ml_method_by_value(MLStringBufferAppendMethod, NULL, cmdify_nil, MLStringBufferT, MLNilT, NULL);
	ml_method_by_value(MLStringBufferAppendMethod, NULL, cmdify_integer, MLStringBufferT, MLIntegerT, NULL);
	ml_method_by_value(MLStringBufferAppendMethod, NULL, cmdify_real, MLStringBufferT, MLRealT, NULL);
	ml_method_by_value(MLStringBufferAppendMethod, NULL, cmdify_string, MLStringBufferT, MLStringT, NULL);
	ml_method_by_value(MLStringBufferAppendMethod, NULL, cmdify_method, MLStringBufferT, MLMethodT, NULL);
	ml_method_by_value(MLStringBufferAppendMethod, NULL, cmdify_list, MLStringBufferT, MLListT, NULL);
	ml_method_by_value(MLStringBufferAppendMethod, NULL, cmdify_map, MLStringBufferT, MLMapT, NULL);

	target_init();
	context_init();
	library_init();

	struct sigaction Action[1];
	memset(Action, 0, sizeof(struct sigaction));
	Action->sa_handler = (void *)exit;
	sigaction(SIGINT, Action, NULL);

	const char *TargetName = NULL;
	int NumThreads = 1;
	int InteractiveMode = 0;
	for (int I = 1; I < Argc; ++I) {
		if (Argv[I][0] == '-') {
			switch (Argv[I][1]) {
			case 'V': {
				printf("rabs version %d.%d.%d\n", CURRENT_VERSION);
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
			case 'b': {
				ProgressBar = 1;
				break;
			}
			case 'E': {
				const char *ErrorLogFileName = Argv[I][2] ? (Argv[I] + 2) : Argv[++I];
				ErrorLogFile = open(ErrorLogFileName, O_CREAT | O_WRONLY, 0600);
				if (ErrorLogFile == -1) {
					printf("Error open error file: %s\n", Argv[I] + 2);
					exit(-1);
				}
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
			case 'F': {
				if (Argv[I][2]) {
					SystemName = Argv[I] + 2;
				} else {
					SystemName = Argv[++I];
				}
				break;
			}
			case 'G': {
				DependencyGraph = fopen("dependencies.dot", "w");
				fprintf(DependencyGraph, "digraph Dependencies {\n");
				fprintf(DependencyGraph, "\tnode [shape=box];\n");
				break;
			}
			case 'i': {
				InteractiveMode = 1;
				break;
			}
			case 'd': {
				DebugThreads = 1;
				break;
			}
#ifdef Linux
			case 'w': {
				targetwatch_init();
				WatchMode = 1;
				break;
			}
#endif
			case 't': {
				GC_disable();
				break;
			}
			case '-': {
				break;
			}
			case 'h': default: {
				printf("Usage: %s { options } [ target ]\n", Argv[0]);
				puts("    -h              display this message and exit");
				puts("    -V              print version and exit");
				puts("    -Dkey[=value]   add a define");
				puts("    -c              print shell commands");
				puts("    -s              print each target after building");
				puts("    -p n            run n threads");
				puts("    -G              generate dependencies.dot");
#ifdef Linux
				puts("    -w              watch for file changes [experimental]");
#endif
				exit(0);
			}
			}
		} else {
			TargetName = Argv[I];
		}
	}

	const char *Path = getcwd(NULL, 0);
	CurrentDirectory = Path;
	RootPath = find_root(Path);
	if (!RootPath) {
		SystemName = "_minibuild_";
		RootPath = find_root(Path);
	}
	if (!RootPath) {
		puts("\e[31mError: could not find project root\e[0m");
		exit(1);
	}

	printf("RootPath = %s\n", RootPath);
	printf("Building in %s\n", Path);
	cache_open(RootPath);

	context_push("");

	target_t *Iteration = target_create("meta:::ITERATION");
	Iteration->LastUpdated = CurrentIteration;
	memset(Iteration->Hash, 0, SHA256_BLOCK_SIZE);
	*(long *)Iteration->Hash = CurrentIteration;
	Iteration->Hash[SHA256_BLOCK_SIZE - 1] = 1;
	context_symb_set(CurrentContext, "ITERATION", (ml_value_t *)Iteration);
	context_symb_set(CurrentContext, "ROOT", (ml_value_t *)CurrentContext);

	CurrentThread = new(build_thread_t);
	CurrentThread->Id = 0;
	CurrentThread->Status = BUILD_IDLE;
	if (!InteractiveMode) target_threads_start(NumThreads);

	ml_value_t *Result = load_file(concat(RootPath, "/", SystemName, NULL));
	if (Result->Type == MLErrorT) {
		printf("\e[31mError: %s\n\e[0m", ml_error_message(Result));
		ml_source_t Source;
		int Level = 0;
		while (ml_error_source(Result, Level++, &Source)) {
			printf("\e[31m\t%s:%d\n\e[0m", Source.Name, Source.Line);
		}
		exit(1);
	}
	target_t *Target;
	if (TargetName) {
		int HasPrefix = !strncmp(TargetName, "meta:", strlen("meta:"));
		HasPrefix |= !strncmp(TargetName, "file:", strlen("file:"));
		if (!HasPrefix) {
			TargetName = concat("meta:", match_prefix(Path, RootPath), "::", TargetName, NULL);
		}
		Target = target_find(TargetName);
		if (!Target) {
			printf("\e[31mError: target not defined: %s\e[0m\n", TargetName);
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
	target_threads_wait(Target);
	if (DependencyGraph) {
		fprintf(DependencyGraph, "}");
		fclose(DependencyGraph);
	}
	if (InteractiveMode) {
		target_interactive_start(NumThreads);
		ml_console(rabs_ml_global, Globals, "--> ", "... ");
	} else if (WatchMode) {
#ifdef Linux
		targetwatch_wait(restart);
#endif
	}
	return 0;
}
