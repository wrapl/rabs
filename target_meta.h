#ifndef TARGET_META_H
#define TARGET_META_H

#include "target.h"
#include "context.h"

typedef struct target_meta_t target_meta_t;

extern ml_type_t *MetaTargetT;

void target_meta_init();

time_t target_meta_hash(target_meta_t *Target, time_t PreviousTime, unsigned char PreviousHash[SHA256_BLOCK_SIZE], int DependsLastUpdated);

target_t *target_meta_create(const char *Id, context_t *BuildContext, target_t **Slot);
ml_value_t *target_meta_new(void *Data, int Count, ml_value_t **Args);

#endif
