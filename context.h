#ifndef CONTEXT_H
#define CONTEXT_H

#include "vfs.h"
#include "stringmap.h"
#include "minilang.h"

typedef struct context_t context_t;

struct context_t {
	context_t *Parent;
	const char *Path, *Name;
	const vmount_t *Mounts;
	struct target_t *Default;
	stringmap_t Locals[1];
};

void context_init();

context_t *context_find(const char *Path);

context_t *context_push(const char *Path);
context_t *context_scope(const char *Name);
void context_pop();

ml_value_t *context_symb_get(context_t *Context, const char *Name);
void context_symb_set(context_t *Context, const char *Name, ml_value_t *Value);

extern context_t *CurrentContext;

#endif
