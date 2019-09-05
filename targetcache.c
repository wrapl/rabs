#include "targetcache.h"
#include "target.h"
#include "minilang/stringmap.h"
#include "minilang/ml_macros.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <gc/gc.h>

/*
typedef struct node_t node_t;

struct node_t {
	const ml_type_t *Type;
	node_t *Child[2];
	uint32_t Byte;
	uint8_t OtherBits;
};

static node_t *CacheRoot = 0;

void targetcache_init(int CacheSize) {
}

target_t **targetcache_lookup(const char *Id, size_t IdLength) {
	uint8_t *UBytes = (uint8_t *)Id;
	size_t ULen = IdLength;
	if (!CacheRoot) return (target_t **)&CacheRoot;
	node_t *Node = CacheRoot, **Slot = &CacheRoot;
	while (!Node->Type) {
		uint8_t C = 0;
		if (Node->Byte < ULen) C = UBytes[Node->Byte];
		const int Dir = (1 + (Node->OtherBits | C)) >> 8;
		Slot = &Node->Child[Dir];
		Node = *Slot;
	}
	uint32_t NewByte, NewOtherBits;
	uint8_t *VBytes = (uint8_t *)((target_t *)Node)->Id;
	for (NewByte = 0; NewByte < ULen; ++NewByte) {
		if (VBytes[NewByte] != UBytes[NewByte]) {
			NewOtherBits = VBytes[NewByte] ^ UBytes[NewByte];
			goto different_byte_found;
		}
	}
	if (VBytes[NewByte] != 0) {
		NewOtherBits = VBytes[NewByte];
		goto different_byte_found;
	}
	return (target_t **)Slot;
different_byte_found:
	NewOtherBits |= NewOtherBits >> 1;
	NewOtherBits |= NewOtherBits >> 2;
	NewOtherBits |= NewOtherBits >> 4;
	NewOtherBits = (NewOtherBits & ~(NewOtherBits >> 1)) ^ 255;
	uint8_t C = VBytes[NewByte];
	int NewDir = (1 + (NewOtherBits | C)) >> 8;
	node_t *NewNode = new(node_t);
	NewNode->Byte = NewByte;
	NewNode->OtherBits = NewOtherBits;
	Slot = &NewNode->Child[1 - NewDir];
	node_t **WhereP = &CacheRoot;
	for (;;) {
		node_t *Node = *WhereP;
		if (Node->Type) break;
		if (Node->Byte > NewByte) break;
		if (Node->Byte == NewByte && Node->OtherBits > NewOtherBits) break;
		uint8_t C = 0;
		if (Node->Byte < ULen) C = UBytes[Node->Byte];
		const int Dir = (1 + (Node->OtherBits | C)) >> 8;
		WhereP = Node->Child + Dir;
	}
	NewNode->Child[NewDir] = *WhereP;
	*WhereP = NewNode;
	return (target_t **)Slot;
}

int targetcache_size() {
	return 0;
}
*/

typedef struct {
	target_t *Target;
	unsigned long Hash;
} node_t;

static size_t SizeA, SpaceA;
static node_t *NodesA;

void targetcache_init(int CacheSize) {
	int InitialSize = 16;
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

target_t **targetcache_lookup(const char *Id, size_t IdLength) {
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
				const unsigned char *A = (unsigned char *)Nodes[Index].Target->Id;
				const unsigned char *B = (unsigned char *)Id;
			loop1:
				if (*A < *B) break;
				if (!*A) {
					return &Nodes[Index].Target;
				}
				if (*B) {
					++A; ++B; goto loop1;
				}
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
						const unsigned char *A = (unsigned char *)Nodes[Index].Target->Id;
						const unsigned char *B = (unsigned char *)Id;
					loop2:
						if (*A < *B) {
							OldTarget = Nodes[Index].Target;
							Nodes[Index].Target = Target;
							break;
						}
						if (!*A) asm("int3");
						if (*B) {
							++A; ++B; goto loop2;
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
	return SizeA - SpaceA;
}
