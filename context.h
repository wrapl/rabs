#ifndef CONTEXT_H
#define CONTEXT_H

#include "vfs.h"
#include <lua.h>

typedef struct context_t context_t;

struct context_t {
	context_t *Parent;
	const char *Path, *Name;
	const vmount_t *Mounts;
	int Ref, Locals;
	struct target_t *Default;
};

void context_init();

context_t *context_find(const char *Path);

context_t *context_push(const char *Path);
context_t *context_scope(const char *Name);
void context_pop();

int context_symb_get(context_t *Context, const char *Name);
void context_symb_set(const char *Name);

extern int msghandler(lua_State *L);

extern context_t *CurrentContext;

#endif
