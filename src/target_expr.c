#include "target_expr.h"

#include "rabs.h"
#include "util.h"
#include "cache.h"
#include "targetcache.h"

#undef ML_CATEGORY
#define ML_CATEGORY "expr"

extern ml_value_t *ArgifyMethod;

ML_TYPE(ExprTargetT, (TargetT), "expr-target");
// An expression target represents the a Minilang value that needs to be recomputed whenever other targets change.
// The value of an expression target is the return value of its build function.

ML_METHOD(ArgifyMethod, MLListT, ExprTargetT) {
//!internal
	target_expr_t *Target = (target_expr_t *)Args[1];
	target_depends_auto((target_t *)Target);
	target_queue((target_t *)Target, CurrentTarget);
	target_wait((target_t *)Target, CurrentTarget);
	return ml_simple_inline(ArgifyMethod, 2, Args[0], Target->Value);
}

ML_METHOD("append", MLStringBufferT, ExprTargetT) {
//!internal
	ml_stringbuffer_t *Buffer = (ml_stringbuffer_t *)Args[0];
	target_expr_t *Target = (target_expr_t *)Args[1];
	target_depends_auto((target_t *)Target);
	target_queue((target_t *)Target, CurrentTarget);
	target_wait((target_t *)Target, CurrentTarget);
	return ml_stringbuffer_simple_append(Buffer, Target->Value);
}

time_t target_expr_hash(target_expr_t *Target, time_t PreviousTime, unsigned char PreviousHash[SHA256_BLOCK_SIZE]) {
	target_value_hash(Target->Value, Target->Base.Hash);
	return 0;
}

int target_expr_missing(target_expr_t *Target) {
	Target->Value = cache_expr_get((target_t *)Target);
	return Target->Value == NULL;
}

ML_FUNCTION(Expr) {
//<Name
//>exprtarget
// Returns a new expression target with :mini:`Name`.
	ML_CHECK_ARG_COUNT(1);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	const char *Name = ml_string_value(Args[0]);
	char *Id;
	asprintf(&Id, "expr:%s::%s", CurrentContext->Path, Name);
	target_index_slot R = targetcache_insert(Id);
	//target_t **Slot =
	if (!R.Slot[0]) {
		target_expr_t *Target = target_new(target_expr_t, ExprTargetT, Id, R.Index, R.Slot);
		Target->Value = NULL;
	}
	if (Count > 1) {
		target_t *Target = R.Slot[0];
		Target->Build = Args[1];
		Target->BuildContext = CurrentContext;
	}
	return (ml_value_t *)R.Slot[0];
}

target_t *target_expr_create(const char *Id, context_t *BuildContext, size_t Index, target_t **Slot) {
	target_expr_t *Target = target_new(target_expr_t, ExprTargetT, Id, Index, Slot);
	Target->Base.BuildContext = BuildContext;
	return (target_t *)Target;
}

void target_expr_init(void) {
#include "target_expr_init.c"
}
