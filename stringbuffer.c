#define _GNU_SOURCE
#include "stringbuffer.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <gc.h>

#define new(T) ((T *)GC_MALLOC(sizeof(T)))
#define anew(T, N) ((T *)GC_MALLOC((N) * sizeof(T)))
#define snew(N) ((char *)GC_MALLOC_ATOMIC(N))
#define xnew(T, N, U) ((T *)GC_MALLOC(sizeof(T) + (N) * sizeof(U)))

#define NODE_SIZE 120

struct stringbuffer_node_t {
	stringbuffer_node_t *Next;
	char Chars[NODE_SIZE];
};

static stringbuffer_node_t *Cache = 0;

ssize_t stringbuffer_add(stringbuffer_t *Buffer, const char *String, size_t Length) {
	size_t Remaining = Length;
	stringbuffer_node_t **Slot = &Buffer->Nodes;
	stringbuffer_node_t *Node = Buffer->Nodes;
	if (Node) {
		while (Node->Next) Node = Node->Next;
		Slot = &Node->Next;
	}
	while (Buffer->Space < Remaining) {
		memcpy(Node->Chars + NODE_SIZE - Buffer->Space, String, Buffer->Space);
		String += Buffer->Space;
		Remaining -= Buffer->Space;
		stringbuffer_node_t *Next;
		if (Cache) {
			Next = Cache;
			Cache = Cache->Next;
		} else {
			Next = new(stringbuffer_node_t);
		}
		Node = Slot[0] = Next;
		Slot = &Node->Next;
		Buffer->Space = NODE_SIZE;
	}
	memcpy(Node->Chars + NODE_SIZE - Buffer->Space, String, Remaining);
	Buffer->Space -= Remaining;
	Buffer->Length += Length;
	return Length;
}

ssize_t stringbuffer_addf(stringbuffer_t *Buffer, const char *Format, ...) {
	char *String;
	va_list Args;
	va_start(Args, Format);
	size_t Length = vasprintf(&String, Format, Args);
	va_end(Args);
	return stringbuffer_add(Buffer, String, Length);
}

const char *stringbuffer_get(stringbuffer_t *Buffer) {
	if (Buffer->Length == 0) return "";
	char *String = GC_malloc_atomic(Buffer->Length + 1);
	char *P = String;
	stringbuffer_node_t *Node = Buffer->Nodes;
	while (Node->Next) {
		memcpy(P, Node->Chars, NODE_SIZE);
		P += NODE_SIZE;
		Node = Node->Next;
	}
	memcpy(P, Node->Chars, NODE_SIZE - Buffer->Space);
	P += NODE_SIZE - Buffer->Space;
	*P++ = 0;
	stringbuffer_node_t **Slot = &Cache;
	while (Slot[0]) Slot = &Slot[0]->Next;
	Slot[0] = Buffer->Nodes;
	Buffer->Nodes = 0;
	Buffer->Length = Buffer->Space = 0;
	return String;
}

ml_type_t StringBufferT[1] = {{
	AnyT,
	ml_default_hash,
	ml_default_call,
	ml_default_index,
	ml_default_deref,
	ml_default_assign,
	ml_default_next,
	ml_default_key
}};

