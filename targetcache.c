#include "targetcache.h"
#include "target.h"
#include "minilang/stringmap.h"
#include "minilang/ml_macros.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <gc/gc.h>

typedef struct {
	target_t *Target;
	unsigned long Hash;
} node_t;

static size_t SizeA, SpaceA;
static node_t *NodesA;

void targetcache_init(int CacheSize) {
	int InitialSize = 1;
	while (InitialSize <= CacheSize) InitialSize *= 2;
	SizeA = InitialSize;
	SpaceA = InitialSize;
	NodesA = anew(node_t, SizeA);
}

static void sort_nodes(node_t *First, node_t *Last) {
	node_t *A = First;
	node_t *B = Last;
	node_t T = *A;
	node_t P = *B;
	while (!P.Target) {
		--B;
		--Last;
		if (A == B) return;
		P = *B;
	}
	while (A != B) {
		int Cmp;
		if (T.Target) {
			if (T.Hash < P.Hash) {
				Cmp = -1;
			} else if (T.Hash > P.Hash) {
				Cmp = 1;
			} else {
				Cmp = strcmp(T.Target->Id, P.Target->Id);
			}
		} else {
			Cmp = -1;
		}
		if (Cmp > 0) {
			*A = T;
			T = *++A;
		} else {
			*B = T;
			T = *--B;
		}
	}
	*A = P;
	if (First < A - 1) sort_nodes(First, A - 1);
	if (A + 1 < Last) sort_nodes(A + 1, Last);
}

target_t **targetcache_lookup(const char *Id) {
	unsigned long Hash = stringmap_hash(Id);
	unsigned int Mask = SizeA - 1;
	for (;;) {
		unsigned int Incr = ((Hash >> 8) | 1) & Mask;
		unsigned int Index = Hash & Mask;
		node_t *Nodes = NodesA;
		for (;;) {
			if (!Nodes[Index].Target) break;
			if (Nodes[Index].Hash < Hash) break;
			if (Nodes[Index].Hash == Hash) {
				int Cmp = strcmp(Nodes[Index].Target->Id, Id);
				if (Cmp == 0) return &Nodes[Index].Target;
				if (Cmp < 0) break;
			}
			Index += Incr;
			Index &= Mask;
		}
		if (SpaceA > (SizeA >> 3)) {
			--SpaceA;
			unsigned int Result = Index;
			target_t *OldTarget = Nodes[Index].Target;
			Nodes[Index].Target = 0;
			Nodes[Index].Hash = Hash;
			while (OldTarget) {
				target_t *Target = OldTarget;
				Id = Target->Id;
				Hash = Target->IdHash;
				Incr = ((Hash >> 8) | 1) & Mask;
				for (;;) {
					Index += Incr;
					Index &= Mask;
					if (Index == Result) {
					} else if (!Nodes[Index].Target) {
						Nodes[Index].Target = Target;
						Nodes[Index].Hash = Hash;
						return &Nodes[Result].Target;
					} else if (Nodes[Index].Hash < Hash) {
						OldTarget = Nodes[Index].Target;
						Nodes[Index].Target = Target;
						Nodes[Index].Hash = Hash;
						break;
					} else if (Nodes[Index].Hash == Hash) {
						int Cmp = strcmp(Nodes[Index].Target->Id, Id);
						if (Cmp < 0) {
							OldTarget = Nodes[Index].Target;
							Nodes[Index].Target = Target;
							break;
						}
					}
				}
			}
			return &Nodes[Result].Target;
		}
		size_t NewSize = SizeA * 2;
		Mask = NewSize - 1;
		node_t *NewNodes = anew(node_t, NewSize);
		sort_nodes(Nodes, Nodes + SizeA - 1);
		for (node_t *Old = Nodes; Old->Target; ++Old) {
			target_t *Target = Old->Target;
			unsigned long NewHash = Target->IdHash;
			unsigned int NewIncr = ((NewHash >> 8) | 1) & Mask;
			unsigned int NewIndex = NewHash & Mask;
			while (NewNodes[NewIndex].Target) {
				NewIndex += NewIncr;
				NewIndex &= Mask;
			}
			NewNodes[NewIndex].Target = Target;
			NewNodes[NewIndex].Hash = NewHash;
		}
		NodesA = NewNodes;
		SpaceA += SizeA;
		SizeA = NewSize;
	}
	return 0;
}

int targetcache_size() {
	return SizeA;
}
