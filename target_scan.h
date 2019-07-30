#ifndef TARGET_SCAN_H
#define TARGET_SCAN_H

#include "target.h"
#include "context.h"

typedef struct target_scan_t target_scan_t;

extern ml_type_t *ScanTargetT;

void target_scan_init();

time_t target_scan_hash(target_scan_t *Target, time_t PreviousTime, unsigned char PreviousHash[SHA256_BLOCK_SIZE]);

target_t *target_scan_create(const char *Id, context_t *BuildContext, target_t **Slot);
ml_value_t *target_scan_new(void *Data, int Count, ml_value_t **Args);

#endif
