#include "context.h"
#include "rabs.h"
#include "util.h"
#include "cache.h"
#include "target.h"
#include "target_symb.h"
#include "stringmap.h"
#include <gc/gc.h>
#include <unistd.h>
#include <string.h>

static stringmap_t ContextCache[1] = {STRINGMAP_INIT};
static ml_value_t *DefaultString;
ML_TYPE(ContextT, (MLAnyT), "context");

context_t *context_find(const char *Name) {
	return stringmap_search(ContextCache, Name);
}

context_t *context_make(const char *Name) {
	context_t **Slot = (context_t **)stringmap_slot(ContextCache, Name);
	if (!Slot[0]) {
		context_t *Context = Slot[0] = new(context_t);
		Context->Name = Name;
	}
	return Slot[0];
}

context_t *context_push(const char *Path) {
	context_t *Context = context_make(Path);
	Context->Type = ContextT;
	Context->Parent = CurrentContext;
	Context->Path = Path;
	Context->Name = Path;
	Context->FullPath = concat(RootPath, Path, NULL);
	CurrentContext = Context;
	Context->Locals[0] = (stringmap_t)STRINGMAP_INIT;
	target_t *Default = Context->Default = (target_t *)target_meta_new(NULL, 1, &DefaultString);
	stringmap_insert(Context->Locals, "DEFAULT", Default);
	target_t *BuildDir = target_file_check(Path[0] == '/' ? Path + 1 : Path, 0);
	stringmap_insert(Context->Locals, "BUILDDIR", BuildDir);
	stringmap_insert(Context->Locals, "PATH", BuildDir);
	stringmap_insert(Context->Locals, "CONTEXT", Context);
	stringmap_insert(Context->Locals, "PARENT", (ml_value_t *)Context->Parent ?: MLNil);
	return Context;
}

context_t *context_scope(const char *Name) {
	context_t *Context = context_make(concat(CurrentContext->Name, ":", Name, NULL));
	Context->Type = ContextT;
	Context->Parent = CurrentContext;
	Context->Path = CurrentContext->Path;
	Context->FullPath = CurrentContext->FullPath;
	CurrentContext = Context;
	Context->Default = Context->Parent->Default;
	Context->Locals[0] = (stringmap_t)STRINGMAP_INIT;
	stringmap_insert(Context->Locals, "CONTEXT", Context);
	stringmap_insert(Context->Locals, "PARENT", Context->Parent);
	return Context;
}

void context_pop() {
	CurrentContext = CurrentContext->Parent;
	chdir(concat(RootPath, CurrentContext->Path, NULL));
}

ml_value_t *context_symb_get(context_t *Context, const char *Name) {
	while (Context) {
		ml_value_t *Value = stringmap_search(Context->Locals, Name);
		if (Value) return Value;
		Context = Context->Parent;
	}
	return 0;
}

ml_value_t *context_symb_set(context_t *Context, const char *Name, ml_value_t *Value) {
	stringmap_insert(Context->Locals, Name, Value);
	return Value;
}

ML_METHOD(".", ContextT, MLStringT) {
	context_t *Context = (context_t *)Args[0];
	const char *Name = ml_string_value(Args[1]);
	return (ml_value_t *)target_symb_new(Context, Name);
}

ML_METHOD("::", ContextT, MLStringT) {
	context_t *Context = (context_t *)Args[0];
	const char *Name = ml_string_value(Args[1]);
	return (ml_value_t *)target_symb_new(Context, Name);
}

ML_METHOD("parent", ContextT) {
	context_t *Context = (context_t *)Args[0];
	return (ml_value_t *)Context->Parent ?: MLNil;
}

ML_METHOD("name", ContextT) {
	context_t *Context = (context_t *)Args[0];
	return ml_string(Context->Name, -1);
}

ML_METHOD("path", ContextT) {
	context_t *Context = (context_t *)Args[0];
	return ml_string(Context->Path, -1);
}

ML_METHOD("/", ContextT, MLStringT) {
	context_t *Context = (context_t *)Args[0];
	const char *Name = ml_string_value(Args[1]);
	const char *Path = concat(Context->Path, "/", Name, NULL);
	return (ml_value_t *)context_make(Path) ?: MLNil;
}

ML_METHOD("@", ContextT, MLStringT) {
	context_t *Context = (context_t *)Args[0];
	const char *Name = ml_string_value(Args[1]);
	const char *Path = concat(Context->Path, ":", Name, NULL);
	return (ml_value_t *)context_make(Path) ?: MLNil;
}

ML_METHOD("in", ContextT, MLFunctionT) {
	context_t *OldContext = CurrentContext;
	CurrentContext = (context_t *)Args[0];
	ml_value_t *Result = ml_simple_call(Args[1], 0, NULL);
	if (Result->Type == MLErrorT) {
		printf("Error: %s\n", ml_error_message(Result));
		ml_source_t Source;
		int Level = 0;
		while (ml_error_source(Result, Level++, &Source)) {
			printf("\e[31m\t%s:%d\n\e[0m", Source.Name, Source.Line);
		}
		exit(1);
	}
	CurrentContext = OldContext;
	return Result;
}

static int context_export_fn(const char *Name, void *Value, ml_value_t *Exports) {
	ml_list_append(Exports, ml_cstring(Name));
	return 0;
}

ML_METHOD("exports", ContextT) {
	context_t *Context = (context_t *)Args[0];
	ml_value_t *Exports = ml_list();
	stringmap_foreach(Context->Locals, Exports, (void *)context_export_fn);
	return Exports;
}

void context_init() {
	DefaultString = ml_cstring("DEFAULT");
#include "context_init.c"
}
