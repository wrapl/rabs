#ifndef TARGET_H
#define TARGET_H

#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include "targetset.h"
#include "sha256.h"
#include "minilang.h"

#define INVALID_TARGET 0xFFFFFFFF

struct target_t {
	const ml_type_t *Type;
	target_t *PriorityAdjustNext, *Parent;
	ml_value_t *Build;
	struct context_t *BuildContext;
	const char *Id;
	targetset_t Affects[1];
	targetset_t Depends[1];
	targetset_t BuildDepends[1];
	size_t CacheIndex;
	int WaitCount;
	int LastUpdated;
	int IdLength;
	int QueueIndex, QueuePriority;
	unsigned long IdHash;
	unsigned char Hash[SHA256_BLOCK_SIZE];
};

target_t *target_alloc(int Size, ml_type_t *Type, const char *Id, size_t Index, target_t **Slot);
#define target_new(type, Type, Id, Index, Slot) ((type *)target_alloc(sizeof(type), Type, Id, Index, Slot))

extern int StatusUpdates;
extern int ProgressBar;
extern int MonitorFiles;
extern int DebugThreads;
extern int WatchMode;
extern FILE *DependencyGraph;
extern pthread_mutex_t InterpreterLock[1];
extern ml_type_t TargetT[];

void target_init();

time_t target_hash(target_t *Target, time_t PreviousTime, unsigned char PreviousHash[SHA256_BLOCK_SIZE], int DependsLastUpdated);
void target_value_hash(ml_value_t *Value, unsigned char Hash[SHA256_BLOCK_SIZE]);

ml_value_t *target_dir_new(void *Data, int Count, ml_value_t **Args);
ml_value_t *target_file_new(void *Data, int Count, ml_value_t **Args);
ml_value_t *target_meta_new(void *Data, int Count, ml_value_t **Args);
ml_value_t *target_expr_new(void *Data, int Count, ml_value_t **Args);
ml_value_t *target_depends_auto_value(void *Data, int Count, ml_value_t **Args);

void target_depends_add(target_t *Target, target_t *Depend);
void target_depends_auto(target_t *Depend);
target_t *target_find(const char *Id);
target_t *target_create(const char *Id);
target_t *target_load(const char *Id, size_t Index, target_t **Slot);
void target_push(target_t *Target);
target_t *target_file_check(const char *Path, int Absolute);
void target_threads_start(int NumThreads);
void target_threads_wait(target_t *Target);
void target_interactive_start(int NumThreads);

int target_wait(target_t *Target, target_t *Waiter);
int target_queue(target_t *Target, target_t *Parent);

void display_threads();

#endif
