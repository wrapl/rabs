#include "context.h"
#include "rabs.h"
#include "util.h"
#include "cache.h"
#include "target.h"
#include <gc.h>
#include <unistd.h>

context_t *CurrentContext = 0;
static struct HXmap *ContextCache;

context_t *context_find(const char *Path) {
	return HXmap_get(ContextCache, Path);
}

context_t *context_push(const char *Path) {
	context_t *Context = (context_t *)GC_malloc(sizeof(context_t));
	Context->Parent = CurrentContext;
	Context->Path = Path;
	Context->Name = "";
	if (CurrentContext) {
		Context->Mounts = CurrentContext->Mounts;
	} else {
		Context->Mounts = 0;
	}
	CurrentContext = Context;
	chdir(concat(RootPath, CurrentContext->Path, 0));
	lua_createtable(L, 0, 0);
	lua_pushstring(L, "DEFAULT");
	lua_pushcfunction(L, target_meta_new);
	lua_pushvalue(L, -2);
	lua_call(L, 1, 1);
	Context->Default = luaL_checkudata(L, -1, "target");
	lua_rawset(L, -3);
	Context->Locals = luaL_ref(L, LUA_REGISTRYINDEX);
	HXmap_add(ContextCache, Context->Path, Context);
	return Context;
}

context_t *context_scope(const char *Name) {
	context_t *Context = (context_t *)GC_malloc(sizeof(context_t));
	Context->Parent = CurrentContext;
	Context->Path = CurrentContext->Path;
	Context->Name = concat(CurrentContext->Name, ":", Name, 0);
	if (CurrentContext) {
		Context->Mounts = CurrentContext->Mounts;
	} else {
		Context->Mounts = 0;
	}
	CurrentContext = Context;
	Context->Default = Context->Parent->Default;
	lua_createtable(L, 0, 0);
	Context->Locals = luaL_ref(L, LUA_REGISTRYINDEX);
	const char *Id = concat(Context->Path, Context->Name, 0);
	HXmap_add(ContextCache, Id, Context);
	return Context;
}

void context_pop() {
	CurrentContext = CurrentContext->Parent;
	chdir(concat(RootPath, CurrentContext->Path, 0));
}

int context_symb_get(context_t *Context, const char *Name) {
	while (Context) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, Context->Locals);
		lua_pushstring(L, Name);
		if (lua_rawget(L, -2) != LUA_TNIL) {
			lua_remove(L, -2);
			return 1;
		}
		lua_pop(L, 2);
		Context = Context->Parent;
	}
	lua_getglobal(L, Name);
	return 0;
}

void context_symb_set(const char *Name) {
	lua_rawgeti(L, LUA_REGISTRYINDEX, CurrentContext->Locals);
	lua_pushstring(L, Name);
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pop(L, 1);
}

int msghandler(lua_State *L) {
	const char *Msg = lua_tostring(L, 1);
	if (!Msg) {
		if (luaL_callmeta(L, 1, "__tostring") && lua_type(L, -1) == LUA_TSTRING) {
			return 1;
		} else {
			Msg = lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
		}
	}
	luaL_traceback(L, L, Msg, 1);
	return 1;
}

void context_init() {
	ContextCache = HXmap_init(HXMAPT_DEFAULT, HXMAP_SCKEY);
}
