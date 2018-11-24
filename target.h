#ifndef TARGET_H
#define TARGET_H

#include <time.h>
#include <stdint.h>
#include <pthread.h>
#include "targetset.h"
#include "sha256.h"
#include "minilang.h"

typedef struct target_t target_t;

#define TARGET_FIELDS \
	const ml_type_t *Type; \
	target_t *Next, *Parent; \
	ml_value_t *Build; \
	struct context_t *BuildContext; \
	const char *Id; \
	targetset_t Affects[1]; \
	targetset_t Depends[1]; \
	targetset_t BuildDepends[1]; \
	int WaitCount; \
	int LastUpdated; \
	int IdLength; \
	unsigned long IdHash; \
	BYTE Hash[SHA256_BLOCK_SIZE];

struct target_t {
	TARGET_FIELDS
};

extern int StatusUpdates;
extern int MonitorFiles;
extern int DebugThreads;
extern pthread_mutex_t GlobalLock[1];

void target_init();

ml_value_t *target_dir_new(void *Data, int Count, ml_value_t **Args);
ml_value_t *target_file_new(void *Data, int Count, ml_value_t **Args);
ml_value_t *target_meta_new(void *Data, int Count, ml_value_t **Args);
ml_value_t *target_expr_new(void *Data, int Count, ml_value_t **Args);
ml_value_t *target_depends_auto_value(void *Data, int Count, ml_value_t **Args);

target_t *target_symb_new(const char *Name);
void target_symb_update(const char *Name);

void target_depends_add(target_t *Target, target_t *Depend);
void target_depends_auto(target_t *Depend);
target_t *target_find(const char *Id);
target_t *target_get(const char *Id);
void target_push(target_t *Target);
target_t *target_file_check(const char *Path, int Absolute);
void target_threads_start(int NumThreads);
void target_threads_wait(target_t *Target);
void target_interactive_start(int NumThreads);

int target_wait(target_t *Target, target_t *Waiter);
int target_queue(target_t *Target, target_t *Parent);

#endif
