#ifndef TARGET_EXPR_H
#define TARGET_EXPR_H

#include "target.h"
#include "context.h"

typedef struct target_expr_t target_expr_t;

struct target_expr_t {
	TARGET_FIELDS
	ml_value_t *Value;
};

extern ml_type_t *ExprTargetT;

void target_expr_init();

time_t target_expr_hash(target_expr_t *Target, time_t PreviousTime, unsigned char PreviousHash[SHA256_BLOCK_SIZE]);
int target_expr_missing(target_expr_t *Target);

target_t *target_expr_create(const char *Id, context_t *BuildContext, target_t **Slot);
ml_value_t *target_expr_new(void *Data, int Count, ml_value_t **Args);

#endif
