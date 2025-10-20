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
#include "ml_sequence.h"
#include "ml_stream.h"
#include "ml_file.h"
#include "ml_time.h"
#include "ml_json.h"
#include "ml_cbor.h"
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
static int EchoCommands = 0;
ML_METHOD_DECL(ArgifyMethod, "argify");

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

static long rabs_property_hash(rabs_property_t *Property, ml_hash_chain_t *Chain) {
	long Hash = 53817;
	for (const char *P = Property->Name; P[0]; ++P) Hash = ((Hash << 5) + Hash) + P[0];
	return Hash;
}

static ml_value_t *rabs_property_deref(rabs_property_t *Property) {
	ml_value_t *Value = context_symb_get(CurrentContext, Property->Name) ?:
		stringmap_search(Globals, Property->Name) ?: MLNil;
	if (CurrentTarget) target_depends_auto(target_symb_new(CurrentContext, Property->Name));
	return Value;
}

static void rabs_property_assign(ml_state_t *Caller, rabs_property_t *Property, ml_value_t *Value) {
	context_symb_set(CurrentContext, Property->Name, Value);
	//target_symb_update(Name);
	ML_RETURN(Value);
}

static void rabs_property_call(ml_state_t *Caller, rabs_property_t *Property, int Count, ml_value_t **Args) {
	ml_value_t *Value = rabs_property_deref(Property);
	return ml_call(Caller, Value, Count, Args);
}

ML_TYPE(RabsPropertyT, (MLAnyT), "property",
//!internal
	.hash = (void *)rabs_property_hash,
	.deref = (void *)rabs_property_deref,
	.assign = (void *)rabs_property_assign,
	.call = (void *)rabs_property_call
);

ml_value_t *rabs_ml_global(void *Data, const char *Name, const char *Source, int Line, int Mode) {
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
	ml_parser_t *Parser;
	ml_compiler_t *Compiler;
} preprocessor_t;

static const char *preprocessor_read(preprocessor_t *Preprocessor) {
	preprocessor_node_t *Node = Preprocessor->Nodes;
	while (Node) {
		if (!Node->File) {
			Node->File = fopen(Node->FileName, "r");
			if (!Node->File) {
				ml_parse_warn(Preprocessor->Parser, "LoadError", "error opening %s", Node->FileName);
			}
		}
		char *Line = NULL;
		size_t Length = 0;
		ssize_t Actual = getline(&Line, &Length, Node->File);
		if (Actual < 0) {
			free(Line);
			fclose(Node->File);
			Node = Preprocessor->Nodes = Node->Next;
			if (Node) ml_parser_source(Preprocessor->Parser, Node->Source);
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
			Node->Source = ml_parser_source(Preprocessor->Parser, (ml_source_t){FileName, 1});
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
	Preprocessor->Parser = ml_parser((void *)preprocessor_read, Preprocessor);
	Preprocessor->Compiler = ml_compiler((ml_getter_t)rabs_ml_global, NULL);
	ml_parser_source(Preprocessor->Parser, (ml_source_t){FileName, 1});

	load_file_state_t *State = new(load_file_state_t);
	State->Base.run = (void *)load_file_loaded;
	State->Base.Context = MLRootContext;
	const mlc_expr_t *Expr = ml_accept_file(Preprocessor->Parser);
	if (!Expr) {
		return ml_parser_value(Preprocessor->Parser) ?: ml_error("FileError", "Error loading file %s", FileName);
	}
	ml_function_compile((ml_state_t *)State, Expr, Preprocessor->Compiler, NULL);
	ml_scheduler_t *Scheduler = ml_context_get_static(MLRootContext, ML_SCHEDULER_INDEX);
	while (!State->Result) Scheduler->run(Scheduler);
	return State->Result;
}

ML_FUNCTION(Subdir) {
//<Name:string
//>context|error
// Creates a new directory subcontext with name :mini:`Name` and loads the :file:`build.rabs` file inside the directory.
// Returns an error if the directory does not exist or does not contain a :file:`build.rabs` file.
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
	int Depends = 1;
	for (int I = 1; I < Count; ++I) {
		if (ml_is(Args[I], MLListT)) {
			stringmap_t *Filter = Context->Filter = stringmap_new();
			ML_LIST_FOREACH(Args[I], Iter) {
				if (!ml_is(Iter->Value, MLStringT)) return ml_error("TypeError", "Expected string");
				stringmap_insert(Filter, ml_string_value(Iter->Value), MLNil);
			}
		} else if (ml_is(Args[I], MLMapT)) {
			ML_MAP_FOREACH(Args[I], Iter) {
				if (!ml_is(Iter->Key, MLStringT)) return ml_error("TypeError", "Expected string");
				context_symb_set(Context, ml_string_value(Iter->Key), Iter->Value);
			}
		} else if (Args[I] == MLNil) {
			Depends = 0;
		}
	}
	if (Depends) targetset_insert(ParentDefault->Depends, CurrentContext->Default);
	ml_value_t *Result = load_file(FileName);
	context_pop();
	if (ml_is_error(Result)) {
		return Result;
	} else {
		return (ml_value_t *)Context;
	}
}

ML_FUNCTION(Scope) {
//<Name:string
//<Function:function
//>any
// Creates a new scoped subcontext with name :mini:`Name` and calls :mini:`Function()` in the new context.
	ML_CHECK_ARG_COUNT(2);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	const char *Name = ml_string_value(Args[0]);
	context_scope(Name);
	ml_value_t *Result = ml_simple_call(Args[1], 0, NULL);
	context_pop();
	return Result;
}

#if defined(Darwin)
#define LIB_EXTENSION ".dylib"
#elif defined(Mingw)
#define LIB_EXTENSION ".dll"
#else
#define LIB_EXTENSION ".so"
#endif

#define MAX_EXTENSION 10

ML_FUNCTION(Include) {
//<Path..:string
//>any
// Loads the Minilang file or shared library specified by :mini:`Path`.
	ML_CHECK_ARG_COUNT(1);
	ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
	for (int I = 0; I < Count; ++I) {
		ml_value_t *Result = ml_stringbuffer_simple_append(Buffer, Args[I]);
		if (ml_is_error(Result)) return Result;
	}
	size_t Length = Buffer->Length;
	char *FileName0 = GC_MALLOC_ATOMIC(Length + MAX_EXTENSION);
	memcpy(FileName0, ml_stringbuffer_get_string(Buffer), Length);
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

ML_FUNCTION(Vmount) {
//<Path:string
//<Source:string
//>nil
// Mounts the directory :mini:`Source` onto :mini:`Path`. Resolving a file in :mini:`Path` will also check :mini:`Source`.
	ML_CHECK_ARG_COUNT(2);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	ML_CHECK_ARG_TYPE(1, MLStringT);
	const char *Path = ml_string_value(Args[0]);
	const char *Target = ml_string_value(Args[1]);
	if (Target[0] == '/') {
		vfs_mount(
			concat(CurrentContext->Path, "/", Path, NULL),
			Target,
			1
		);
	} else {
		vfs_mount(
			concat(CurrentContext->Path, "/", Path, NULL),
			concat(CurrentContext->Path, "/", Target, NULL),
			0
		);
	}
	return MLNil;
}

#ifdef __MINGW32__
#define WIFEXITED(Status) (((Status) & 0x7f) == 0)
#define WEXITSTATUS(Status) (((Status) & 0xff00) >> 8)
#endif

ML_METHOD("append", MLStringBufferT, MLNilT) {
//!internal
	return MLNil;
}

ML_METHOD("append", MLStringBufferT, MLMethodT) {
//!internal
	ml_stringbuffer_t *Buffer = (ml_stringbuffer_t *)Args[0];
	const char *Name = ml_method_name(Args[1]);
	ml_stringbuffer_write(Buffer, Name, strlen(Name));
	return MLSome;
}

ML_METHOD("append", MLStringBufferT, MLListT) {
//!internal
	ml_stringbuffer_t *Buffer = (ml_stringbuffer_t *)Args[0];
	int Last = Buffer->Length;
	ML_LIST_FOREACH(Args[1], Node) {
		if (Buffer->Length > Last) ml_stringbuffer_put(Buffer, ' ');
		Last = Buffer->Length;
		ml_stringbuffer_simple_append(Buffer, Node->Value);
	}
	return MLSome;
}

ML_METHOD("append", MLStringBufferT, MLMapT) {
//!internal
	ml_stringbuffer_t *Buffer = (ml_stringbuffer_t *)Args[0];
	int Last = Buffer->Length;
	ML_MAP_FOREACH(Args[1], Iter) {
		if (Buffer->Length > Last) ml_stringbuffer_put(Buffer, ' ');
		Last = Buffer->Length;
		ml_value_t *Result = ml_stringbuffer_simple_append(Buffer, Iter->Key);
		if (ml_is_error(Result)) return Result;
		if (Iter->Value != MLNil && Iter->Value != MLSome) {
			ml_stringbuffer_put(Buffer, '=');
			Result = ml_stringbuffer_simple_append(Buffer, Iter->Value);
			if (ml_is_error(Result)) return Result;
		}
	}
	return MLSome;
}

static int ErrorLogFile = STDERR_FILENO;

typedef struct command_output_t command_output_t;

struct command_output_t {
	command_output_t *Next;
	char Chars[ML_STRINGBUFFER_NODE_SIZE];
};

static ml_value_t *command(int Capture, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
	int Last = Buffer->Length;
	for (int I = 0; I < Count; ++I) {
		if (Buffer->Length > Last) ml_stringbuffer_put(Buffer, ' ');
		Last = Buffer->Length;
		ml_value_t *Result = ml_stringbuffer_simple_append(Buffer, Args[I]);
		if (ml_is_error(Result)) return Result;
	}
	const char *Command = ml_stringbuffer_get_string(Buffer);
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
		size_t Total = 0;
		command_output_t *Head = new(command_output_t), *Current = Head;
		size_t Space = ML_STRINGBUFFER_NODE_SIZE;
		char *Chars = Current->Chars;
		for (;;) {
			ssize_t Count = read(Pipe[0], Chars, Space);
			if (Count <= 0) break;
			if (Count > 0) {
				Total += Count;
				Space -= Count;
				if (!Space) {
					Current = Current->Next = new(command_output_t);
					Space = ML_STRINGBUFFER_NODE_SIZE;
					Chars = Current->Chars;
				} else {
					Chars += Count;
				}
			}
		}
		Chars = snew(Total + 1);
		char *End = Chars;
		for (command_output_t *Output = Head; Output != Current; Output = Output->Next) {
			memcpy(End, Output->Chars, ML_STRINGBUFFER_NODE_SIZE);
			End += ML_STRINGBUFFER_NODE_SIZE;
		}
		memcpy(End, Current->Chars, ML_STRINGBUFFER_NODE_SIZE - Space);
		Chars[Total] = 0;
		Result = ml_string(Chars, Total);
	} else {
		char Chars[ML_STRINGBUFFER_NODE_SIZE];
		for (;;) {
			ssize_t Count = read(Pipe[0], Chars, ML_STRINGBUFFER_NODE_SIZE);
			if (Count <= 0) break;
		}
	}
	close(Pipe[0]);
	int Status;
	if (waitpid(Child, &Status, 0) == -1) {
		Result = ml_error("WaitError", "error waiting for child process");
		ml_error_trace_add(Result, (ml_source_t){Command, 0});
	}
	clock_t End = clock();
	pthread_mutex_lock(InterpreterLock);
	if (CurrentThread) CurrentThread->Child = 0;
	if (EchoCommands) printf("\t\e[33m%f seconds.\e[0m\n", ((double)(End - Start)) / CLOCKS_PER_SEC);
	if (WIFEXITED(Status)) {
		if (WEXITSTATUS(Status) != 0) {
			Result = ml_error("ExecuteError", "process returned non-zero exit code");
			ml_error_trace_add(Result, (ml_source_t){Command, WEXITSTATUS(Status)});
		}
	} else {
		Result = ml_error("ExecuteError", "process exited abnormally");
		ml_error_trace_add(Result, (ml_source_t){Command, 0});
	}
	return Result;
}

ML_FUNCTION(Execute) {
//<Command..:any
//>nil|error
// Builds a shell command from :mini:`Command..` and executes it, discarding the output. Returns :mini:`nil` on success or raises an error.
	return command(0, Count, Args);
}

ML_FUNCTION(Shell) {
//<Command..:any
//>string|error
// Builds a shell command from :mini:`Command..` and executes it, capturing the output. Returns the captured output on success or raises an error.
	return command(1, Count, Args);
}

ML_METHOD(ArgifyMethod, MLListT, MLNilT) {
//!internal
	return Args[0];
}

ML_METHOD(ArgifyMethod, MLListT, MLIntegerT) {
//!internal
	char *Chars;
	size_t Length = asprintf(&Chars, "%ld", ml_integer_value(Args[1]));
	ml_list_append(Args[0], ml_string(Chars, Length));
	return Args[0];
}

ML_METHOD(ArgifyMethod, MLListT, MLRealT) {
//!internal
	char *Chars;
	size_t Length = asprintf(&Chars, "%f", ml_real_value(Args[1]));
	ml_list_append(Args[0], ml_string(Chars, Length));
	return Args[0];
}

ML_METHOD(ArgifyMethod, MLListT, MLStringT) {
//!internal
	ml_list_append(Args[0], Args[1]);
	return Args[0];
}

ML_METHOD(ArgifyMethod, MLListT, MLMethodT) {
//!internal
	ml_list_append(Args[0], ml_string(ml_method_name(Args[1]), -1));
	return Args[0];
}

ML_METHOD(ArgifyMethod, MLListT, MLListT) {
//!internal
	ML_LIST_FOREACH(Args[1], Node) {
		ml_value_t *Result = ml_simple_inline(ArgifyMethod, 2, Args[0], Node->Value);
		if (ml_is_error(Result)) return Result;
	}
	return Args[0];
}

ML_METHOD(ArgifyMethod, MLListT, MLMapT) {
//!internal
	ML_MAP_FOREACH(Args[1], Iter) {
		ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
		ml_value_t *Result = ml_stringbuffer_simple_append(Buffer, Iter->Key);
		if (ml_is_error(Result)) return Result;
		if (Iter->Value != MLNil && Iter->Value != MLSome) {
			ml_stringbuffer_put(Buffer, '=');
			Result = ml_stringbuffer_simple_append(Buffer, Iter->Value);
			if (ml_is_error(Result)) return Result;
		}
		ml_list_append(Args[0], ml_stringbuffer_get_value(Buffer));
	}
	return Args[0];
}

#ifdef Mingw

#define Execv Execute
#define Shellv Shell

#else

ML_FUNCTION(Execv) {
//<Command:list
//>nil|error
// Similar to :mini:`execute()` but expects a list of individual arguments instead of letting the shell split the command line.
	ML_CHECK_ARG_COUNT(1);
	ml_value_t *ArgList = ml_list();
	for (int I = 0; I < Count; ++I) {
		ml_value_t *Result = ml_simple_inline(ArgifyMethod, 2, ArgList, Args[I]);
		if (ml_is_error(Result)) return Result;
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

ML_FUNCTION(Shellv) {
//<Command:list
//>nil|error
// Similar to :mini:`shell()` but expects a list of individual arguments instead of letting the shell split the command line.
	ML_CHECK_ARG_COUNT(1);
	ml_value_t *ArgList = ml_list();
	for (int I = 0; I < Count; ++I) {
		ml_value_t *Result = ml_simple_inline(ArgifyMethod, 2, ArgList, Args[I]);
		if (ml_is_error(Result)) return Result;
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
		if (Size > 0) ml_stringbuffer_write(Buffer, Chars, Size);
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
			ml_value_t *Result = ml_string(ml_stringbuffer_get_string(Buffer), Length);
			return Result;
		}
	} else {
		return ml_error("ExecuteError", "process exited abnormally");
	}
}
#endif

ML_FUNCTION(Mkdir) {
//<Path..:any
//>nil|error
// Creates a directory with path :mini:`Path` if it does not exist, creating intermediate directories if required.
	ML_CHECK_ARG_COUNT(1);
	ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
	for (int I = 0; I < Count; ++I) {
		ml_value_t *Result = ml_stringbuffer_simple_append(Buffer, Args[I]);
		if (ml_is_error(Result)) return Result;
	}
	char *Path = ml_stringbuffer_get_string(Buffer);
	if (mkdir_p(Path) < 0) {
		return ml_error("FileError", "error creating directory %s", Path);
	}
	return MLNil;
}

ML_FUNCTION(Chdir) {
//<Path..:any
//>nil
// Changes the current directory to :mini:`Path`.
	ML_CHECK_ARG_COUNT(1);
	ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
	for (int I = 0; I < Count; ++I) {
		ml_value_t *Result = ml_stringbuffer_simple_append(Buffer, Args[I]);
		if (ml_is_error(Result)) return Result;
	}
	if (CurrentDirectory) GC_free((void *)CurrentDirectory);
	CurrentDirectory = ml_stringbuffer_get_uncollectable(Buffer);
	return Args[0];
}

ML_FUNCTION(Open) {
//<Path:any
//<Mode:string
//>file
// Opens the file at path :mini:`Path` with the specified mode.
	ML_CHECK_ARG_COUNT(2);
	ML_CHECK_ARG_TYPE(1, MLStringT);
	ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
	ml_value_t *Result = ml_stringbuffer_simple_append(Buffer, Args[0]);
	if (ml_is_error(Result)) return Result;
	const char *Path = ml_stringbuffer_get_string(Buffer);
	const char *Mode = ml_string_value(Args[1]);
	FILE *Handle = fopen(Path, Mode);
	if (!Handle) return ml_error("FileError", "failed to open %s in mode %s: %s", Path, Mode, strerror(errno));
	return ml_file(Handle);
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

ML_FUNCTION(Print) {
//<Values..:any
//>nil
// Prints out :mini:`Values` to standard output.
	ml_stringbuffer_t Buffer[1] = {ML_STRINGBUFFER_INIT};
	for (int I = 0; I < Count; ++I) {
		ml_value_t *Result = ml_stringbuffer_simple_append(Buffer, Args[I]);
		if (ml_is_error(Result)) return Result;
	}
	ml_stringbuffer_drain(Buffer, stdout, (void *)ml_stringbuffer_print);
	fflush(stdout);
	return MLNil;
}

ML_FUNCTION(Getenv) {
//<Name:string
//>string|nil
// Returns the current value of the environment variable :mini:`Name` or :mini:`nil` if it is not defined.
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

ML_FUNCTION(Setenv) {
//<Name:string
//<Value:string
//>nil
// Sets the value of the environment variable :mini:`Name` to :mini:`Value`.
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

ML_FUNCTION(Target) {
//<Path:string
//>target|error
// Returns the target with path :mini:`Path` if is has been defined, otherwise raises an error.
	ML_CHECK_ARG_COUNT(1);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	const char *Id = ml_string_value(Args[0]);
	target_t *Target = target_find(Id);
	if (!Target) return ml_error("ValueError", "Target %s not found", Id);
	return (ml_value_t *)Target;
}

ML_FUNCTION(Defined) {
//<Name:string
//>string|nil
// If :mini:`Name` was defined in the *rabs* command line then returns the associated value, otherwise returns :mini:`nil`.
	ML_CHECK_ARG_COUNT(1);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	const char *Key = ml_string_value(Args[0]);
	return stringmap_search(Defines, Key) ?: MLNil;
}

static int target_depends_auto_single(ml_value_t *Arg, void *Data) {
	if (ml_is(Arg, MLListT)) {
		ML_LIST_FOREACH(Arg, Iter) {
			if (target_depends_auto_single(Iter->Value, NULL)) return 1;
		}
	} else if (ml_is(Arg, MLStringT)) {
		target_t *Depend = target_symb_new(CurrentContext, ml_string_value(Arg));
		target_depends_auto(Depend);
		return 0;
	} else if (ml_is(Arg, TargetT)) {
		target_depends_auto((target_t *)Arg);
		return 0;
	} else if (Arg == MLNil) {
		return 0;
	}
	return 1;
}

ML_FUNCTION(Check) {
//<Target..:target
//>nil
// Checks that each :mini:`Target` is up to date, building if necessary.
	for (int I = 0; I < Count; ++I) target_depends_auto_single(Args[I], NULL);
	return MLNil;
}

/*static ml_value_t *type(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	return ml_string(Args[0]->Type->Name, -1);
}*/

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
	//printf("Looking for library path at %s\n", LibPath);
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

typedef struct target_arg_t target_arg_t;

struct target_arg_t {
	target_arg_t *Next;
	const char *Name;
	target_t *Target;
};

int main(int Argc, char **Argv) {
	CurrentDirectory = "<random>";
	SavedArgc = Argc;
	SavedArgv = Argv;
	GC_INIT();
	ml_init(Argv[0], Globals);
	ml_object_init(Globals);
	ml_sequence_init(Globals);
	ml_stream_init(Globals);
	ml_file_init(Globals);
	ml_time_init(Globals);
	ml_json_init(Globals);
	ml_cbor_init(Globals);
	ml_uuid_init(Globals);
	stringmap_insert(Globals, "vmount", Vmount);
	stringmap_insert(Globals, "subdir", Subdir);
	stringmap_insert(Globals, "target", Target);
	stringmap_insert(Globals, "file", FileT);
	stringmap_insert(Globals, "meta", MetaT);
	stringmap_insert(Globals, "expr", ExprT);
	// TODO: add functions to register and create udf targets
	stringmap_insert(Globals, "symbol", SymbolT);
	stringmap_insert(Globals, "scan", ScanT);
	stringmap_insert(Globals, "include", Include);
	stringmap_insert(Globals, "context", ContextT);
	stringmap_insert(Globals, "execute", Execute);
	stringmap_insert(Globals, "shell", Shell);
	stringmap_insert(Globals, "execv", Execv);
	stringmap_insert(Globals, "shellv", Shellv);
	stringmap_insert(Globals, "mkdir", Mkdir);
	stringmap_insert(Globals, "chdir", Chdir);
	stringmap_insert(Globals, "scope", Scope);
	stringmap_insert(Globals, "print", Print);
	stringmap_insert(Globals, "open", Open);
	stringmap_insert(Globals, "getenv", Getenv);
	stringmap_insert(Globals, "setenv", Setenv);
	stringmap_insert(Globals, "defined", Defined);
	stringmap_insert(Globals, "check", Check);
	stringmap_insert(Globals, "error", MLErrorValueT);
	int Version[] = {CURRENT_VERSION};
	stringmap_insert(Globals, "VERSION", ml_tuplev(3, ml_integer(Version[0]), ml_integer(Version[1]), ml_integer(Version[2])));
	stringmap_insert(Globals, "LIBPATH", lib_path());
#ifndef GENERATE_INIT
#include "rabs_init.c"
#endif
	target_init();
	context_init();
	library_init();

	struct sigaction Action[1];
	memset(Action, 0, sizeof(struct sigaction));
	Action->sa_handler = (void *)exit;
	sigaction(SIGINT, Action, NULL);


	target_arg_t *TargetArgs = NULL;
	int NumThreads = 1;
	int InteractiveMode = 0;
	for (int I = 1; I < Argc; ++I) {
		if (Argv[I][0] == '-') {
			switch (Argv[I][1]) {
			case 'V': {
				printf("%d.%d.%d\n", CURRENT_VERSION);
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
			case 'H': {
				DisplayHashes = 1;
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
			target_arg_t *Arg = new(target_arg_t);
			Arg->Name = Argv[I];
			Arg->Next = TargetArgs;
			TargetArgs = Arg;
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
	if (ml_is_error(Result)) {
		printf("\e[31mError: %s\n\e[0m", ml_error_message(Result));
		ml_source_t Source;
		int Level = 0;
		while (ml_error_source(Result, Level++, &Source)) {
			printf("\e[31m\t%s:%d\n\e[0m", Source.Name, Source.Line);
		}
		exit(1);
	}
	if (TargetArgs) {
		for (target_arg_t *Arg = TargetArgs; Arg; Arg = Arg->Next) {
			int HasPrefix = !strncmp(Arg->Name, "meta:", strlen("meta:"));
			HasPrefix |= !strncmp(Arg->Name, "file:", strlen("file:"));
			if (!HasPrefix) {
				Arg->Name = concat("meta:", match_prefix(Path, RootPath), "::", Arg->Name, NULL);
			}
			Arg->Target = target_find(Arg->Name);
			if (!Arg->Target) {
				printf("\e[31mError: target not defined: %s\e[0m\n", Arg->Name);
				exit(1);
			}
		}
	} else {
		context_t *Context = context_find(match_prefix(Path, RootPath));
		if (!Context) {
			printf("\e[31mError: current directory is not in project\e[0m");
			exit(1);
		}
		target_arg_t *Arg = new(target_arg_t);
		Arg->Target = Context->Default;
		TargetArgs = Arg;
	}
	for (target_arg_t *Arg = TargetArgs; Arg; Arg = Arg->Next) {
		target_queue(Arg->Target, NULL);
	}
	target_threads_wait();
	if (DependencyGraph) {
		fprintf(DependencyGraph, "}");
		fclose(DependencyGraph);
	}
	if (InteractiveMode) {
		target_interactive_start(NumThreads);
		ml_console(MLRootContext, rabs_ml_global, Globals, "--> ", "... ");
	} else if (WatchMode) {
#ifdef Linux
		targetwatch_wait(restart);
#endif
	}
	return 0;
}
