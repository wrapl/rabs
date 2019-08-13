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
#include <regex.h>
#include <dirent.h>
#include <ml_file.h>
#include <limits.h>

#ifdef Linux
#include "targetwatch.h"
#endif

#ifndef Mingw
#include <sys/wait.h>
#endif

enum {
	STATE_UNCHECKED = 0,
	STATE_CHECKING = -1,
	STATE_QUEUED = -2
};

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

static int target_missing(target_t *Target, int LastChecked);

static build_thread_t *BuildThreads = 0;
__thread build_thread_t *CurrentThread = 0;
static int RunningThreads = 0, LastThread = 0;

void target_depends_auto(target_t *Depend) {
	if (CurrentTarget && CurrentTarget != Depend) {
		targetset_insert(CurrentTarget->BuildDepends, Depend);
		target_queue(Depend, CurrentTarget);
		target_wait(Depend, CurrentTarget);
	}
}

target_t *target_alloc(int Size, ml_type_t *Type, const char *Id, target_t **Slot) {
	++NumTargets;
	target_t *Target = (target_t *)GC_MALLOC(Size);
	Target->Type = Type;
	Target->Id = Id;
	Target->IdLength = strlen(Id);
	Target->IdHash = stringmap_hash(Id);
	Target->Build = 0;
	Target->Parent = 0;
	Target->LastUpdated = STATE_UNCHECKED;
	Target->QueueIndex = -1;
	Target->QueuePriority = PRIORITY_INVALID;
	Target->Depends->Type = TargetSetT;
	Target->Affects->Type = TargetSetT;
	Slot[0] = Target;
	return Target;
}

static int target_depends_single(ml_value_t *Arg, target_t *Target) {
	if (Arg->Type == MLListT) {
		return ml_list_foreach(Arg, Target, (void *)target_depends_single);
	} else if (Arg->Type == MLStringT) {
		target_t *Depend = target_symb_new(ml_string_value(Arg));
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

ml_value_t *target_depend(void *Data, int Count, ml_value_t **Args) {
	target_t *Target = (target_t *)Args[0];
	for (int I = 1; I < Count; ++I) {
		int Error = target_depends_single(Args[I], Target);
		if (Error) return ml_error("TypeError", "Invalid value in dependency list");
	}
	return Args[0];
}

ml_value_t *target_get_id(void *Data, int Count, ml_value_t **Args) {
	target_t *Target = (target_t *)Args[0];
	return ml_string(Target->Id, -1);
}

ml_value_t *target_get_build(void *Data, int Count, ml_value_t **Args) {
	target_t *Target = (target_t *)Args[0];
	return Target->Build ?: MLNil;
}

ml_value_t *target_set_build(void *Data, int Count, ml_value_t **Args) {
	target_t *Target = (target_t *)Args[0];
	Target->Build = Args[1];
	Target->BuildContext = CurrentContext;
	return Args[0];
}

ml_value_t *target_get_depends(void *Data, int Count, ml_value_t **Args) {
	target_t *Target = (target_t *)Args[0];
	return (ml_value_t *)Target->Depends;
}

ml_value_t *target_get_affects(void *Data, int Count, ml_value_t **Args) {
	target_t *Target = (target_t *)Args[0];
	return (ml_value_t *)Target->Affects;
}

ml_value_t *target_get_priority(void *Data, int Count, ml_value_t **Args) {
	target_t *Target = (target_t *)Args[0];
	return ml_integer(Target->QueuePriority);
}

static int target_depends_auto_single(ml_value_t *Arg, void *Data) {
	if (Arg->Type == MLListT) {
		return ml_list_foreach(Arg, 0, (void *)target_depends_auto_single);
	} else if (Arg->Type == MLStringT) {
		target_t *Depend = target_symb_new(ml_string_value(Arg));
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

static int map_update_hash(ml_value_t *Key, ml_value_t *Value, SHA256_CTX *Ctx) {
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
		sha256_update(Ctx, (unsigned char *)String, Len);
		sha256_final(Ctx, Hash);
	} else if (Value->Type == MLListT) {
		SHA256_CTX Ctx[1];
		sha256_init(Ctx);
		ml_list_foreach(Value, Ctx, (void *)list_update_hash);
		sha256_final(Ctx, Hash);
	} else if (Value->Type == MLMapT) {
		SHA256_CTX Ctx[1];
		sha256_init(Ctx);
		ml_map_foreach(Value, Ctx, (void *)map_update_hash);
		sha256_final(Ctx, Hash);
	} else if (Value->Type == MLClosureT) {
		ml_closure_sha256(Value, Hash);
	} else if (ml_is(Value, TargetT)) {
		target_t *Target = (target_t *)Value;
		SHA256_CTX Ctx[1];
		sha256_init(Ctx);
		sha256_update(Ctx, (unsigned char *)Target->Id, strlen(Target->Id));
		sha256_final(Ctx, Hash);
	} else {
		memset(Hash, -1, SHA256_BLOCK_SIZE);
	}
}

time_t target_hash(target_t *Target, time_t PreviousTime, unsigned char PreviousHash[SHA256_BLOCK_SIZE], int DependsLastUpdated) {
	if (Target->Type == FileTargetT) return target_file_hash((target_file_t *)Target, PreviousTime, PreviousHash);
	if (Target->Type == MetaTargetT) return target_meta_hash((target_meta_t *)Target, PreviousTime, PreviousHash, DependsLastUpdated);
	if (Target->Type == ScanTargetT) return target_scan_hash((target_scan_t *)Target, PreviousTime, PreviousHash);
	if (Target->Type == SymbTargetT) return target_symb_hash((target_symb_t *)Target, PreviousTime, PreviousHash);
	if (Target->Type == ExprTargetT) return target_expr_hash((target_expr_t *)Target, PreviousTime, PreviousHash);
	return 0;
}

static int target_missing(target_t *Target, int LastChecked) {
	if (Target->Type == FileTargetT) return target_file_missing((target_file_t *)Target);
	if (Target->Type == ExprTargetT) return target_expr_missing((target_expr_t *)Target);
	return 0;
}

target_t *target_find(const char *Id) {
	target_t **Slot = targetcache_lookup(Id);
	if (Slot[0]) return Slot[0];
	Id = GC_strdup(Id);
	if (!memcmp(Id, "file:", 5)) {
		return target_file_create(Id, CurrentContext, Slot);
	}
	if (!memcmp(Id, "symb:", 5)) {
		return target_symb_create(Id, CurrentContext, Slot);
	}
	if (!memcmp(Id, "expr:", 5)) {
		return target_expr_create(Id, CurrentContext, Slot);
	}
	if (!memcmp(Id, "scan:", 5)) {
		return target_scan_create(Id, CurrentContext, Slot);
	}
	if (!memcmp(Id, "meta:", 5)) {
		return target_meta_create(Id, CurrentContext, Slot);
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

int target_affect(target_t *Target, target_t *Depend) {
	--Target->WaitCount;
	if (Target->LastUpdated == STATE_QUEUED && Target->WaitCount == 0) {
		targetqueue_insert(Target);
		pthread_cond_broadcast(TargetAvailable);
	}
	return 0;
}

static int target_depends_fn(target_t *Depend, int *DependsLastUpdated) {
	if (Depend->LastUpdated > *DependsLastUpdated) *DependsLastUpdated = Depend->LastUpdated;
	if (Depend->LastUpdated == CurrentIteration) {
		if (StatusUpdates) printf("\tUpdating due to \e[32m%s\e[0m\n", Depend->Id);
	}
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

int target_find_leaves0(target_t *Target, target_t *Parent);

int target_find_leaves(target_t *Target, target_t *Parent) {
	if (Target->Parent == Parent) targetset_insert(Parent->BuildDepends, Target);
	targetset_foreach(Target->Depends, Parent, (void *)target_find_leaves);
	targetset_foreach(Target->BuildDepends, Parent, (void *)target_find_leaves0);
	return 0;
}

int target_find_leaves0(target_t *Target, target_t *Parent) {
	if (Target->Parent == Parent) targetset_insert(Parent->BuildDepends, Target);
	targetset_foreach(Target->Depends, Parent, (void *)target_find_leaves);
	targetset_foreach(Target->BuildDepends, Parent, (void *)target_find_leaves0);
	return 0;
}

int target_insert(target_t *Target, targetset_t *Set) {
	targetset_insert(Set, Target);
	return 0;
}

int target_set_parent(target_t *Target, target_t *Parent) {
	if (!Target->Parent) {
		Target->Parent = Parent;
		if (DependencyGraph) {
			fprintf(DependencyGraph, "\tT%lx -> T%lx [color=red];\n", (uintptr_t)Target, (uintptr_t)Target->Parent);
		}
	}
	return 0;
}

int targetset_print(target_t *Target, void *Data) {
	printf("\t%s\n", Target->Id);
	return 0;
}

static int target_graph_depends(target_t *Depend, target_t *Target) {
	fprintf(DependencyGraph, "\tT%lx -> T%lx;\n", (uintptr_t)Target, (uintptr_t)Depend);
	return 0;
}

static int target_graph_build_depends(target_t *Depend, target_t *Target) {
	fprintf(DependencyGraph, "\tT%lx -> T%lx [style=dotted];\n", (uintptr_t)Target, (uintptr_t)Depend);
	return 0;
}

static int target_graph_scans(target_t *Depend, target_t *Target) {
	fprintf(DependencyGraph, "\tT%lx -> T%lx [style=dashed];\n", (uintptr_t)Target, (uintptr_t)Depend);
	return 0;
}

void display_threads() {
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

static int build_scan_target_list(ml_value_t *Depend, targetset_t *Scans) {
	if (Depend->Type == MLListT) {
		ml_list_foreach(Depend, Scans, (void *)build_scan_target_list);
	} else if (ml_is((ml_value_t *)Depend, TargetT)) {
		targetset_insert(Scans, (target_t *)Depend);
	}
	return 0;
}

void target_update(target_t *Target) {
	if (DebugThreads) display_threads();
	if (DependencyGraph) {
		fprintf(DependencyGraph, "\tT%lx [label=\"%s\"];\n", (uintptr_t)Target, Target->Id);
		targetset_foreach(Target->Depends, Target, (void *)target_graph_depends);
	}
	Target->LastUpdated = STATE_CHECKING;
	int DependsLastUpdated = 0;
	unsigned char PreviousBuildHash[SHA256_BLOCK_SIZE];
	unsigned char BuildHash[SHA256_BLOCK_SIZE];
	if (Target->Build && Target->Build->Type == MLClosureT) {
		ml_closure_sha256(Target->Build, BuildHash);
		int I = 0;
		for (const unsigned char *P = (unsigned char *)Target->BuildContext->Path; *P; ++P) {
			BuildHash[I] ^= *P;
			I = (I + 1) % SHA256_BLOCK_SIZE;
		}
		cache_build_hash_get(Target, PreviousBuildHash);
		if (memcmp(PreviousBuildHash, BuildHash, SHA256_BLOCK_SIZE)) {
			DependsLastUpdated = CurrentIteration;
		}
	} else {
		memset(BuildHash, 0, sizeof(SHA256_BLOCK_SIZE));
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
			if (Target->Type == ScanTargetT) {
				targetset_foreach(Depends, Target, (void *)target_set_parent);
			} else if (Target->Parent) {
				targetset_foreach(Depends, Target->Parent, (void *)target_set_parent);
			}
			targetset_foreach(Depends, Target, (void *)target_queue);
			targetset_foreach(Depends, Target, (void *)target_wait);
			targetset_foreach(Depends, &DependsLastUpdated, (void *)target_depends_fn);
		}
	}

	if ((DependsLastUpdated > LastChecked) || target_missing(Target, LastChecked)) {
		if (!Target->Build && Target->Parent) {
			fprintf(stderr, "\e[34mRebuilding %s because of %s\n\e[0m", Target->Parent->Id, Target->Id);
			target_rebuild(Target->Parent);
			Target->Parent = 0;
			Target->LastUpdated = STATE_UNCHECKED;
			--QueuedTargets;
			target_queue(Target, 0);
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
				if (Result->Type != MLListT) {
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
			cache_build_hash_set(Target, BuildHash);

			CurrentDirectory = OldDirectory;
			CurrentContext = OldContext;
			CurrentTarget = OldTarget;
		}
	} else {
		if (Target->Type == ScanTargetT) {
			targetset_t *Scans = cache_scan_get(Target);
			if (DependencyGraph) {
				targetset_foreach(Scans, Target, (void *)target_graph_scans);
			}
			targetset_foreach(Scans, Target, (void *)target_set_parent);
			targetset_foreach(Scans, Target, (void *)target_queue);
			targetset_foreach(Scans, Target, (void *)target_wait);
		}
	}
	if (DependencyGraph) {
		targetset_foreach(Target->BuildDepends, Target, (void *)target_graph_build_depends);
	}
	FileTime = target_hash(Target, FileTime, Previous, DependsLastUpdated);
	if (!LastUpdated || memcmp(Previous, Target->Hash, SHA256_BLOCK_SIZE)) {
		Target->LastUpdated = CurrentIteration;
		cache_hash_set(Target, FileTime);
		cache_depends_set(Target, Target->BuildDepends);
	} else {
		Target->LastUpdated = LastUpdated;
		cache_last_check_set(Target, FileTime);
	}
	++BuiltTargets;
	if (StatusUpdates) printf("\e[35m%d / %d\e[0m #%d Updated \e[32m%s\e[0m to iteration %d\n", BuiltTargets, QueuedTargets, CurrentThread->Id, Target->Id, Target->LastUpdated);
	pthread_cond_broadcast(TargetUpdated);

	targetset_foreach(Target->Affects, Target, (void *)target_affect);

#ifdef Linux
	if (WatchMode && !Target->Build && Target->Type == FileTargetT) {
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
				return 0;
			}
			pthread_cond_wait(TargetAvailable, InterpreterLock);
			++RunningThreads;
			NextTarget = targetqueue_next();
		}
		target_t *Target = NextTarget;
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

void target_threads_kill() {
	for (build_thread_t *Thread = BuildThreads; Thread; Thread = Thread->Next) {
		if (Thread->Child) {
			fprintf(stderr, "\e[31mKilling child process %d\n\e[0m", Thread->Child);
			killpg(Thread->Child, SIGKILL);
			int Status;
			waitpid(Thread->Child, &Status, 0);
		}
	}
}

void target_init() {
	targetqueue_init();

	TargetT = ml_type(MLAnyT, "target");
	SHA256Method = ml_method("sha256");
	MissingMethod = ml_method("missing");
	ml_method_by_name("[]", 0, target_depend, TargetT, MLAnyT, NULL);
	ml_method_by_name("scan", 0, target_scan_new, TargetT, NULL);
	ml_method_by_name("=>", 0, target_set_build, TargetT, MLAnyT, NULL);
	ml_method_by_name("id", 0, target_get_id, TargetT, NULL);
	ml_method_by_name("build", 0, target_get_build, TargetT, NULL);
	ml_method_by_name("build", 0, target_set_build, TargetT, MLAnyT, NULL);
	ml_method_by_name("depends", 0, target_get_depends, TargetT, NULL);
	ml_method_by_name("affects", 0, target_get_affects, TargetT, NULL);
	ml_method_by_name("priority", 0, target_get_priority, TargetT, NULL);
	target_expr_init();
	target_file_init();
	target_meta_init();
	target_scan_init();
	target_symb_init();
	atexit(target_threads_kill);
}
