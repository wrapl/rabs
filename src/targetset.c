#include "targetset.h"
#include <gc/gc.h>
#include "target.h"
#include "ml_macros.h"

#undef ML_CATEGORY
#define ML_CATEGORY "targetset"

#define INITIAL_SIZE 4

typedef struct {
	const ml_type_t *Type;
	target_t **Current, **End;
	int Index;
} targetset_iter_t;

static void ML_TYPED_FN(ml_iter_key, TargetSetIterT, ml_state_t *Caller, targetset_iter_t *Iter) {
	ML_CONTINUE(Caller, ml_integer(Iter->Index));
}

static void ML_TYPED_FN(ml_iter_value, TargetSetIterT, ml_state_t *Caller, targetset_iter_t *Iter) {
	ML_CONTINUE(Caller, Iter->Current[0]);
}

static void ML_TYPED_FN(ml_iter_next, TargetSetIterT, ml_state_t *Caller, targetset_iter_t *Iter) {
	for (target_t **Current = Iter->Current + 1; Current < Iter->End; ++Current) {
		if (*Current) {
			Iter->Current = Current;
			++Iter->Index;
			ML_CONTINUE(Caller, Iter);
		}
	}
	ML_CONTINUE(Caller, MLNil);
}

ML_TYPE(TargetSetIterT, (), "targetset-iter");

static void ML_TYPED_FN(ml_iterate, TargetSetT, ml_state_t *Caller, targetset_t *Set) {
	target_t **End = Set->Targets + Set->Size;
	for (target_t **T = Set->Targets; T < End; ++T) {
		if (*T) {
			targetset_iter_t *Iter = new(targetset_iter_t);
			Iter->Type = TargetSetIterT;
			Iter->Current = T;
			Iter->End = End;
			Iter->Index = 1;
			ML_CONTINUE(Caller, Iter);
		}
	}
	ML_CONTINUE(Caller, MLNil);
}

ML_TYPE(TargetSetT, (MLSequenceT), "targetset");

void targetset_ml_init() {
#include "targetset_init.c"
}

targetset_t *targetset_new() {
	targetset_t *Set = new(targetset_t);
	Set->Type = TargetSetT;
	return Set;
}

void targetset_init(targetset_t *Set, int Min) {
	int Size = 1;
	while (Size < Min) Size <<= 1;
	Set->Size = Size;
	Set->Space = Size;
	Set->Targets = anew(target_t *, Size + 1);
}

static void sort_targets(target_t **First, target_t **Last) {
	target_t **A = First;
	target_t **B = Last;
	target_t *T = *A;
	target_t *P = *B;
	while (!P) {
		--B;
		--Last;
		if (A == B) return;
		P = *B;
	}
	while (A != B) {
		if (T > P) {
			*A = T;
			T = *++A;
		} else {
			*B = T;
			T = *--B;
		}
	}
	*A = P;
	if (First < A - 1) sort_targets(First, A - 1);
	if (A + 1 < Last) sort_targets(A + 1, Last);
}

int targetset_insert(targetset_t *Set, target_t *Target) {
	int Hash = ((intptr_t)Target) >> 4;
	int Incr = (((intptr_t)Target) >> 8) | 1;
	target_t **Targets = Set->Targets;
	if (!Targets) {
		Targets = Set->Targets = anew(target_t *, INITIAL_SIZE + 1);
		Set->Size = INITIAL_SIZE;
		Set->Space = INITIAL_SIZE - 1;
		Targets[Hash & (INITIAL_SIZE - 1)] = Target;
		return 1;
	}
	int Mask = Set->Size - 1;
	int Index = Hash & Mask;
	for (;;) {
		if (Targets[Index] == Target) return 0;
		if (Targets[Index] < Target) break;
		Index += Incr;
		Index &= Mask;
	}
	if (--Set->Space > (Set->Size >> 3)) {
		target_t *OldTarget = Targets[Index];
		Targets[Index] = Target;
		while (OldTarget) {
			Target = OldTarget;
			Incr = (((intptr_t)Target) >> 8) | 1;
			Index += Incr;
			Index &= Mask;
			for (;;) {
				if (Targets[Index] < Target) {
					OldTarget = Targets[Index];
					Targets[Index] = Target;
					break;
				}
				Index += Incr;
				Index &= Mask;
			}
		}
		return 1;
	}
	int NewSize = Set->Size * 2;
	target_t **NewTargets = anew(target_t *, NewSize + 1);
	Targets[Set->Size] = Target;
	Mask = NewSize - 1;
	sort_targets(Targets, Targets + Set->Size);
	for (target_t **Old = Targets; *Old; ++Old) {
		Target = *Old;
		Hash = ((intptr_t)Target) >> 4;
		Incr = (((intptr_t)Target) >> 8) | 1;
		Index = Hash & Mask;
		while (NewTargets[Index]) {
			Index += Incr;
			Index &= Mask;
		}
		NewTargets[Index] = Target;
	}
	Set->Space += Set->Size;
	Set->Size = NewSize;
	Set->Targets = NewTargets;
	return 1;
}

int targetset_foreach(targetset_t *Set, void *Data, int (*callback)(target_t *, void *)) {
	target_t **End = Set->Targets + Set->Size;
	for (target_t **T = Set->Targets; T < End; ++T) {
		if (*T && callback(*T, Data)) return 1;
	}
	return 0;
}
