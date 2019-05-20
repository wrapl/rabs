#include <gc/gc.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
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
#include "ml_object.h"
#include "ml_file.h"
#include "rabs.h"
#include "minilang/stringmap.h"
#include "library.h"
#include "ml_console.h"

#ifdef Linux
#include "targetwatch.h"
#endif

#ifndef Mingw
#include <sys/wait.h>
#endif

const char *SystemName = "build.rabs";
const char *RootPath = 0;
ml_value_t *AppendMethod;
ml_value_t *StringMethod;
ml_value_t *ArgifyMethod;
ml_value_t *CmdifyMethod;
static int EchoCommands = 0;

static stringmap_t Globals[1] = {STRINGMAP_INIT};
static stringmap_t Defines[1] = {STRINGMAP_INIT};
static int SavedArgc;
static char **SavedArgv;

ml_value_t *rabs_global(const char *Name) {
	return stringmap_search(Globals, Name) ?: MLNil;
}

static ml_value_t *rabs_ml_get(void *Data, const char *Name) {
	ml_value_t *Value = context_symb_get(CurrentContext, Name);
	if (Value) {
		target_t *Target = target_symb_new(Name);
		target_depends_auto(Target);
		return Value;
	} else {
		return stringmap_search(Globals, Name) ?: MLNil; //ml_error("NameError", "%s undefined", Name);
	}
}

static ml_value_t *rabs_ml_set(void *Data, const char *Name, ml_value_t *Value) {
	context_symb_set(CurrentContext, Name, Value);
	//target_symb_update(Name);
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
	mlc_error_t Error[1];
} preprocessor_t;

static const char *preprocessor_read(preprocessor_t *Preprocessor) {
	preprocessor_node_t *Node = Preprocessor->Nodes;
	while (Node) {
		if (!Node->File) {
			Node->File = fopen(Node->FileName, "r");
			if (!Node->File) {
				Preprocessor->Error->Message = ml_error("LoadError", "error opening %s", Node->FileName);
				longjmp(Preprocessor->Error->Handler, 1);
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
		} else {
			const char *NewLine = GC_strdup(Line);
			free(Line);
			return NewLine;
		}
	}
	return NULL;
}

static ml_value_t *load_file(const char *FileName) {
#ifdef Linux
	if (WatchMode) targetwatch_add(FileName);
#endif
	preprocessor_node_t *Node = new(preprocessor_node_t);
	Node->FileName = FileName;
	preprocessor_t Preprocessor[1] = {Node, NULL,};
	Preprocessor->Scanner = ml_scanner(FileName, Preprocessor, (void *)preprocessor_read, Preprocessor->Error);
	if (setjmp(Preprocessor->Error->Handler)) return Preprocessor->Error->Message;
	mlc_expr_t *Expr = ml_accept_block(Preprocessor->Scanner);
	ml_accept_eoi(Preprocessor->Scanner);
	ml_value_t *Closure = ml_compile(Expr, rabs_ml_global, NULL, Preprocessor->Error);
	if (Closure->Type == MLErrorT) return Closure;
	return ml_call(Closure, 0, NULL);
}

ml_value_t *subdir(void *Data, int Count, ml_value_t **Args) {
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
	targetset_insert(ParentDefault->Depends, CurrentContext->Default);
	ml_value_t *Result = load_file(FileName);
	context_pop();
	if (Result->Type == MLErrorT) {
		return Result;
	} else {
		return (ml_value_t *)Context;
	}
}

ml_value_t *scope(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(2);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	const char *Name = ml_string_value(Args[0]);
	context_t *Context = context_scope(Name);
	ml_value_t *Result = ml_call(Args[1], 0, NULL);
	if (Result->Type == MLErrorT) {
		printf("Error: %s\n", ml_error_message(Result));
		const char *Source;
		int Line;
		for (int I = 0; ml_error_trace(Result, I, &Source, &Line); ++I) printf("\e[31m\t%s:%d\n\e[0m", Source, Line);
		exit(1);
	}
	context_pop();
	return (ml_value_t *)Context;
}

ml_value_t *symbol(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	return rabs_ml_global(NULL, ml_string_value(Args[0]));
}

ml_value_t *include(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
	for (int I = 0; I < Count; ++I) {
		ml_value_t *Result = ml_inline(AppendMethod, 2, Buffer, Args[I]);
		if (Result->Type == MLErrorT) return Result;
	}
	char *FileName = ml_stringbuffer_get(Buffer);
	char *Extension = FileName + strlen(FileName);
	while ((Extension > FileName) && (Extension[-1] != '.')) --Extension;
	if (!strcmp(Extension, "rabs")) {
		if (FileName[0] != '/') {
			FileName = vfs_resolve(concat(CurrentContext->FullPath, "/", FileName, NULL));
		}
		return load_file(FileName);
	} else if (!strcmp(Extension, "so")) {
		return library_load(FileName, Globals);
	} else {
		return ml_error("IncludeError", "Unknown include type: %s", FileName);
	}
}

ml_value_t *vmount(void *Data, int Count, ml_value_t **Args) {
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

ml_value_t *context(void *Data, int Count, ml_value_t **Args) {
	if (Count > 0) {
		ML_CHECK_ARG_TYPE(0, MLStringT);
		return (ml_value_t *)context_find(ml_string_value(Args[0]));
	} else {
		return (ml_value_t *)CurrentContext;
	}
}

#ifdef __MINGW32__
#define WIFEXITED(Status) (((Status) & 0x7f) == 0)
#define WEXITSTATUS(Status) (((Status) & 0xff00) >> 8)
#endif

ml_value_t *cmdify_nil(void *Data, int Count, ml_value_t **Args) {
	return MLNil;
}

ml_value_t *cmdify_integer(void *Data, int Count, ml_value_t **Args) {
	ml_stringbuffer_t *Buffer = (ml_stringbuffer_t *)Args[0];
	ml_stringbuffer_addf(Buffer, "%ld", ml_integer_value(Args[1]));
	return MLSome;
}

ml_value_t *cmdify_real(void *Data, int Count, ml_value_t **Args) {
	ml_stringbuffer_t *Buffer = (ml_stringbuffer_t *)Args[0];
	ml_stringbuffer_addf(Buffer, "%f", ml_real_value(Args[1]));
	return MLSome;
}

ml_value_t *cmdify_string(void *Data, int Count, ml_value_t **Args) {
	ml_stringbuffer_t *Buffer = (ml_stringbuffer_t *)Args[0];
	ml_stringbuffer_add(Buffer, ml_string_value(Args[1]), ml_string_length(Args[1]));
	return ml_string_length(Args[1]) ? MLSome : MLNil;
}

ml_value_t *cmdify_method(void *Data, int Count, ml_value_t **Args) {
	ml_list_append(Args[0], ml_string(ml_method_name(Args[1]), -1));
	return MLSome;
}

ml_value_t *cmdify_list(void *Data, int Count, ml_value_t **Args) {
	ml_stringbuffer_t *Buffer = (ml_stringbuffer_t *)Args[0];
	ml_list_node_t *Node = ((ml_list_t *)Args[1])->Head;
	if (Node) {
		ml_inline(CmdifyMethod, 2, Buffer, Node->Value);
		while ((Node = Node->Next)) {
			ml_stringbuffer_add(Buffer, " ", 1);
			ml_inline(CmdifyMethod, 2, Buffer, Node->Value);
		}
		return MLSome;
	} else {
		return MLNil;
	}
}

typedef struct cmdify_context_t {
	ml_value_t *Argv;
	ml_value_t *Result;
	int First;
} cmdify_context_t;

static int cmdify_tree_node(ml_value_t *Key, ml_value_t *Value, cmdify_context_t *Context) {
	ml_stringbuffer_t *Buffer = (ml_stringbuffer_t *)Context->Argv;
	if (Context->First) {
		Context->First = 0;
	} else {
		ml_stringbuffer_add(Buffer, " ", 1);
	}
	ml_value_t *Result = ml_inline(AppendMethod, 2, Buffer, Key);
	if (Result->Type == MLErrorT) {
		Context->Result = Result;
		return 1;
	}
	if (Value != MLNil) {
		ml_stringbuffer_add(Buffer, "=", 1);
		Result = ml_inline(AppendMethod, 2, Buffer, Value);
		if (Result->Type == MLErrorT) {
			Context->Result = Result;
			return 1;
		}
	}
	return 0;
}

ml_value_t *cmdify_tree(void *Data, int Count, ml_value_t **Args) {
	cmdify_context_t Context = {Args[0], MLSome, 1};
	ml_tree_foreach(Args[1], &Context, (void *)cmdify_tree_node);
	return Context.Result;
}

ml_value_t *execute(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
	for (int I = 0; I < Count; ++I) {
		ml_value_t *Result = ml_inline(CmdifyMethod, 2, Buffer, Args[I]);
		if (Result->Type == MLErrorT) return Result;
		if (Result != MLNil) ml_stringbuffer_add(Buffer, " ", 1);
	}
	const char *Command = ml_stringbuffer_get(Buffer);
	if (EchoCommands) printf("\e[34m%s: %s\e[0m\n", CurrentDirectory, Command);
	if (DebugThreads) {
		strncpy(CurrentThread->Command, Command, sizeof(CurrentThread->Command));
		display_threads();
	}
	clock_t Start = clock();
	if (chdir(CurrentDirectory)) {
		return ml_error("ExecuteError", "error changing directory to %s", CurrentDirectory);
	}
	FILE *File = popen(Command, "r");
	pthread_mutex_unlock(InterpreterLock);
	char Chars[120];
	while (!feof(File)) {
		ssize_t Size = fread(Chars, 1, 120, File);
		if (Size == -1) break;
		//fwrite(Chars, 1, Size, stdout);
	}
	int Result = pclose(File);
	clock_t End = clock();
	pthread_mutex_lock(InterpreterLock);
	if (EchoCommands) printf("\t\e[33m%f seconds.\e[0m\n", ((double)(End - Start)) / CLOCKS_PER_SEC);
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
		ml_value_t *Result = ml_inline(CmdifyMethod, 2, Buffer, Args[I]);
		if (Result->Type == MLErrorT) return Result;
		if (Result != MLNil) ml_stringbuffer_add(Buffer, " ", 1);
	}
	const char *Command = ml_stringbuffer_get(Buffer);
	if (EchoCommands) printf("\e[34m%s: %s\e[0m\n", CurrentDirectory, Command);
	if (DebugThreads) {
		strncpy(CurrentThread->Command, Command, sizeof(CurrentThread->Command));
		display_threads();
	}
	clock_t Start = clock();
	if (chdir(CurrentDirectory)) {
		return ml_error("ExecuteError", "error changing directory to %s", CurrentDirectory);
	}
	FILE *File = popen(Command, "r");
	pthread_mutex_unlock(InterpreterLock);
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
	pthread_mutex_lock(InterpreterLock);
	if (EchoCommands) printf("\t\e[33m%f seconds.\e[0m\n", ((double)(End - Start)) / CLOCKS_PER_SEC);
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

ml_value_t *argify_nil(void *Data, int Count, ml_value_t **Args) {
	return MLSome;
}

ml_value_t *argify_integer(void *Data, int Count, ml_value_t **Args) {
	char *Chars;
	size_t Length = asprintf(&Chars, "%ld", ml_integer_value(Args[1]));
	ml_list_append(Args[0], ml_string(Chars, Length));
	return MLSome;
}

ml_value_t *argify_real(void *Data, int Count, ml_value_t **Args) {
	char *Chars;
	size_t Length = asprintf(&Chars, "%f", ml_real_value(Args[1]));
	ml_list_append(Args[0], ml_string(Chars, Length));
	return MLSome;
}

ml_value_t *argify_string(void *Data, int Count, ml_value_t **Args) {
	ml_list_append(Args[0], Args[1]);
	return MLSome;
}

ml_value_t *argify_method(void *Data, int Count, ml_value_t **Args) {
	ml_list_append(Args[0], ml_string(ml_method_name(Args[1]), -1));
	return MLSome;
}

ml_value_t *argify_list(void *Data, int Count, ml_value_t **Args) {
	for (ml_list_node_t *Node = ((ml_list_t *)Args[1])->Head; Node; Node = Node->Next) {
		ml_value_t *Result = ml_inline(ArgifyMethod, 2, Args[0], Node->Value);
		if (Result->Type == MLErrorT) return Result;
	}
	return MLSome;
}

typedef struct argify_context_t {
	ml_value_t *Argv;
	ml_value_t *Result;
} argify_context_t;

static int argify_tree_node(ml_value_t *Key, ml_value_t *Value, argify_context_t *Context) {
	ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
	ml_value_t *Result = ml_inline(AppendMethod, 2, Buffer, Key);
	if (Result->Type == MLErrorT) {
		Context->Result = Result;
		return 1;
	}
	if (Value != MLNil) {
		ml_stringbuffer_add(Buffer, "=", 1);
		Result = ml_inline(AppendMethod, 2, Buffer, Value);
		if (Result->Type == MLErrorT) {
			Context->Result = Result;
			return 1;
		}
	}
	size_t Length = Buffer->Length;
	Result = ml_string(ml_stringbuffer_get(Buffer), Length);
	ml_list_append(Context->Argv, Result);
	return 0;
}

ml_value_t *argify_tree(void *Data, int Count, ml_value_t **Args) {
	argify_context_t Context = {Args[0], MLSome};
	ml_tree_foreach(Args[1], &Context, (void *)argify_tree_node);
	return Context.Result;
}

#ifdef Mingw
#define rabs_execv execute
#define rabs_shellv shell
#else
ml_value_t *rabs_execv(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ml_value_t *ArgList = ml_list();
	for (int I = 0; I < Count; ++I) {
		ml_value_t *Result = ml_inline(ArgifyMethod, 2, ArgList, Args[I]);
		if (Result->Type == MLErrorT) return Result;
	}
	int Argc = ml_list_length(ArgList);
	const char *Argv[Argc + 1];
	const char **Argp = Argv;
	for (ml_list_node_t *Node = ((ml_list_t *)ArgList)->Head; Node; Node = Node->Next) {
		*Argp = ml_string_value(Node->Value);
		++Argp;
	}
	*Argp = 0;
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

ml_value_t *rabs_shellv(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ml_value_t *ArgList = ml_list();
	for (int I = 0; I < Count; ++I) {
		ml_value_t *Result = ml_inline(ArgifyMethod, 2, ArgList, Args[I]);
		if (Result->Type == MLErrorT) return Result;
	}
	int Argc = ml_list_length(ArgList);
	const char *Argv[Argc + 1];
	const char **Argp = Argv;
	for (ml_list_node_t *Node = ((ml_list_t *)ArgList)->Head; Node; Node = Node->Next) {
		*Argp = ml_string_value(Node->Value);
		++Argp;
	}
	*Argp = 0;
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

ml_value_t *rabs_chdir(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
	for (int I = 0; I < Count; ++I) {
		ml_value_t *Result = ml_inline(AppendMethod, 2, Buffer, Args[I]);
		if (Result->Type == MLErrorT) return Result;
	}
	if (CurrentDirectory) GC_free((void *)CurrentDirectory);
	CurrentDirectory = ml_stringbuffer_get_uncollectable(Buffer);
	return Args[0];
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
	char *FileName = snew(strlen(Path) + strlen(SystemName) + 2);
	char *End = stpcpy(FileName, Path);
	End[0] = '/';
	strcpy(End + 1, SystemName);
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
			strcpy(End + 1, SystemName);
			goto loop;
		}
	}
	return 0;
}

static ml_value_t *print(void *Data, int Count, ml_value_t **Args) {
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
#ifdef __MINGW32__
	char *Buffer;
	asprintf(&Buffer, "%s=%s", Key, Value);
	putenv(Buffer);
#else
	setenv(Key, Value, 1);
#endif
	return MLNil;
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

static ml_value_t *debug(void *Data, int Count, ml_value_t **Args) {
#if defined(ARM)
	__asm__ __volatile__("bkpt");
#else
	asm("int3");
#endif
	return MLNil;
}

static void restart() {
	cache_close();
	execv("/proc/self/exe", SavedArgv);
}

int main(int Argc, char **Argv) {
	SavedArgc = Argc;
	SavedArgv = Argv;
	GC_INIT();
	ml_init();
	AppendMethod = ml_method("append");
	StringMethod = ml_method("string");
	ArgifyMethod = ml_method("argify");
	CmdifyMethod = ml_method("cmdify");
	stringmap_insert(Globals, "vmount", ml_function(0, vmount));
	stringmap_insert(Globals, "subdir", ml_function(0, subdir));
	stringmap_insert(Globals, "file", ml_function(0, target_file_new));
	stringmap_insert(Globals, "meta", ml_function(0, target_meta_new));
	stringmap_insert(Globals, "expr", ml_function(0, target_expr_new));
	stringmap_insert(Globals, "symbol", ml_function(0, symbol));
	stringmap_insert(Globals, "include", ml_function(0, include));
	stringmap_insert(Globals, "context", ml_function(0, context));
	stringmap_insert(Globals, "execute", ml_function(0, execute));
	stringmap_insert(Globals, "shell", ml_function(0, shell));
	stringmap_insert(Globals, "execv", ml_function(0, rabs_execv));
	stringmap_insert(Globals, "shellv", ml_function(0, rabs_shellv));
	stringmap_insert(Globals, "mkdir", ml_function(0, rabs_mkdir));
	stringmap_insert(Globals, "chdir", ml_function(0, rabs_chdir));
	stringmap_insert(Globals, "scope", ml_function(0, scope));
	stringmap_insert(Globals, "print", ml_function(0, print));
	stringmap_insert(Globals, "open", ml_function(0, rabs_open));
	stringmap_insert(Globals, "getenv", ml_function(0, ml_getenv));
	stringmap_insert(Globals, "setenv", ml_function(0, ml_setenv));
	stringmap_insert(Globals, "defined", ml_function(0, defined));
	stringmap_insert(Globals, "check", ml_function(0, target_depends_auto_value));
	stringmap_insert(Globals, "debug", ml_function(0, debug));
	stringmap_insert(Globals, "type", ml_function(0, type));
	stringmap_insert(Globals, "error", ml_function(0, error));

	ml_method_by_value(ArgifyMethod, NULL, argify_nil, MLListT, MLNilT, NULL);
	ml_method_by_value(ArgifyMethod, NULL, argify_integer, MLListT, MLIntegerT, NULL);
	ml_method_by_value(ArgifyMethod, NULL, argify_real, MLListT, MLRealT, NULL);
	ml_method_by_value(ArgifyMethod, NULL, argify_string, MLListT, MLStringT, NULL);
	ml_method_by_value(ArgifyMethod, NULL, argify_method, MLListT, MLMethodT, NULL);
	ml_method_by_value(ArgifyMethod, NULL, argify_list, MLListT, MLListT, NULL);
	ml_method_by_value(ArgifyMethod, NULL, argify_tree, MLListT, MLTreeT, NULL);

	ml_method_by_value(CmdifyMethod, NULL, cmdify_nil, MLStringBufferT, MLNilT, NULL);
	ml_method_by_value(CmdifyMethod, NULL, cmdify_integer, MLStringBufferT, MLIntegerT, NULL);
	ml_method_by_value(CmdifyMethod, NULL, cmdify_real, MLStringBufferT, MLRealT, NULL);
	ml_method_by_value(CmdifyMethod, NULL, cmdify_string, MLStringBufferT, MLStringT, NULL);
	ml_method_by_value(CmdifyMethod, NULL, cmdify_method, MLStringBufferT, MLMethodT, NULL);
	ml_method_by_value(CmdifyMethod, NULL, cmdify_list, MLStringBufferT, MLListT, NULL);
	ml_method_by_value(CmdifyMethod, NULL, cmdify_tree, MLStringBufferT, MLTreeT, NULL);

	target_init();
	context_init();
	ml_file_init();
	ml_object_init(Globals);
	library_init();

	const char *TargetName = 0;
	int QueryOnly = 0;
	int ListTargets = 0;
	int NumThreads = 1;
	int InteractiveMode = 0;
	for (int I = 1; I < Argc; ++I) {
		if (Argv[I][0] == '-') {
			switch (Argv[I][1]) {
			case 'v': {
				printf("rabs version %s\n", CURRENT_VERSION);
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
				if (!strcmp(Argv[I] + 2, "debug-compiler")) {
					MLDebugClosures = 1;
				}
				break;
			}
			case 'h': default: {
				printf("Usage: %s { options } [ target ]\n", Argv[0]);
				puts("    -h              display this message and exit");
				puts("    -v              print version and exit");
				puts("    -Dkey[=value]   add a define");
				puts("    -c              print shell commands");
				puts("    -s              print each target after building");
				puts("    -p n            run n threads");
				puts("    -G              generate dependencies.dot");
#ifdef Linux
				puts("    -w              watch for file changes");
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

	printf("RootPath = %s, Path = %s\n", RootPath, Path);
	cache_open(RootPath);

	context_push("");
	context_symb_set(CurrentContext, "VERSION", ml_integer(CurrentIteration));

	if (!InteractiveMode) target_threads_start(NumThreads);

	ml_value_t *Result = load_file(concat(RootPath, "/", SystemName, NULL));
	if (Result->Type == MLErrorT) {
		printf("\e[31mError: %s\n\e[0m", ml_error_message(Result));
		const char *Source;
		int Line;
		for (int I = 0; ml_error_trace(Result, I, &Source, &Line); ++I) printf("\e[31m\t%s:%d\n\e[0m", Source, Line);
		exit(1);
	}
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
	target_threads_wait(Target);
	if (DependencyGraph) {
		fprintf(DependencyGraph, "}");
		fclose(DependencyGraph);
	}
	if (InteractiveMode) {
		target_interactive_start(NumThreads);
		ml_console(rabs_ml_global, Globals);
	} else if (WatchMode) {
#ifdef Linux
		targetwatch_wait(restart);
#endif
	}
	return 0;
}
