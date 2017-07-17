#ifndef TARGET_H
#define TARGET_H

#include <time.h>
#include <stdint.h>
#include "stringmap.h"
#include "sha256.h"
#include "minilang.h"

typedef struct target_t target_t;

#define TARGET_FIELDS \
	const ml_type_t *Type; \
	ml_value_t *Build; \
	int LastUpdated; \
	struct context_t *BuildContext; \
	const char *Id; \
	stringmap_t Depends[1]; \
	int8_t Hash[SHA256_BLOCK_SIZE];

struct target_t {
	TARGET_FIELDS
};

void target_init();

ml_value_t *target_dir_new(ml_t *ML, void *Data, int Count, ml_value_t **Args);
ml_value_t *target_file_new(ml_t *ML, void *Data, int Count, ml_value_t **Args);
ml_value_t *target_meta_new(ml_t *ML, void *Data, int Count, ml_value_t **Args);

target_t *target_symb_new(const char *Name);

void target_depends_add(target_t *Target, target_t *Depend);
void target_update(target_t *Target);
void target_query(target_t *Target);
void target_depends_auto(target_t *Depend);
target_t *target_find(const char *Id);
target_t *target_get(const char *Id);
void target_push(target_t *Target);
void target_list();

#endif
