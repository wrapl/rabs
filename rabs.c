#define _GNU_SOURCE
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <lfs.h>
#include <gc/gc.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <time.h>
#include "target.h"
#include "context.h"
#include "util.h"
#include "cache.h"
#include <libHX/io.h>

const char *RootPath = 0;
static int RabsEnv;
lua_State *L;

static void load_file(lua_State *L, const char *FileName) {
	//printf("\n\nLoad: %s\n", Path);
	//printf("Loading: %s\n", FileName);
	lua_pushcfunction(L, msghandler);
	if (luaL_loadfile(L, FileName) != LUA_OK) {
		fprintf(stderr, "\e[31mError: %s\e[0m", lua_tostring(L, -1));
		exit(1);
	}
	lua_rawgeti(L, LUA_REGISTRYINDEX, RabsEnv);
	lua_setupvalue(L, -2, 1);
	if (lua_pcall(L, 0, 0, -2) != LUA_OK) {
		fprintf(stderr, "\e[31mError: %s\e[0m", lua_tostring(L, -1));
		exit(1);
	}
	lua_pop(L, 1);
}

int subdir(lua_State *L) {
	const char *Path = luaL_checkstring(L, 1);
	Path = concat(CurrentContext->Path, "/", Path, 0);
	//printf("Path = %s\n", Path);
	HX_mkdir(concat(RootPath, Path, 0), 0777);
	const char *FileName = concat(RootPath, Path, "/_build_", 0);
	//printf("FileName = %s\n", FileName);
	FileName = vfs_resolve(CurrentContext->Mounts, FileName);
	target_t *ParentDefault = CurrentContext->Default;
	context_push(Path);
	target_depends_add(ParentDefault, CurrentContext->Default);
	load_file(L, FileName);
	context_pop();
	return 0;
}

int scope(lua_State *L) {
	const char *Name = luaL_checkstring(L, 1);
	context_scope(Name);
	lua_pushcfunction(L, msghandler);
	lua_pushvalue(L, 2);
	if (lua_pcall(L, 0, 0, -2) != LUA_OK) {
		fprintf(stderr, "\e[31mError: %s\e[0m", lua_tostring(L, -1));
		exit(1);
	}
	lua_pop(L, 1);
	context_pop();
	return 0;
}

int include(lua_State *L) {
	const char *FileName = luaL_checkstring(L, 1);
	if (FileName[0] != '/') {
		FileName = concat(RootPath, CurrentContext->Path, "/", FileName, 0);
		FileName = vfs_resolve(CurrentContext->Mounts, FileName);
	}
	load_file(L, FileName);
	return 0;
}

int vmount(lua_State *L) {
	const char *Path = luaL_checkstring(L, 1);
	const char *Target = luaL_checkstring(L, 2);
	CurrentContext->Mounts = vfs_mount(CurrentContext->Mounts,
		concat(CurrentContext->Path, "/", Path, 0),
		concat(CurrentContext->Path, "/", Target, 0)
	);
	return 0;
}

int context(lua_State *L) {
	lua_pushstring(L, CurrentContext->Path);
	return 1;
}

char *stringify(char *Buffer) {
	switch (lua_type(L, -1)) {
	case LUA_TNIL: return Buffer;
	case LUA_TNUMBER: if (lua_isinteger(L, -1)) {
		return Buffer + sprintf(Buffer, "%d", lua_tointeger(L, -1));
	} else {
		return Buffer + sprintf(Buffer, "%f", lua_tonumber(L, -1));
	}
	case LUA_TBOOLEAN: return stpcpy(Buffer, lua_toboolean(L, -1) ? "true" : "false");
	case LUA_TSTRING: return stpcpy(Buffer, lua_tostring(L, -1));
	case LUA_TTABLE: {
		int Length = luaL_len(L, -1);
		for (int I = 1; I <= Length; ++I) {
			if (I > 1) *(Buffer++) = ' ';
			lua_rawgeti(L, -1, I);
			Buffer = stringify(Buffer);
			lua_pop(L, 1);
		}
		return Buffer;
	}
	case LUA_TFUNCTION: {
		lua_pushcfunction(L, msghandler);
		lua_pushvalue(L, -2);
		if (lua_pcall(L, 0, 1, -2) != LUA_OK) {
			fprintf(stderr, "\e[31mError: %s\e[0m", lua_tostring(L, -1));
			exit(1);
		}
		Buffer = stringify(Buffer);
		lua_pop(L, 2);
		return Buffer;
	}
	}
	target_t *Target = luaL_checkudata(L, -1, "target");
	lua_pushcfunction(L, target_tostring);
	lua_pushvalue(L, -2);
	lua_call(L, 1, 1);
	Buffer = stpcpy(Buffer, lua_tostring(L, -1));
	lua_pop(L, 1);
	return Buffer;
}

int execute(lua_State *L) {
	char *Buffer = GC_malloc_atomic(8192);
	int N = lua_gettop(L);
	lua_pushvalue(L, 1);
	char *Next = stringify(Buffer);
	lua_pop(L, 1);
	for (int I = 2; I <= N; ++I) {
		*Next++ = ' ';
		lua_pushvalue(L, I);
		Next = stringify(Next);
		lua_pop(L, 1);
	}
	clock_t Start = clock();
	printf("\e[34m%s\e[0m\n", Buffer);
	if (system(Buffer)) {
		return luaL_error(L, "Process returned non-zero exit code");
	} else {
		clock_t End = clock();
		printf("\t\e[34m%f seconds.\e[0m\n", ((double)(End - Start)) / CLOCKS_PER_SEC);
		return 0;
	}
}

int shell(lua_State *L) {
	char *Buffer = GC_malloc_atomic(8192);
	int N = lua_gettop(L);
	lua_pushvalue(L, 1);
	char *Next = stringify(Buffer);
	lua_pop(L, 1);
	for (int I = 2; I <= N; ++I) {
		*Next++ = ' ';
		lua_pushvalue(L, I);
		Next = stringify(Next);
		lua_pop(L, 1);
	}
	printf("\e[34m%s\e[0m\n", Buffer);
	clock_t Start = clock();
	FILE *File = popen(Buffer, "r");
	luaL_Buffer Output[1];
	luaL_buffinit(L, Output);
	while (!feof(File)) {
		ssize_t Count = fread(Buffer, 1, 8192, File);
		luaL_addlstring(Output, Buffer, Count);
	}
	pclose(File);
	luaL_pushresult(Output);
	clock_t End = clock();
	printf("\t\e[34m%f seconds.\e[0m\n", ((double)(End - Start)) / CLOCKS_PER_SEC);
	return 1;
}

int lua_mkdir(lua_State *L) {
	char *Buffer = GC_malloc_atomic(8192);
	int N = lua_gettop(L);
	lua_pushvalue(L, 1);
	char *Next = stringify(Buffer);
	lua_pop(L, 1);
	for (int I = 2; I <= N; ++I) {
		*Next++ = '/';
		lua_pushvalue(L, I);
		Next = stringify(Next);
		lua_pop(L, 1);
	}
	if (HX_mkdir(Buffer, 0777) < 0) {
		return luaL_error(L, "Failed to create directory");
	}
	return 0;
}

int rabs_index(lua_State *L) {
	const char *Name = lua_tostring(L, 2);
	if (context_symb_get(CurrentContext, Name)) {
		target_t *Target = target_symb_new(Name);
		target_depends_auto(Target);
		target_update(Target);
	}
	return 1;
}

int rabs_newindex(lua_State *L) {
	const char *Name = lua_tostring(L, 2);
	context_symb_set(Name);
	return 0;
}

static const char *find_root(const char *Path) {
	char *FileName = (char *)GC_malloc(strlen(Path) + strlen("/_build_") + 1);
	char *End = stpcpy(FileName, Path);
	strcpy(End, "/_build_");
	char Line[strlen("-- ROOT --\n")];
	FILE *File = 0;
loop:
	File = fopen(FileName, "r");
	if (File) {
		if (fread(Line, 1, sizeof(Line), File) == sizeof(Line)) {
			if (!memcmp(Line, "-- ROOT --\n", sizeof(Line))) {
				fclose(File);
				*End = 0;
				return FileName;
			}
		}
		fclose(File);
	}
	while (--End > FileName) {
		if (*End == '/') {
			strcpy(End, "/_build_");
			goto loop;
		}
	}
	return 0;
}

static const luaL_Reg Globals[] = {
	{"vmount", vmount},
	{"subdir", subdir},
	{"file", target_file_new},
	{"meta", target_meta_new},
	{"include", include},
	{"context", context},
	{"execute", execute},
	{"shell", shell},
	{"mkdir", lua_mkdir},
	{"scope", scope},
	{0, 0}
};

static const luaL_Reg RabsMethods[] = {
	{"__index", rabs_index},
	{"__newindex", rabs_newindex},
	{0, 0}
};

static void *lua_alloc(void *Data, void *P, size_t Old, size_t New) {
	if (P) {
		return GC_realloc(P, New);
	} else {
		return GC_malloc(New);
	}
}

extern char LuaBuiltins[];

int main(int Argc, const char **Argv) {
	L = lua_newstate(lua_alloc, 0);
	luaL_openlibs(L);
	luaopen_lfs(L);
	if (luaL_loadstring(L, LuaBuiltins) != LUA_OK) {
		fprintf(stderr, "\e[31mError: %s\e[0m", lua_tostring(L, -1));
		exit(1);
	}
	if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
		fprintf(stderr, "\e[31mError: %s\e[0m", lua_tostring(L, -1));
		exit(1);
	}

	const char *TargetName = 0;
	int QueryOnly = 0;
	int ListTargets = 0;
	for (int I = 1; I < Argc; ++I) {
		if (Argv[I][0] == '-') {
			switch (Argv[I][1]) {
			case 'D': {
				char *Define = concat(Argv[I] + 2, 0);
				char *Equals = strchr(Define, '=');
				if (Equals) {
					*Equals = 0;
					lua_pushstring(L, Equals + 1);
				} else {
					lua_pushboolean(L, 1);
				};
				lua_setglobal(L, Define);
				break;
			};
			case 'q': {
				QueryOnly = 1;
				break;
			}
			case 'l': {
				ListTargets = 1;
				break;
			}
			};
		} else {
			TargetName = Argv[I];
		};
	};

	vfs_init();
	target_init();
	context_init();
	lua_createtable(L, 0, sizeof(Globals) / sizeof(luaL_Reg));
	luaL_setfuncs(L, Globals, 0);
	luaL_newmetatable(L, "rabs");
	luaL_setfuncs(L, RabsMethods, 0);
	lua_setmetatable(L, -2);
	RabsEnv = luaL_ref(L, LUA_REGISTRYINDEX);
#ifdef LINUX
	const char *Path = get_current_dir_name();
#else
	char *Path = (char *)GC_malloc_atomic(1024);
	getcwd(Path, 1024);
#endif
	RootPath = find_root(Path);
	if (!RootPath) {
		puts("\e[31mError: could not find project root\e[0m");
	} else {
		printf("RootPath = %s, Path = %s\n", RootPath, Path);
		cache_open(RootPath);
		context_push("");
		lua_pushinteger(L, CurrentVersion);
		context_symb_set("VERSION");
		lua_pop(L, 1);
		load_file(L, concat(RootPath, "/_build_", 0));
		target_t *Target;
		if (TargetName) {
			Target = target_get(TargetName);
			if (!Target) {
				printf("\e[31mError: invalid target %s\e[0m", TargetName);
				exit(1);
			}
		} else {
			context_t *Context = context_find(match_prefix(Path, RootPath));
			if (!Context) {
				printf("\e[31mError: current directory is not in project\e[0m");
				exit(1);
			}
			Target = Context->Default;
		}
		if (ListTargets) {
			target_list();
		} else if (QueryOnly) {
			target_query(Target);
		} else {
			target_update(Target);
		}
	}
	return 0;
}
