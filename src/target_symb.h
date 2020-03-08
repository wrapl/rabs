#ifndef TARGET_SYMB_H
#define TARGET_SYMB_H

#include "target.h"
#include "context.h"

typedef struct target_symb_t target_symb_t;

extern ml_type_t *SymbTargetT;

void target_symb_init();

time_t target_symb_hash(target_symb_t *Target, time_t PreviousTime, unsigned char PreviousHash[SHA256_BLOCK_SIZE]);

target_t *target_symb_create(const char *Id, context_t *BuildContext, size_t Index, target_t **Slot);
target_t *target_symb_new(context_t *Context, const char *Name);

#endif
