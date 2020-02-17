#ifndef TARGET_FILE_H
#define TARGET_FILE_H

#include "target.h"
#include "context.h"

typedef struct target_file_t target_file_t;

extern ml_type_t *FileTargetT;

void target_file_init();

time_t target_file_hash(target_file_t *Target, time_t PreviousTime, unsigned char PreviousHash[SHA256_BLOCK_SIZE]);
int target_file_missing(target_file_t *Target);
void target_file_watch(target_file_t *Target);

target_t *target_file_create(const char *Id, context_t *BuildContext, size_t Index, target_t **Slot);
ml_value_t *target_file_new(void *Data, int Count, ml_value_t **Args);

#endif
