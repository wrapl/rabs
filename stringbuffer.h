#ifndef STRINGBUFFER_H
#define STRINGBUFFER_H

#include "minilang.h"
#include <stdlib.h>

typedef struct stringbuffer_t stringbuffer_t;
typedef struct stringbuffer_node_t stringbuffer_node_t;

struct stringbuffer_t {
	const ml_type_t *Type;
	stringbuffer_node_t *Nodes;
	size_t Space, Length;
};

extern ml_type_t StringBufferT[1];

#define STRINGBUFFER_INIT (stringbuffer_t){StringBufferT, 0,}

ssize_t stringbuffer_add(stringbuffer_t *Buffer, const char *String, size_t Length);
ssize_t stringbuffer_addf(stringbuffer_t *Buffer, const char *Format, ...);
const char *stringbuffer_get(stringbuffer_t *Buffer);

#endif
