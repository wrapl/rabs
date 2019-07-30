#include "target_expr.h"

#include "rabs.h"
#include "util.h"
#include "cache.h"
#include "targetcache.h"

extern ml_value_t *AppendMethod;
extern ml_value_t *ArgifyMethod;
extern ml_value_t *CmdifyMethod;
extern ml_value_t *StringMethod;

ml_type_t *ExprTargetT;

static ml_value_t *target_expr_stringify(void *Data, int Count, ml_value_t **Args) {
	ml_stringbuffer_t *Buffer = (ml_stringbuffer_t *)Args[0];
	target_expr_t *Target = (target_expr_t *)Args[1];
	target_depends_auto((target_t *)Target);
	target_queue((target_t *)Target, CurrentTarget);
	target_wait((target_t *)Target, CurrentTarget);
	return ml_inline(AppendMethod, 2, Buffer, Target->Value);
}

static ml_value_t *target_expr_argify(void *Data, int Count, ml_value_t **Args) {
	target_expr_t *Target = (target_expr_t *)Args[1];
	target_depends_auto((target_t *)Target);
	target_queue((target_t *)Target, CurrentTarget);
	target_wait((target_t *)Target, CurrentTarget);
	return ml_inline(ArgifyMethod, 2, Args[0], Target->Value);
}

static ml_value_t *target_expr_cmdify(void *Data, int Count, ml_value_t **Args) {
	ml_stringbuffer_t *Buffer = (ml_stringbuffer_t *)Args[0];
	target_expr_t *Target = (target_expr_t *)Args[1];
	target_depends_auto((target_t *)Target);
	target_queue((target_t *)Target, CurrentTarget);
	target_wait((target_t *)Target, CurrentTarget);
	return ml_inline(AppendMethod, 2, Buffer, Target->Value);
}

static ml_value_t *target_expr_to_string(void *Data, int Count, ml_value_t **Args) {
	target_expr_t *Target = (target_expr_t *)Args[0];
	target_depends_auto((target_t *)Target);
	target_queue((target_t *)Target, CurrentTarget);
	target_wait((target_t *)Target, CurrentTarget);
	return ml_inline(StringMethod, 1, Target->Value);
}

time_t target_expr_hash(target_expr_t *Target, time_t PreviousTime, unsigned char PreviousHash[SHA256_BLOCK_SIZE]) {
	target_value_hash(Target->Value, Target->Hash);
	return 0;
}

int target_expr_missing(target_expr_t *Target) {
	Target->Value = cache_expr_get((target_t *)Target);
	return Target->Value == NULL;
}

ml_value_t *target_expr_new(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	const char *Name = ml_string_value(Args[0]);
	const char *Id = concat("expr:", CurrentContext->Path, "::", Name, NULL);
	target_t **Slot = targetcache_lookup(Id);
	if (!Slot[0]) {
		target_expr_t *Target = target_new(target_expr_t, ExprTargetT, Id, Slot);
		Slot[0] = (target_t *)Target;
	}
	return (ml_value_t *)Slot[0];
}

target_t *target_expr_create(const char *Id, context_t *BuildContext, target_t **Slot) {
	target_expr_t *Target = target_new(target_expr_t, ExprTargetT, Id, Slot);
	Target->BuildContext = BuildContext;
	return (target_t *)Target;
}

void target_expr_init() {
	ExprTargetT = ml_type(TargetT, "expr-target");
	ml_method_by_value(AppendMethod, 0, target_expr_stringify, MLStringBufferT, ExprTargetT, NULL);
	ml_method_by_value(ArgifyMethod, 0, target_expr_argify, MLListT, ExprTargetT, NULL);
	ml_method_by_value(CmdifyMethod, 0, target_expr_cmdify, MLStringBufferT, ExprTargetT, NULL);
	ml_method_by_name("string", 0, target_expr_to_string, ExprTargetT, NULL);

}
