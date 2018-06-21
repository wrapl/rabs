#include "targetset.h"
#include <gc/gc.h>
#include "target.h"

static void sort_targets(target_t **First, target_t **Last) {
	target_t **A = First;
	target_t **B = Last;
	target_t *P = *A;
	target_t *T = *B;
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
	int Hash = ((intptr_t)Target) >> 8;
	int Incr = (((intptr_t)Target) >> 16) | 1;
	target_t **Targets = Set->Targets;
	if (!Targets) {
		Targets = Set->Targets = anew(target_t *, 5);
		Set->Size = 4;
		Set->Space = 3;
		Targets[Hash & 3] = Target;
		return 0;
	}
	int Mask = Set->Size - 1;
	int Index = Hash & Mask;
	for (;;) {
		if (Targets[Index] == Target) return 1;
		if (Targets[Index] < Target) break;
		Index += Incr;
		Index &= Mask;
	}
	if (--Set->Space > (Set->Space >> 3)) {
		target_t *OldTarget = Targets[Index];
		Targets[Index] = Target;
		while (OldTarget) {
			Target = OldTarget;
			Incr = (((intptr_t)Target) >> 16) | 1;
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
		return 0;
	}
	int NewSize = Set->Size * 2;
	target_t **NewTargets = anew(target_t *, NewSize + 1);
	Targets[Set->Size] = Target;
	Mask = NewSize - 1;
	sort_targets(Targets, Targets + Set->Size);
	for (target_t **Old = Targets; *Old; ++Old) {
		Target = *Old;
		Hash = ((intptr_t)Target) >> 8;
		Incr = (((intptr_t)Target) >> 16) | 1;
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
	return 0;
}

int targetset_foreach(targetset_t *Set, void *Data, int (*callback)(target_t *, void *)) {
	target_t **End = Set->Targets + Set->Size;
	for (target_t **T = Set->Targets; T < End; ++T) {
		if (*T && callback(*T, Data)) return 1;
	}
	return 0;
}
