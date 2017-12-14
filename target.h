#ifndef TARGET_H
#define TARGET_H

#include <time.h>
#include <stdint.h>
#include <pthread.h>
#include "stringmap.h"
#include "sha256.h"
#include "minilang.h"

typedef struct target_t target_t;

#define TARGET_FIELDS \
	const ml_type_t *Type; \
	target_t *Next; \
	ml_value_t *Build; \
	struct context_t *BuildContext; \
	const char *Id; \
	stringmap_t Depends[1]; \
	stringmap_t Targets[1]; \
	int WaitCount, DependsLastUpdated, LastUpdated; \
	int8_t Hash[SHA256_BLOCK_SIZE];

struct target_t {
	TARGET_FIELDS
};

void target_init();

ml_value_t *target_dir_new(void *Data, int Count, ml_value_t **Args);
ml_value_t *target_file_new(void *Data, int Count, ml_value_t **Args);
ml_value_t *target_meta_new(void *Data, int Count, ml_value_t **Args);
ml_value_t *target_expr_new(void *Data, int Count, ml_value_t **Args);

target_t *target_symb_new(const char *Name);

void target_depends_add(target_t *Target, target_t *Depend);
void target_update(target_t *Target);
void target_query(target_t *Target);
void target_depends_auto(target_t *Depend);
target_t *target_find(const char *Id);
target_t *target_get(const char *Id);
void target_push(target_t *Target);
void target_list();
target_t *target_file_check(const char *Path, int Absolute);
void target_threads_start(int NumThreads);
void target_threads_wait(int NumThreads);

#endif
