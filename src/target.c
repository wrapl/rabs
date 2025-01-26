#include "target.h"
#include "target_file.h"
#include "target_expr.h"
#include "target_meta.h"
#include "target_scan.h"
#include "target_symb.h"
#include "rabs.h"
#include "util.h"
#include "context.h"
#include "targetcache.h"
#include "targetqueue.h"
#include "cache.h"
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <gc/gc.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <dirent.h>
#include <ml_file.h>
#include <limits.h>
#include <signal.h>
#include <inttypes.h>
#include "ml_bytecode.h"
#include "ml_cbor.h"

#ifdef Linux
#include "targetwatch.h"
#endif

#ifndef Mingw
#include <sys/wait.h>
#endif

#undef ML_CATEGORY
#define ML_CATEGORY "target"

enum {
	STATE_UNCHECKED = 0,
	STATE_CHECKING = -1,
	STATE_QUEUED = -2
};

int StatusUpdates = 0;
int ProgressBar = 0;
int MonitorFiles = 0;
int DebugThreads = 0;
int WatchMode = 0;
FILE *DependencyGraph = NULL;

pthread_mutex_t InterpreterLock[1] = {PTHREAD_MUTEX_INITIALIZER};
static pthread_cond_t TargetAvailable[1] = {PTHREAD_COND_INITIALIZER};
static pthread_cond_t TargetUpdated[1] = {PTHREAD_COND_INITIALIZER};

ML_TYPE(TargetT, (MLAnyT), "target");
// Base type for all targets.

ML_METHOD_DECL(SHA256, "sha256");
ML_METHOD_DECL(Missing, "missing");

__thread target_t *CurrentTarget = NULL;
__thread context_t *CurrentContext = NULL;
__thread const char *CurrentDirectory = NULL;

static int QueuedTargets = 0, BuiltTargets = 0, NumTargets = 0;

static int target_missing(target_t *Target, int LastChecked);

static build_thread_t *BuildThreads = NULL;
__thread build_thread_t *CurrentThread = NULL;
static int RunningThreads = 0, LastThread = 0;

void target_depends_auto(target_t *Depend) {
	if (CurrentTarget && CurrentTarget != Depend) {
		targetset_insert(CurrentTarget->BuildDepends, Depend);
		target_queue(Depend, CurrentTarget);
		target_wait(Depend, CurrentTarget);
	}
}

target_t *target_alloc(int Size, ml_type_t *Type, const char *Id, size_t Index, target_t **Slot) {
	++NumTargets;
	target_t *Target = (target_t *)GC_MALLOC(Size);
	Target->Type = Type;
	Target->Id = Id;
	Target->IdLength = strlen(Id);
	Target->IdHash = stringmap_hash(Id);
	Target->Build = NULL;
	Target->Parent = NULL;
	Target->LastUpdated = STATE_UNCHECKED;
	Target->QueueIndex = -1;
	Target->QueuePriority = PRIORITY_INVALID;
	Target->Depends->Type = TargetSetT;
	Target->Affects->Type = TargetSetT;
	Target->CacheIndex = Index;
	Slot[0] = Target;
	return Target;
}

static int target_depends_single(ml_value_t *Arg, target_t *Target) {
	if (ml_is(Arg, MLListT)) {
		return ml_list_foreach(Arg, Target, (void *)target_depends_single);
	} else if (Arg->Type == MLStringT) {
		target_t *Depend = target_symb_new(CurrentContext, ml_string_value(Arg));
		targetset_insert(Target->Depends, Depend);
		if (CurrentTarget) targetset_insert(Target->BuildDepends, Depend);
		return 0;
	} else if (ml_is(Arg, TargetT)) {
		target_t *Depend = (target_t *)Arg;
		targetset_insert(Target->Depends, Depend);
		if (CurrentTarget) targetset_insert(Target->BuildDepends, Depend);
		return 0;
	} else if (Arg == MLNil) {
		return 0;
	}
	return 1;
}

ML_METHODV("[]", TargetT, MLAnyT) {
//<Target
//<Dependency
//>target
// Adds a number of dependencies to :mini:`Target`.
// * If :mini:`Dependency` is a list then each value is added.
// * If :mini:`Dependency` is a string then a dependency on the corresponding symbol target is added.
// * Otherwise :mini:`Dependency` should be another target.
// Returns :mini:`Target`.
	target_t *Target = (target_t *)Args[0];
	if ((Count > 1) && (Args[Count - 1] != MLNil) && ml_is(Args[Count - 1], MLFunctionT)) {
		Target->Build = Args[Count - 1];
		Target->BuildContext = CurrentContext;
		if (CurrentTarget) {
			Target->Parent = CurrentTarget;
			if (DependencyGraph) {
				fprintf(DependencyGraph, "\tT%" PRIxPTR " -> T%" PRIxPTR " [color=red];\n", (uintptr_t)Target, (uintptr_t)Target->Parent);
			}
		}
		--Count;
	}
	for (int I = 1; I < Count; ++I) {
		int Error = target_depends_single(Args[I], Target);
		if (Error) return ml_error("TypeError", "Invalid value in dependency list");
	}
	return Args[0];
}

ML_METHOD("<<", TargetT, MLAnyT) {
//<Target
//<Dependency
//>target
// Adds a dependency to :mini:`Target`. Equivalent to :mini:`Target[Dependency]`.
	target_t *Target = (target_t *)Args[0];
	for (int I = 1; I < Count; ++I) {
		int Error = target_depends_single(Args[I], Target);
		if (Error) return ml_error("TypeError", "Invalid value in dependency list");
	}
	return Args[0];
}

ML_METHOD("scan", TargetT, MLStringT) {
//<Target
//<Name
//>scan
// Returns a new scan target using :mini:`Target` as the base target.
	return target_scan_new(NULL, Count, Args);
}

ML_METHOD("id", TargetT) {
//<Target
//>string
// Returns the id of :mini:`Target`.
	target_t *Target = (target_t *)Args[0];
	return ml_string(Target->Id, -1);
}

ML_METHOD("build", TargetT) {
//<Target
//>function|nil
// Returns the build function of :mini:`Target` if one has been set, otherwise returns :mini:`nil`.
	target_t *Target = (target_t *)Args[0];
	return Target->Build ?: MLNil;
}

ML_METHOD("context", TargetT) {
//<Target
//>context|nil
// Returns the build context of :mini:`Target` if one has been set, otherwise returns :mini:`nil`.
	target_t *Target = (target_t *)Args[0];
	return (ml_value_t *)Target->BuildContext ?: MLNil;
}

ML_METHOD("=>", TargetT, MLAnyT) {
//<Target
//<Function
//>target
// Sets the build function for :mini:`Target` to :mini:`Function` and returns :mini:`Target`. The current context is also captured.
	target_t *Target = (target_t *)Args[0];
	Target->Build = Args[1];
	Target->BuildContext = CurrentContext;
	if (CurrentTarget) {
		Target->Parent = CurrentTarget;
		if (DependencyGraph) {
			fprintf(DependencyGraph, "\tT%" PRIxPTR " -> T%" PRIxPTR " [color=red];\n", (uintptr_t)Target, (uintptr_t)Target->Parent);
		}
	}
	return Args[0];
}

ML_METHOD("build", TargetT, MLAnyT) {
//<Target
//<Function
//>target
// Sets the build function for :mini:`Target` to :mini:`Function` and returns :mini:`Target`. The current context is also captured.
	target_t *Target = (target_t *)Args[0];
	Target->Build = Args[1];
	Target->BuildContext = CurrentContext;
	if (CurrentTarget) {
		Target->Parent = CurrentTarget;
		if (DependencyGraph) {
			fprintf(DependencyGraph, "\tT%" PRIxPTR " -> T%" PRIxPTR " [color=red];\n", (uintptr_t)Target, (uintptr_t)Target->Parent);
		}
	}
	return Args[0];
}

ML_METHOD("depends", TargetT) {
//<Target
//>targetset
// Returns the set of dependencies of :mini:`Target`.
	target_t *Target = (target_t *)Args[0];
	return (ml_value_t *)Target->Depends;
}

ML_METHOD("cached_depends", TargetT) {
//<Target
//>targetset
// Returns the set of dependencies of :mini:`Target`.
	target_t *Target = (target_t *)Args[0];
	return (ml_value_t *)cache_depends_get(Target);
}

ML_METHOD("affects", TargetT) {
//<Target
//>targetset
// Returns the set of dependencies of :mini:`Target`.
	target_t *Target = (target_t *)Args[0];
	return (ml_value_t *)Target->Affects;
}

ML_METHOD("priority", TargetT) {
//<Target
//>integer
// Returns the computed priority of :mini:`Target`.
	target_t *Target = (target_t *)Args[0];
	return ml_integer(Target->QueuePriority);
}

void target_value_hash(ml_value_t *Value, unsigned char Hash[SHA256_BLOCK_SIZE]) {
	typeof(target_value_hash) *function = ml_typed_fn_get(ml_typeof(Value), target_value_hash);
	if (function) return function(Value, Hash);
	memset(Hash, -1, SHA256_BLOCK_SIZE);
}

void ML_TYPED_FN(target_value_hash, MLNilT, ml_value_t *Value, unsigned char Hash[SHA256_BLOCK_SIZE]) {
	memset(Hash, -1, SHA256_BLOCK_SIZE);
}

void ML_TYPED_FN(target_value_hash, MLIntegerT, ml_value_t *Value, unsigned char Hash[SHA256_BLOCK_SIZE]) {
	memset(Hash, 0, SHA256_BLOCK_SIZE);
	*(long *)Hash = ml_integer_value(Value);
	Hash[SHA256_BLOCK_SIZE - 1] = 1;
}

void ML_TYPED_FN(target_value_hash, MLRealT, ml_value_t *Value, unsigned char Hash[SHA256_BLOCK_SIZE]) {
	memset(Hash, 0, SHA256_BLOCK_SIZE);
	*(double *)Hash = ml_real_value(Value);
	Hash[SHA256_BLOCK_SIZE - 1] = 1;
}

void ML_TYPED_FN(target_value_hash, MLStringT, ml_value_t *Value, unsigned char Hash[SHA256_BLOCK_SIZE]) {
	const char *String = ml_string_value(Value);
	size_t Len = ml_string_length(Value);
	SHA256_CTX Ctx[1];
	sha256_init(Ctx);
	sha256_update(Ctx, (unsigned char *)String, Len);
	sha256_final(Ctx, Hash);
}

void ML_TYPED_FN(target_value_hash, MLListT, ml_value_t *Value, unsigned char Hash[SHA256_BLOCK_SIZE]) {
	SHA256_CTX Ctx[1];
	sha256_init(Ctx);
	ML_LIST_FOREACH(Value, Iter) {
		unsigned char ChildHash[SHA256_BLOCK_SIZE];
		target_value_hash(Iter->Value, ChildHash);
		sha256_update(Ctx, ChildHash, SHA256_BLOCK_SIZE);
	}
	sha256_final(Ctx, Hash);
}

void ML_TYPED_FN(target_value_hash, MLMapT, ml_value_t *Value, unsigned char Hash[SHA256_BLOCK_SIZE]) {
	SHA256_CTX Ctx[1];
	sha256_init(Ctx);
	ML_MAP_FOREACH(Value, Iter) {
		unsigned char ChildHash[SHA256_BLOCK_SIZE];
		target_value_hash(Iter->Key, ChildHash);
		sha256_update(Ctx, ChildHash, SHA256_BLOCK_SIZE);
		target_value_hash(Iter->Value, ChildHash);
		sha256_update(Ctx, ChildHash, SHA256_BLOCK_SIZE);
	}
	sha256_final(Ctx, Hash);
}

void ML_TYPED_FN(target_value_hash, MLClosureT, ml_value_t *Value, unsigned char Hash[SHA256_BLOCK_SIZE]) {
	ml_closure_sha256(Value, Hash);
}

void ML_TYPED_FN(target_value_hash, TargetT, ml_value_t *Value, unsigned char Hash[SHA256_BLOCK_SIZE]) {
	target_t *Target = (target_t *)Value;
	SHA256_CTX Ctx[1];
	sha256_init(Ctx);
	sha256_update(Ctx, (unsigned char *)Target->Id, strlen(Target->Id));
	sha256_final(Ctx, Hash);
}

time_t target_hash(target_t *Target, time_t PreviousTime, unsigned char PreviousHash[SHA256_BLOCK_SIZE], int DependsLastUpdated) {
	if (Target->Type == FileT) return target_file_hash((target_file_t *)Target, PreviousTime, PreviousHash);
	if (Target->Type == MetaT) return target_meta_hash((target_meta_t *)Target, PreviousTime, PreviousHash, DependsLastUpdated);
	if (Target->Type == ScanT) return target_scan_hash((target_scan_t *)Target, PreviousTime, PreviousHash);
	if (Target->Type == SymbolT) return target_symb_hash((target_symb_t *)Target, PreviousTime, PreviousHash);
	if (Target->Type == ExprT) return target_expr_hash((target_expr_t *)Target, PreviousTime, PreviousHash);
	return 0;
}

static int target_missing(target_t *Target, int LastChecked) {
	if (Target->Type == FileT) return target_file_missing((target_file_t *)Target);
	if (Target->Type == ExprT) return target_expr_missing((target_expr_t *)Target);
	return 0;
}

target_t *target_find(const char *Id) {
	target_index_slot R = targetcache_search(Id);
	if (!R.Slot) return NULL;
	if (R.Slot[0]) return R.Slot[0];
	Id = GC_strdup(Id);
	if (!memcmp(Id, "file:", 5)) {
		return target_file_create(Id, CurrentContext, R.Index, R.Slot);
	}
	if (!memcmp(Id, "symb:", 5)) {
		return target_symb_create(Id, CurrentContext, R.Index, R.Slot);
	}
	if (!memcmp(Id, "expr:", 5)) {
		return target_expr_create(Id, CurrentContext, R.Index, R.Slot);
	}
	if (!memcmp(Id, "scan:", 5)) {
		return target_scan_create(Id, CurrentContext, R.Index, R.Slot);
	}
	if (!memcmp(Id, "meta:", 5)) {
		return target_meta_create(Id, CurrentContext, R.Index, R.Slot);
	}
	fprintf(stderr, "internal error: unknown target type: %s\n", Id);
	return NULL;
}

target_t *target_create(const char *Id) {
	target_index_slot R = targetcache_insert(Id);
	if (R.Slot[0]) return R.Slot[0];
	Id = GC_strdup(Id);
	if (!memcmp(Id, "file:", 5)) {
		return target_file_create(Id, CurrentContext, R.Index, R.Slot);
	}
	if (!memcmp(Id, "symb:", 5)) {
		return target_symb_create(Id, CurrentContext, R.Index, R.Slot);
	}
	if (!memcmp(Id, "expr:", 5)) {
		return target_expr_create(Id, CurrentContext, R.Index, R.Slot);
	}
	if (!memcmp(Id, "scan:", 5)) {
		return target_scan_create(Id, CurrentContext, R.Index, R.Slot);
	}
	if (!memcmp(Id, "meta:", 5)) {
		return target_meta_create(Id, CurrentContext, R.Index, R.Slot);
	}
	fprintf(stderr, "internal error: unknown target type: %s\n", Id);
	return NULL;
}

target_t *target_load(const char *Id, size_t Index, target_t **Slot) {
	if (!memcmp(Id, "file:", 5)) {
		return target_file_create(Id, CurrentContext, Index, Slot);
	}
	if (!memcmp(Id, "symb:", 5)) {
		return target_symb_create(Id, CurrentContext, Index, Slot);
	}
	if (!memcmp(Id, "expr:", 5)) {
		return target_expr_create(Id, CurrentContext, Index, Slot);
	}
	if (!memcmp(Id, "scan:", 5)) {
		return target_scan_create(Id, CurrentContext, Index, Slot);
	}
	if (!memcmp(Id, "meta:", 5)) {
		return target_meta_create(Id, CurrentContext, Index, Slot);
	}
	fprintf(stderr, "internal error: unknown target type: %s\n", Id);
	return NULL;
}

/*
static int target_print(target_t *Target, void *Data) {
	printf("%s\n", Target->Id);
	return 0;
}
*/

static int target_affect(target_t *Target, target_t *Depend) {
	--Target->WaitCount;
	if (Target->LastUpdated == STATE_QUEUED && Target->WaitCount == 0) {
		targetqueue_insert(Target);
		pthread_cond_broadcast(TargetAvailable);
	}
	return 0;
}

static int target_depends_fn(target_t *Depend, int *DependsLastUpdated) {
	if (Depend->LastUpdated > *DependsLastUpdated) *DependsLastUpdated = Depend->LastUpdated;
	/*if (Depend->LastUpdated == CurrentIteration) {
		if (StatusUpdates) printf("\tUpdating due to \e[32m%s\e[0m\n", Depend->Id);
	}*/
	return 0;
}

static void target_rebuild(target_t *Target) {
	target_t *Parent;
	if (!Target->Build && (Parent = cache_parent_get(Target))) {
		fprintf(stderr, "\e[34mRebuilding %s because of %s\n\e[0m", Parent->Id, Target->Id);
		target_rebuild(Parent);
	}
	if (Target->Build) {
		target_t *OldTarget = CurrentTarget;
		context_t *OldContext = CurrentContext;
		const char *OldDirectory = CurrentDirectory;

		CurrentContext = Target->BuildContext;
		CurrentTarget = Target;
		CurrentDirectory = CurrentContext ? CurrentContext->FullPath : RootPath;
		ml_value_t *Result = ml_simple_inline(Target->Build, 1, Target);
		if (ml_is_error(Result)) {
			fprintf(stderr, "\e[31mError: %s: %s\n\e[0m", Target->Id, ml_error_message(Result));
			ml_source_t Source;
			int Level = 0;
			while (ml_error_source(Result, Level++, &Source)) {
				fprintf(stderr, "\e[31m\t%s:%d\n\e[0m", Source.Name, Source.Line);
			}
			exit(1);
		}

		CurrentDirectory = OldDirectory;
		CurrentContext = OldContext;
		CurrentTarget = OldTarget;
	}
}

/*
static int target_insert(target_t *Target, targetset_t *Set) {
	targetset_insert(Set, Target);
	return 0;
}
*/

/*
static int target_set_parent(target_t *Target, target_t *Parent) {
	if (!Target->Parent) {
		Target->Parent = Parent;
		if (DependencyGraph) {
			fprintf(DependencyGraph, "\tT%" PRIxPTR " -> T%" PRIxPTR " [color=red];\n", (uintptr_t)Target, (uintptr_t)Target->Parent);
		}
	}
	return 0;
}
*/

/*
static int targetset_print(target_t *Target, void *Data) {
	printf("\t%s\n", Target->Id);
	return 0;
}
*/

static int target_graph_depends(target_t *Depend, target_t *Target) {
	fprintf(DependencyGraph, "\tT%" PRIxPTR " -> T%" PRIxPTR ";\n", (uintptr_t)Target, (uintptr_t)Depend);
	return 0;
}

static int target_graph_build_depends(target_t *Depend, target_t *Target) {
	fprintf(DependencyGraph, "\tT%" PRIxPTR " -> T%" PRIxPTR " [style=dotted];\n", (uintptr_t)Target, (uintptr_t)Depend);
	return 0;
}

static int target_graph_scans(target_t *Depend, target_t *Target) {
	fprintf(DependencyGraph, "\tT%" PRIxPTR " -> T%" PRIxPTR " [style=dashed];\n", (uintptr_t)Target, (uintptr_t)Depend);
	return 0;
}

void display_threads(void) {
	printf("\e[s\e[H\e[K");
	for (build_thread_t *Thread = BuildThreads; Thread; Thread = Thread->Next) {
		if (Thread == CurrentThread) printf("\e[32m");
		switch (Thread->Status) {
		case BUILD_IDLE:
			printf("[%2d] I\n\e[K", Thread->Id);
			break;
		case BUILD_WAIT:
			printf("[%2d] W %6d|%s %.32s\n\e[K", Thread->Id, Thread->Target->QueuePriority, Thread->Target->Id, Thread->Command);
			break;
		case BUILD_EXEC:
			printf("[%2d] X %6d|%s %.32s\n\e[K", Thread->Id, Thread->Target->QueuePriority, Thread->Target->Id, Thread->Command);
			break;
		}
		printf("\e[0m");
	}
	printf("\n\e[u");
}

void display_progress(void) {
	static char *Bars[] = {"", "▏", "▎", "▍", "▌", "▋", "▊", "▉", "█"};
	fputs(" \e[35m▕", stdout);
	double Filled = (64.0 * BuiltTargets) / QueuedTargets;
	int Full = floor(Filled), Partial = (8 * (Filled - Full));
	for (int I = 0; I < Full; ++I) fputs("█", stdout);
	fputs(Bars[Partial], stdout);
	for (int I = Full - !Partial; I < 63; ++I) fputc(' ', stdout);
	printf("▏ %d / %d\e[0m\e[G", BuiltTargets, QueuedTargets);
	fflush(stdout);
	fputs("\e[K", stdout);
}

static int build_scan_target_list(ml_value_t *Depend, targetset_t *Scans) {
	if (ml_is(Depend, MLListT)) {
		ml_list_foreach(Depend, Scans, (void *)build_scan_target_list);
	} else if (ml_is(Depend, TargetT)) {
		targetset_insert(Scans, (target_t *)Depend);
	}
	return 0;
}

static void target_update(target_t *Target) {
	if (DebugThreads) display_threads();
	if (DependencyGraph) {
		fprintf(DependencyGraph, "\tT%" PRIxPTR " [label=\"%s\"];\n", (uintptr_t)Target, Target->Id);
		targetset_foreach(Target->Depends, Target, (void *)target_graph_depends);
	}
	Target->LastUpdated = STATE_CHECKING;
	int DependsLastUpdated = 0;
	unsigned char BuildHash[SHA256_BLOCK_SIZE];
	if (Target->Build) {
		ml_value_sha256(Target->Build, NULL, BuildHash);
		int I = 0;
		for (const unsigned char *P = (unsigned char *)Target->BuildContext->Name; *P; ++P) {
			BuildHash[I] ^= *P;
			I = (I + 1) % SHA256_BLOCK_SIZE;
		}
		unsigned char PreviousBuildHash[SHA256_BLOCK_SIZE];
		cache_build_hash_get(Target, PreviousBuildHash);
		if (memcmp(PreviousBuildHash, BuildHash, SHA256_BLOCK_SIZE)) {
			DependsLastUpdated = CurrentIteration;
			/*ml_closure_list(Target->Build);
			for (int J = 0; J < SHA256_BLOCK_SIZE; ++J) {
				printf(" %02x", (unsigned char)BuildHash[J]);
			}
			puts("");*/
			//if (StatusUpdates) printf("\tUpdating due to build function\n");
		}
	} else {
		memset(BuildHash, 0, SHA256_BLOCK_SIZE);
	}
	targetset_foreach(Target->Depends, Target, (void *)target_wait);
	targetset_foreach(Target->Depends, &DependsLastUpdated, (void *)target_depends_fn);

	unsigned char Previous[SHA256_BLOCK_SIZE];
	int LastUpdated, LastChecked;
	time_t FileTime = 0;
	cache_hash_get(Target, &LastUpdated, &LastChecked, &FileTime, Previous);
	if (DependsLastUpdated <= LastChecked) {
		targetset_t *Depends = cache_depends_get(Target);
		if (Depends) {
			if (DependencyGraph) {
				targetset_foreach(Depends, Target, (void *)target_graph_depends);
			}
			targetset_foreach(Depends, Target, (void *)target_queue);
			targetset_foreach(Depends, Target, (void *)target_wait);
			targetset_foreach(Depends, &DependsLastUpdated, (void *)target_depends_fn);
		}
	}
	if ((DependsLastUpdated > LastChecked) || target_missing(Target, LastChecked)) {
		target_t *Parent;
		if (!Target->Build && (Parent = cache_parent_get(Target))) {
			fprintf(stderr, "\e[34mRebuilding %s because of %s\n\e[0m", Parent->Id, Target->Id);
			target_rebuild(Parent);
			Target->LastUpdated = STATE_UNCHECKED;
			--QueuedTargets;
			target_queue(Target, NULL);
			return;
		}
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
			ml_value_t *Result = ml_simple_inline(Target->Build, 1, Target);
			if (ml_is_error(Result)) {
				fprintf(stderr, "\e[31mError: %s: %s\n\e[0m", Target->Id, ml_error_message(Result));
				ml_source_t Source;
				int Level = 0;
				while (ml_error_source(Result, Level++, &Source)) {
					fprintf(stderr, "\e[31m\t%s:%d\n\e[0m", Source.Name, Source.Line);
				}
				exit(1);
			}
			if (Target->Type == ExprT) {
				((target_expr_t *)Target)->Value = Result;
				cache_expr_set(Target, Result);
			} else if (Target->Type == ScanT) {
				targetset_t Scans[1] = {TARGETSET_INIT};
				if (!ml_is(Result, MLListT)) {
					fprintf(stderr, "\e[31mError: %s: scan results must be a list of targets\n\e[0m", Target->Id);
					exit(1);
				}
				targetset_init(Scans, ml_list_length(Result));
				if (ml_list_foreach(Result, Scans, (void *)build_scan_target_list)) {
					fprintf(stderr, "\e[31mError: %s: scan results must be a list of targets\n\e[0m", Target->Id);
					exit(1);
				}
				targetset_foreach(Scans, Target, (void *)target_queue);
				targetset_foreach(Scans, Target, (void *)target_wait);
				cache_scan_set(Target, Scans);
				if (DependencyGraph) {
					targetset_foreach(Scans, Target, (void *)target_graph_scans);
				}
			}
			cache_depends_set(Target, Target->BuildDepends);
			CurrentDirectory = OldDirectory;
			CurrentContext = OldContext;
			CurrentTarget = OldTarget;
		}
	} else {
		if (Target->Type == ScanT) {
			targetset_t *Scans = cache_scan_get(Target);
			if (DependencyGraph) {
				targetset_foreach(Scans, Target, (void *)target_graph_scans);
			}
			targetset_foreach(Scans, Target, (void *)target_queue);
			targetset_foreach(Scans, Target, (void *)target_wait);
		}
	}
	if (DependencyGraph) {
		targetset_foreach(Target->BuildDepends, Target, (void *)target_graph_build_depends);
	}
	cache_build_hash_set(Target, BuildHash);
	FileTime = target_hash(Target, FileTime, Previous, DependsLastUpdated);
	if (!LastUpdated || memcmp(Previous, Target->Hash, SHA256_BLOCK_SIZE)) {
		Target->LastUpdated = CurrentIteration;
		cache_hash_set(Target, FileTime);
	} else {
		Target->LastUpdated = LastUpdated;
		cache_last_check_set(Target, FileTime);
	}
	++BuiltTargets;
	if (StatusUpdates) printf("\e[35m%d / %d\e[0m #%d Updated \e[32m%s\e[0m to iteration %d\n", BuiltTargets, QueuedTargets, CurrentThread->Id, Target->Id, Target->LastUpdated);
	if (ProgressBar) display_progress();
	pthread_cond_broadcast(TargetUpdated);

	targetset_foreach(Target->Affects, Target, (void *)target_affect);

#ifdef Linux
	if (WatchMode && !Target->Build && Target->Type == FileT) {
		target_file_watch((target_file_t *)Target);
	}
#endif
}

int target_queue(target_t *Target, target_t *Waiter) {
	if (Target->LastUpdated > 0) return 0;
	if (Waiter && targetset_insert(Target->Affects, Waiter)) {
		Waiter->WaitCount += 1;
	}
	if (Target->LastUpdated == STATE_UNCHECKED) {
		Target->LastUpdated = STATE_QUEUED;
		++QueuedTargets;
		targetset_foreach(Target->Depends, Target, (void *)target_queue);
		if (Target->WaitCount == 0) {
			targetqueue_insert(Target);
			pthread_cond_broadcast(TargetAvailable);
		}
	} else if (Target->LastUpdated == STATE_QUEUED) {
		target_priority_invalidate(Target);
	}
	return 0;
}

int target_wait(target_t *Target, target_t *Waiter) {
	if (Target->LastUpdated == STATE_QUEUED) {
		target_update(Target);
	} else while (Target->LastUpdated == STATE_CHECKING) {
		if (DebugThreads) {
			CurrentThread->Status = BUILD_WAIT;
			CurrentThread->Target = Target;
		}
		pthread_cond_wait(TargetUpdated, InterpreterLock);
	}
	if (DebugThreads) {
		CurrentThread->Status = BUILD_EXEC;
		CurrentThread->Target = Waiter;
	}
	return 0;
}

static void *target_thread_fn(void *Arg) {
	GC_add_roots(&CurrentThread, &CurrentThread + 1);
	GC_add_roots(&CurrentDirectory, &CurrentDirectory + 1);
	CurrentThread = (build_thread_t *)Arg;
	char *Path = getcwd(NULL, 0);
	CurrentDirectory = GC_strdup(Path);
	free(Path);
	pthread_mutex_lock(InterpreterLock);
	++RunningThreads;
	for (;;) {
		target_t *NextTarget = targetqueue_next();
		while (!NextTarget) {
			if (DebugThreads) CurrentThread->Status = BUILD_IDLE;
			if (--RunningThreads == 0) {
				pthread_cond_signal(TargetAvailable);
				pthread_mutex_unlock(InterpreterLock);
				return NULL;
			}
			pthread_cond_wait(TargetAvailable, InterpreterLock);
			++RunningThreads;
			NextTarget = targetqueue_next();
		}
		target_t *Target = NextTarget;
		if (Target->LastUpdated == STATE_QUEUED) target_update(Target);
	}
	return NULL;
}

void target_threads_start(int NumThreads) {
	GC_add_roots(&CurrentThread, &CurrentThread + 1);
	GC_add_roots(&CurrentDirectory, &CurrentDirectory + 1);
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
		pthread_create(&BuildThread->Handle, NULL, target_thread_fn, BuildThread);
		BuildThread->Next = BuildThreads;
		BuildThreads = BuildThread;
	}
}

void target_interactive_start(int NumThreads) {
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

void target_threads_wait() {
	--RunningThreads;
	pthread_mutex_unlock(InterpreterLock);
	while (BuildThreads) {
		pthread_join(BuildThreads->Handle, NULL);
		BuildThreads = BuildThreads->Next;
	}
}

static void target_threads_kill(void) {
	for (build_thread_t *Thread = BuildThreads; Thread; Thread = Thread->Next) {
		if (Thread->Child) {
			fprintf(stderr, "\e[31mKilling child process %d\n\e[0m", Thread->Child);
			killpg(Thread->Child, SIGKILL);
			int Status;
			waitpid(Thread->Child, &Status, 0);
		}
	}
}

static void ML_TYPED_FN(ml_cbor_write, TargetT, ml_cbor_writer_t *Writer, target_t *Target) {
	ml_cbor_write_tag(Writer, ML_CBOR_TAG_OBJECT);
	ml_cbor_write_array(Writer, 2);
	ml_cbor_write_string(Writer, strlen("target"));
	ml_cbor_write_raw(Writer, "target", strlen("target"));
	ml_cbor_write_string(Writer, Target->IdLength);
	ml_cbor_write_raw(Writer, Target->Id, Target->IdLength);
}

ML_FUNCTION(DecodeTarget) {
//!internal
	ML_CHECK_ARG_COUNT(1);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	const char *Id = ml_string_value(Args[0]);
	target_t *Target = target_find(Id);
	if (!Target) return ml_error("TargetError", "Target not defined: %s", Id);
	return (ml_value_t *)Target;
}

void target_init(void) {
	targetqueue_init();
	targetset_ml_init();
#ifndef GENERATE_INIT
#include "target_init.c"
#endif
	ml_cbor_default_object("target", (ml_value_t *)DecodeTarget);
	target_expr_init();
	target_file_init();
	target_meta_init();
	target_scan_init();
	target_symb_init();
	atexit(target_threads_kill);
}
