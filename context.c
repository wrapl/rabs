#include "context.h"
#include "rabs.h"
#include "util.h"
#include "cache.h"
#include "target.h"
#include "stringmap.h"
#include <gc.h>
#include <unistd.h>

__thread context_t *CurrentContext = 0;
static stringmap_t ContextCache[1] = {STRINGMAP_INIT};
static ml_value_t *DefaultString;

context_t *context_find(const char *Path) {
	return stringmap_search(ContextCache, Path);
}

context_t *context_push(const char *Path) {
	context_t *Context = (context_t *)GC_malloc(sizeof(context_t));
	Context->Parent = CurrentContext;
	Context->Path = Path;
	Context->Name = Path;
	Context->FullPath = concat(RootPath, Path, 0);
	if (CurrentContext) {
		Context->Mounts = CurrentContext->Mounts;
	} else {
		Context->Mounts = 0;
	}
	CurrentContext = Context;
	Context->Locals[0] = STRINGMAP_INIT;
	target_t *Default = Context->Default = (target_t *)target_meta_new(0, 1, &DefaultString);
	stringmap_insert(Context->Locals, "DEFAULT", Default);
	target_t *BuildDir = target_file_check(vfs_resolve(Context->Mounts, Path), 0);
	stringmap_insert(Context->Locals, "BUILDDIR", BuildDir);
	stringmap_insert(ContextCache, Context->Name, Context);
	return Context;
}

context_t *context_scope(const char *Name) {
	context_t *Context = (context_t *)GC_malloc(sizeof(context_t));
	Context->Parent = CurrentContext;
	Context->Path = CurrentContext->Path;
	Context->Name = concat(CurrentContext->Name, ":", Name, 0);
	Context->FullPath = CurrentContext->FullPath;
	if (CurrentContext) {
		Context->Mounts = CurrentContext->Mounts;
	} else {
		Context->Mounts = 0;
	}
	CurrentContext = Context;
	Context->Default = Context->Parent->Default;
	Context->Locals[0] = STRINGMAP_INIT;
	stringmap_insert(ContextCache, Context->Name, Context);
	return Context;
}

void context_pop() {
	CurrentContext = CurrentContext->Parent;
	chdir(concat(RootPath, CurrentContext->Path, 0));
}

ml_value_t *context_symb_get(context_t *Context, const char *Name) {
	while (Context) {
		ml_value_t *Value = stringmap_search(Context->Locals, Name);
		if (Value) return Value;
		Context = Context->Parent;
	}
	return 0;
}

void context_symb_set(context_t *Context, const char *Name, ml_value_t *Value) {
	stringmap_insert(Context->Locals, Name, Value);
}

void context_init() {
	DefaultString = ml_string("DEFAULT", -1);
}
