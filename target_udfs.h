#ifndef TARGET_UDFS_H
#define TARGET_UDFS_H

#include "target.h"
#include "context.h"

typedef struct target_udfs_t target_udfs_t;

extern ml_type_t *UdfsTargetT;

void target_udfs_init();

time_t target_udfs_hash(target_udfs_t *Target, time_t PreviousTime, unsigned char PreviousHash[SHA256_BLOCK_SIZE]);
int target_udfs_missing(target_udfs_t *Target);

target_t *target_udfs_create(const char *Id, context_t *BuildContext, target_t **Slot);
ml_value_t *target_udfs_new(void *Data, int Count, ml_value_t **Args);

#endif
