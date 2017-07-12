#include "target.h"
#include "rabs.h"
#include "util.h"
#include "context.h"
#include "cache.h"
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <gc.h>
#include <errno.h>

#define new(T) ((T *)GC_MALLOC(sizeof(T)))
#define anew(T, N) ((T *)GC_MALLOC((N) * sizeof(T)))
#define snew(N) ((char *)GC_MALLOC_ATOMIC(N))
#define xnew(T, N, U) ((T *)GC_MALLOC(sizeof(T) + (N) * sizeof(U)))

typedef struct target_update_t target_update_t;
typedef struct target_pair_t target_pair_t;
typedef struct target_file_t target_file_t;
typedef struct target_meta_t target_meta_t;
typedef struct target_scan_t target_scan_t;
typedef struct target_symb_t target_symb_t;

extern const char *RootPath;
static int TargetMetatable;
static int BuildScanTarget;
static struct map_t *TargetCache;
struct target_t *CurrentTarget = 0;
static struct map_t *DetectedDepends = 0;
static int BuiltTargets = 0;

void target_depends_add(target_t *Target, target_t *Depend) {
	if (Target != Depend) map_set(Target->Depends, Depend, 0);
}

static int target_function_hash(lua_State *L, const void *P, size_t Size, SHA256_CTX *Ctx) {
	sha256_update(Ctx, P, Size);
	return 0;
}

static int list_update_hash(ml_value_t *Value, SHA256_CTX *Ctx) {
	int8_t ChildHash[SHA256_BLOCK_SIZE];
	target_value_hash(Value, ChildHash);
	sha256_update(Ctx, ChildHash, SHA256_BLOCK_SIZE);
}

static int tree_update_hash(ml_value_t *Key, ml_value_t *Value, SHA256_CTX *Ctx) {
	int8_t ChildHash[SHA256_BLOCK_SIZE];
	target_value_hash(Key, ChildHash);
	sha256_update(Ctx, ChildHash, SHA256_BLOCK_SIZE);
	target_value_hash(Value, ChildHash);
	sha256_update(Ctx, ChildHash, SHA256_BLOCK_SIZE);
}

static void target_value_hash(ml_value_t *Value, int8_t Hash[SHA256_BLOCK_SIZE]) {
	if (Value == Nil) {
		memset(Hash, 0, SHA256_BLOCK_SIZE);
	} else if (ml_is_integer(Value)) {
		memset(Hash, 0, SHA256_BLOCK_SIZE);
		*(long *)Hash = ml_to_long(Value);
		Hash[SHA256_BLOCK_SIZE - 1] = 1;
	} else if (ml_is_real(Value)) {
		memset(Hash, 0, SHA256_BLOCK_SIZE);
		*(double *)Hash = ml_to_double(Value);
		Hash[SHA256_BLOCK_SIZE - 1] = 1;
	} else if (ml_is_string(Value)) {
		const char *String = ml_to_string(Value);
		size_t Len = strlen(String);
		const char *String = lua_tolstring(L, -1, &Len);
		SHA256_CTX Ctx[1];
		sha256_init(Ctx);
		sha256_update(Ctx, String, Len);
		sha256_final(Ctx, Hash);
	} else if (ml_is_list(Value)) {
		SHA256_CTX Ctx[1];
		sha256_init(Ctx);
		ml_list_foreach(Value, Ctx, list_update_hash);
		sha256_final(Ctx, Hash);
	} else if (ml_is_tree(Value)) {
		SHA256_CTX Ctx[1];
		sha256_init(Ctx);
		ml_tree_foreach(Value, Ctx, tree_update_hash);
		sha256_final(Ctx, Hash);
	} else if (ml_is_closure(Value)) {
		ml_closure_hash(Value, Hash);
	} else {
		target_t *Target = luaL_checkudata(L, -1, "target");
		SHA256_CTX Ctx[1];
		sha256_init(Ctx);
		sha256_update(Ctx, Target->Id, strlen(Target->Id));
		sha256_final(Ctx, Hash);
		return;
	}
}

bool depends_update_fn(const struct map_t_node *Node, int *DependsLastUpdated) {
	target_t *Depends = (target_t *)Node->key;
	target_update(Depends);
	if (Depends->LastUpdated > *DependsLastUpdated) *DependsLastUpdated = Depends->LastUpdated;
	return true;
}

bool depends_print_fn(const struct map_t_node *Node, int *DependsLastUpdated) {
	target_t *Depends = (target_t *)Node->key;
	if (Depends->LastUpdated == *DependsLastUpdated) printf("\t\e[35m%s[%d]\e[0m\n", Depends->Id, Depends->LastUpdated);
	return true;
}

void target_update(target_t *Target) {
	if (Target->LastUpdated == -1) {
		printf("\e[31mError: build cycle with %s\e[0m\n", Target->Id);
		exit(1);
	}
	int Top = lua_gettop(L);
	if (Target->LastUpdated == 0) {
		Target->LastUpdated = -1;
		++BuiltTargets;
		//printf("\e[32m[%d/%d] \e[33mtarget_update(%s)\e[0m\n", BuiltTargets, TargetCache->items, Target->Id);
		int DependsLastUpdated = 0;
		HXmap_qfe(Target->Depends, (void *)depends_update_fn, &DependsLastUpdated);
		int8_t Previous[SHA256_BLOCK_SIZE];
		int LastUpdated, LastChecked;
		time_t FileTime;
		if (Target->Build) {	
			int8_t BuildHash[SHA256_BLOCK_SIZE];
			lua_rawgeti(L, LUA_REGISTRYINDEX, Target->Build);
			target_value_hash(BuildHash);
			lua_pop(L, 1);
			struct map_t *PreviousDetectedDepends = cache_depends_get(Target->Id);
			const char *BuildId = concat(Target->Id, "::build", 0);
			cache_hash_get(BuildId, &LastUpdated, &LastChecked, &FileTime, Previous);
			if (!LastUpdated || memcmp(Previous, BuildHash, SHA256_BLOCK_SIZE)) {
				cache_hash_set(BuildId, 0, BuildHash);
				DependsLastUpdated = CurrentVersion;
				printf("\t\e[35m<build function updated>\e[0m\n");
			} else if (PreviousDetectedDepends) {
				HXmap_qfe(PreviousDetectedDepends, (void *)depends_update_fn, &DependsLastUpdated);
			}
			cache_hash_get(Target->Id, &LastUpdated, &LastChecked, &FileTime, Previous);
			if ((DependsLastUpdated > LastChecked) || Target->Class->missing(Target)) {
				printf("\e[33mtarget_build(%s) Depends = %d, Last Updated = %d\e[0m\n", Target->Id, DependsLastUpdated, LastChecked);
				HXmap_qfe(Target->Depends, (void *)depends_print_fn, &DependsLastUpdated);
				if (PreviousDetectedDepends) {
					HXmap_qfe(PreviousDetectedDepends, (void *)depends_print_fn, &DependsLastUpdated);
				}
				target_t *PreviousTarget = CurrentTarget;
				struct map_t *PreviousDepends = DetectedDepends;
				context_t *PreviousContext = CurrentContext;
				CurrentTarget = Target;
				DetectedDepends = HXmap_init(HXMAPT_DEFAULT, HXMAP_SINGULAR);
				CurrentContext = Target->BuildContext;
				chdir(concat(RootPath, CurrentContext->Path, 0));
				lua_pushcfunction(L, msghandler);
				lua_rawgeti(L, LUA_REGISTRYINDEX, Target->Build);
				lua_rawgeti(L, LUA_REGISTRYINDEX, Target->Ref);
				if (lua_pcall(L, 1, 0, -3) != LUA_OK) {
					fprintf(stderr, "\e[31mError: %s: %s\e[0m", Target->Id, lua_tostring(L, -1));
					exit(1);
				}
				lua_pop(L, 1);
				cache_depends_set(Target->Id, DetectedDepends);
				CurrentTarget = PreviousTarget;
				DetectedDepends = PreviousDepends;
				CurrentContext = PreviousContext;
				chdir(concat(RootPath, CurrentContext->Path, 0));
			}
		} else {
			cache_hash_get(Target->Id, &LastUpdated, &LastChecked, &FileTime, Previous);	
		}
		FileTime = Target->Class->hash(Target, FileTime, Previous);
		if (!LastUpdated || memcmp(Previous, Target->Hash, SHA256_BLOCK_SIZE)) {
			Target->LastUpdated = CurrentVersion;
			cache_hash_set(Target->Id, FileTime, Target->Hash);
		} else {
			Target->LastUpdated = LastUpdated;
			cache_last_check_set(Target->Id);
		}
	}
	if (Top != lua_gettop(L)) {
		printf("Warning: building %s changed the lua stack from %d to %d\n", Target->Id, Top, lua_gettop(L));
	}
}

bool depends_query_fn(const struct map_t_node *Node, int *DependsLastUpdated) {
	target_t *Depends = (target_t *)Node->key;
	target_query(Depends);
	if (Depends->LastUpdated > *DependsLastUpdated) *DependsLastUpdated = Depends->LastUpdated;
	return true;
}

void target_query(target_t *Target) {
	if (Target->LastUpdated == -1) return;
	if (Target->LastUpdated == 0) {
		Target->LastUpdated = -1;
		printf("Target: %s\n", Target->Id);
		int DependsLastUpdated = 0;
		HXmap_qfe(Target->Depends, (void *)depends_query_fn, &DependsLastUpdated);
		int8_t Previous[SHA256_BLOCK_SIZE];
		int LastUpdated, LastChecked;
		time_t FileTime;
		if (Target->Build) {	
			int8_t BuildHash[SHA256_BLOCK_SIZE];
			ml_closure_hash(Target->Build, BuildHash);
			struct map_t *PreviousDetectedDepends = cache_depends_get(Target->Id);
			const char *BuildId = concat(Target->Id, "::build", 0);
			cache_hash_get(BuildId, &LastUpdated, &LastChecked, &FileTime, Previous);
			if (!LastUpdated || memcmp(Previous, BuildHash, SHA256_BLOCK_SIZE)) {
				DependsLastUpdated = CurrentVersion;
				printf("\t\e[35m<build function updated>\e[0m\n");
			} else if (PreviousDetectedDepends) {
				HXmap_qfe(PreviousDetectedDepends, (void *)depends_query_fn, &DependsLastUpdated);
			}
			cache_hash_get(Target->Id, &LastUpdated, &LastChecked, &FileTime, Previous);
			if ((DependsLastUpdated > LastChecked) || Target->Class->missing(Target)) {
				printf("\e[33mtarget_build(%s) Depends = %d, Last Updated = %d\e[0m\n", Target->Id, DependsLastUpdated, LastChecked);
				HXmap_qfe(Target->Depends, (void *)depends_print_fn, &DependsLastUpdated);
				if (PreviousDetectedDepends) {
					HXmap_qfe(PreviousDetectedDepends, (void *)depends_print_fn, &DependsLastUpdated);
				}
			}
		} else {
			cache_hash_get(Target->Id, &LastUpdated, &LastChecked, &FileTime, Previous);	
		}
	}
}

void target_depends_auto(target_t *Depend) {
	if (CurrentTarget && CurrentTarget != Depend && !map_get(CurrentTarget->Depends, Depend)) {
		map_set(DetectedDepends, Depend, 0);
	}
}

static target_t *target_new(target_class_t *Class, const char *Id) {
	target_t *Target = new(target_t);
	Target->ML = ml_object(Target, Class->Type);
	Target->Class = Class;
	Target->Id = Id;
	Target->Build = 0;
	Target->LastUpdated = 0;
	Target->Depends = new_map();
	map_set(TargetCache, Id, Target);
	return Target;
}

static int target_default_missing(target_t *Target) {
	return 0;
}

int target_tostring(lua_State *L) {
	target_t *Target = luaL_checkudata(L, 1, "target");
	luaL_Buffer Buffer[1];
	luaL_buffinit(L, Buffer);
	Target->Class->tostring(Target, Buffer);
	luaL_pushresult(Buffer);
	return 1;
}

static int target_concat(lua_State *L) {
	const char *String;
	if ((String = lua_tostring(L, 1))) {
		lua_pushstring(L, String);
	} else if (luaL_callmeta(L, 1, "__tostring") && lua_type(L, -1) == LUA_TSTRING) {
	} else {
		return luaL_error(L, "Cannot convert arg 1 to string");
	}
	if ((String = lua_tostring(L, 2))) {
		lua_pushstring(L, String);
	} else if (luaL_callmeta(L, 2, "__tostring") && lua_type(L, -1) == LUA_TSTRING) {
	} else {
		return luaL_error(L, "Cannot convert arg 2 to string");
	}
	lua_concat(L, 2);
	return 1;
}

struct target_file_t {
	TARGET_FIELDS
	int Absolute;
	const char *Path;
};

static void target_file_tostring(target_file_t *Target, luaL_Buffer *Buffer) {
	//target_depends_auto((target_t *)Target);
	if (Target->Absolute) {
		luaL_addstring(Buffer, Target->Path);
	} else {
		const char *Path = vfs_resolve(Target->BuildContext->Mounts, concat(RootPath, "/", Target->Path, 0));
		luaL_addstring(Buffer, Path);
	}
}

static time_t target_file_hash(target_file_t *Target, time_t PreviousTime, int8_t PreviousHash[SHA256_BLOCK_SIZE]) {
	const char *FileName;
	if (Target->Absolute) {
		FileName = Target->Path;
	} else {
		FileName = vfs_resolve(Target->BuildContext->Mounts, concat(RootPath, "/", Target->Path, 0));
	}
	struct stat Stat[1];
	if (stat(FileName, Stat)) {
		//printf("\e[31mWarning: rule failed to build: %s\e[0m\n", FileName);
		return 0;
	}
	if (!S_ISREG(Stat->st_mode)) {
		memset(Target->Hash, -1, SHA256_BLOCK_SIZE);
	} else if (Stat->st_mtime == PreviousTime) {
		memcpy(Target->Hash, PreviousHash, SHA256_BLOCK_SIZE);
	} else {
		int File = open(FileName, 0, O_RDONLY);
		SHA256_CTX Ctx[1];
		uint8_t Buffer[8192];
		sha256_init(Ctx);
		for (;;) {
			int Count = read(File, Buffer, 8192);
			if (Count == 0) break;
			if (Count == -1) {
				printf("\e[31mError: read error: %s\e[0m\n", FileName);
				exit(1);
			}
			sha256_update(Ctx, Buffer, Count);
		}
		close(File);
		sha256_final(Ctx, Target->Hash);
	}
	return Stat->st_mtime;
}

static int target_file_missing(target_file_t *Target) {
	const char *FileName;
	if (Target->Absolute) {
		FileName = Target->Path;
	} else {
		FileName = vfs_resolve(CurrentContext->Mounts, concat(RootPath, "/", Target->Path, 0));
	}
	struct stat Stat[1];
	return !!stat(FileName, Stat);
}

target_class_t FileClass[] = {{
	sizeof(target_file_t),
	(void *)target_file_tostring,
	(void *)target_file_hash,
	(void *)target_file_missing
}};

static target_t *target_file_check(const char *Path, int Absolute) {
	Path = concat(Path, 0);
	const char *Id = concat("file:", Path, 0);
	target_file_t *Target = (target_file_t *)map_get(TargetCache, Id);
	if (!Target) {
		Target = (target_file_t *)target_new(FileClass, Id);
		Target->Absolute = Absolute;
		Target->Path = Path;
		Target->Depends = HXmap_init(HXMAPT_DEFAULT, HXMAP_SINGULAR);
		Target->BuildContext = CurrentContext;
	}
	return (target_t *)Target;
}

int target_file_new(lua_State *L) {
	const char *Path = luaL_checkstring(L, 1);
	target_t *Target;
	if (Path[0] != '/') {
		Path = concat(CurrentContext->Path, "/", Path, 0) + 1;
		Target = target_file_check(Path, 0);
	} else {
		const char *Relative = match_prefix(Path, RootPath);
		if (Relative) {
			Target = target_file_check(Relative + 1, 0);
		} else {
			Target = target_file_check(Path, 1);
		}
	}
	lua_rawgeti(L, LUA_REGISTRYINDEX, Target->Ref);
	return 1;
}

int target_file_dir(lua_State *L) {
	target_file_t *FileTarget = (target_file_t *)luaL_checkudata(L, 1, "target");
	char *Path;
	int Absolute;
	if ((lua_gettop(L) == 2) && lua_toboolean(L, 2)) {
		Path = vfs_resolve(FileTarget->BuildContext->Mounts, concat(RootPath, "/", FileTarget->Path, 0));
		Absolute = 1;
	} else {
		Path = concat(FileTarget->Path, 0);
		Absolute = FileTarget->Absolute;
	}
	char *Last = Path;
	for (char *P = Path; *P; ++P) if (*P == '/') Last = P;
	*Last = 0;
	target_t *Target = target_file_check(Path, Absolute);
	lua_rawgeti(L, LUA_REGISTRYINDEX, Target->Ref);
	return 1;
}

int target_file_basename(lua_State *L) {
	target_file_t *FileTarget = (target_file_t *)luaL_checkudata(L, 1, "target");
	const char *Path = FileTarget->Path;
	const char *Last = Path;
	for (const char *P = Path; *P; ++P) if (*P == '/') Last = P;
	lua_pushstring(L, concat(Last + 1, 0));
	return 1;
}

int target_file_exists(lua_State *L) {
	target_file_t *Target = (target_file_t *)luaL_checkudata(L, 1, "target");
	const char *FileName;
	if (Target->Absolute) {
		FileName = Target->Path;
	} else {
		FileName = vfs_resolve(CurrentContext->Mounts, concat(RootPath, "/", Target->Path, 0));
	}
	struct stat Stat[1];
	if (!stat(FileName, Stat)) {
		lua_pushboolean(L, 1);
	} else {
		lua_pushboolean(L, 0);
	}
	return 1;
}

int target_file_copy(lua_State *L) {
	target_file_t *Source = (target_file_t *)luaL_checkudata(L, 1, "target");
	target_file_t *Dest = (target_file_t *)luaL_checkudata(L, 1, "target");
	if (Dest->Class != FileClass) return luaL_error(L, "Error: target is not a file");
	const char *SourcePath, *DestPath;
	if (Source->Absolute) {
		SourcePath = Source->Path;
	} else {
		SourcePath = vfs_resolve(CurrentContext->Mounts, concat(RootPath, "/", Source->Path, 0));
	}
	if (Dest->Absolute) {
		DestPath = Dest->Path;
	} else {
		DestPath = vfs_resolve(CurrentContext->Mounts, concat(RootPath, "/", Dest->Path, 0));
	}
	int Error = HX_copy_file(SourcePath, DestPath, 0);
	if (Error < 0) return luaL_error(L, strerror(-Error));
	return 0;
}

int target_div(lua_State *L) {
	target_file_t *FileTarget = (target_file_t *)luaL_checkudata(L, 1, "target");
	if (FileTarget->Class != FileClass) return luaL_error(L, "Error: target is not a file");
	const char *Path = concat(FileTarget->Path, "/", luaL_checkstring(L, 2), 0);
	target_t *Target = target_file_check(Path, FileTarget->Absolute);
	lua_rawgeti(L, LUA_REGISTRYINDEX, Target->Ref);
	return 1;
}

int target_mod(lua_State *L) {
	target_file_t *FileTarget = (target_file_t *)luaL_checkudata(L, 1, "target");
	if (FileTarget->Class != FileClass) return luaL_error(L, "Error: target is not a file");
	const char *Replacement = luaL_checkstring(L, 2);
	char *Path = concat(FileTarget->Path, ".", Replacement, 0);
	for (char *End = Path + strlen(FileTarget->Path); --End >= Path;) {
		if (*End == '.') {
			strcpy(End + 1, Replacement);
			break;
		} else if (*End == '/') {
			break;
		}
	}
	target_t *Target = target_file_check(Path, FileTarget->Absolute);
	lua_rawgeti(L, LUA_REGISTRYINDEX, Target->Ref);
	return 1;
}

struct target_meta_t {
	TARGET_FIELDS
	const char *Name;
};

static void target_meta_tostring(target_meta_t *Target, luaL_Buffer *Buffer) {
	luaL_addstring(Buffer, CurrentContext->Path);
	luaL_addstring(Buffer, "::");
	luaL_addstring(Buffer, Target->Name);
}

static time_t target_meta_hash(target_meta_t *Target, time_t *PreviousTime, int8_t PreviousHash[SHA256_BLOCK_SIZE]) {
	memset(Target->Hash, -1, SHA256_BLOCK_SIZE);
	return 0;
}

/*static target_status_t target_meta_build(target_meta_t *Target, int Updated) {
	return Updated ? STATUS_UPDATED : STATUS_UNCHANGED;
}*/

static target_class_t MetaClass[] = {{
	sizeof(target_meta_t),
	(void *)target_meta_tostring,
	(void *)target_meta_hash,
	target_default_missing
}};

ml_value_t *target_meta_new(ml_t *ML, void *Data, int Count, ml_value_t **Args) {
	if (Count < 0) return ml_error("ParamError", "missing parameter <name>");
	const char *Name = ml_to_string(Args[0]);
	const char *Id = concat("meta:", CurrentContext->Path, "::", Name, 0);
	target_meta_t *Target = (target_meta_t *)map_get(TargetCache, Id);
	if (!Target) {
		Target = (target_meta_t *)target_new(MetaClass, Id);
		Target->Name = Name;
	}
	return Target->ML;
	return 1;
}

int target_depends(lua_State *L) {
	target_t *Target = luaL_checkudata(L, 1, "target");
	int N = lua_gettop(L);
	for (int I = 2; I <= N; ++I) {
		if (lua_type(L, I) == LUA_TTABLE) {
			lua_pushnil(L);
			while (lua_next(L, I)) {
				lua_pushcfunction(L, target_depends);
				lua_pushvalue(L, 1);
				lua_pushvalue(L, -3);
				lua_call(L, 2, 0);
				lua_pop(L, 1);
			}
		} else {
			target_t *Depend = luaL_checkudata(L, I, "target");
			map_set(Target->Depends, Depend, 0);
		}
	}
	lua_pushvalue(L, 1);
	return 1;
}

static int target_build(lua_State *L) {
	target_t *Target = luaL_checkudata(L, 1, "target");
	if (Target->Build) {
		//return luaL_error(L, "Error: multiple build rules defined for target %s", Target->Id);
	}
	lua_pushvalue(L, 2);
	Target->Build = luaL_ref(L, LUA_REGISTRYINDEX);
	Target->BuildContext = CurrentContext;
	Target->LastUpdated = 0;
	lua_pushvalue(L, 1);
	return 1;
}

struct target_scan_t {
	TARGET_FIELDS
	const char *Name;
	target_t *Source;
	int Scan;
};

static void target_scan_tostring(target_scan_t *Target, luaL_Buffer *Buffer) {
}

bool scan_depends_hash(const struct map_t_node *Node, int8_t Hash[SHA256_BLOCK_SIZE]) {
	target_t *Depends = (target_t *)Node->key;
	target_update(Depends);
	for (int I = 0; I < SHA256_BLOCK_SIZE; ++I) Hash[I] ^= Depends->Hash[I];
	return true;
}

static time_t target_scan_hash(target_scan_t *Target, time_t *PreviousTime, int8_t PreviousHash[SHA256_BLOCK_SIZE]) {
	struct map_t *Scans = cache_scan_get(Target->Id);
	memset(Target->Hash, 0, SHA256_BLOCK_SIZE);
	if (Scans) HXmap_qfe(Scans, (void *)scan_depends_hash, Target->Hash);
	return 0;
}

static target_class_t ScanClass[] = {{
	sizeof(target_scan_t),
	(void *)target_scan_tostring,
	(void *)target_scan_hash,
	target_default_missing
}};

static int build_scan_target(lua_State *L) {
	target_scan_t *Target = (target_scan_t *)luaL_checkudata(L, 1, "target");
	lua_pushcfunction(L, msghandler);
	lua_rawgeti(L, LUA_REGISTRYINDEX, Target->Scan);
	lua_rawgeti(L, LUA_REGISTRYINDEX, Target->Source->Ref);
	if (lua_pcall(L, 1, 1, -3) != LUA_OK) {
		fprintf(stderr, "Error: %s: %s", Target->Id, lua_tostring(L, -1));
		exit(1);
	}
	luaL_checktype(L, -1, LUA_TTABLE);
	struct map_t *Scans = HXmap_init(HXMAPT_DEFAULT, HXMAP_SINGULAR);
	lua_pushnil(L);
	while (lua_next(L, -2)) {
		target_t *ScanTarget = (target_t *)luaL_checkudata(L, -1, "target");
		map_set(Scans, ScanTarget, 0);
		lua_pop(L, 1);
	}
	lua_pop(L, 2);
	cache_scan_set(Target->Id, Scans);
	return 0;
}

int target_scan_new(lua_State *L) {
	target_t *ParentTarget = (target_t *)luaL_checkudata(L, 1, "target");
	const char *Name = luaL_checkstring(L, 2);
	const char *Id = concat("scan:", ParentTarget->Id, "::", Name, 0);
	target_scan_t *Target = (target_scan_t *)map_get(TargetCache, Id);
	if (!Target) {
		Target = (target_scan_t *)target_new(ScanClass, Id);
		map_set(Target->Depends, ParentTarget, 0);
		Target->Name = Name;
		Target->Source = ParentTarget;
		Target->Build = BuildScanTarget;
		Target->BuildContext = CurrentContext;
		lua_pushvalue(L, 3);
		Target->Scan = luaL_ref(L, LUA_REGISTRYINDEX);
	}
	lua_rawgeti(L, LUA_REGISTRYINDEX, Target->Ref);
	return 1;
}

struct target_symb_t {
	TARGET_FIELDS
	const char *Name;
	const char *Path;
};

static void target_symb_tostring(target_symb_t *Target, luaL_Buffer *Buffer) {
}

static time_t target_symb_hash(target_symb_t *Target, time_t *PreviousTime, int8_t PreviousHash[SHA256_BLOCK_SIZE]) {
	context_t *Context = context_find(Target->Path);
	context_symb_get(Context, Target->Name);
	target_value_hash(Target->Hash);
	lua_pop(L, 1);
	return 0;
}

static target_class_t SymbClass[] = {{
	sizeof(target_symb_t),
	(void *)target_symb_tostring,
	(void *)target_symb_hash,
	target_default_missing
}};

target_t *target_symb_new(const char *Name) {
	const char *Id = concat("symb:", CurrentContext->Path, CurrentContext->Name, "/", Name, 0);
	target_symb_t *Target = (target_symb_t *)map_get(TargetCache, Id);
	if (!Target) {
		Target = (target_symb_t *)target_new(SymbClass, Id);
		Target->Path = CurrentContext->Path;
		Target->Name = Name;
	}
	return (target_t *)Target;
}

static int target_index(lua_State *L) {
	target_t *Target = luaL_checkudata(L, 1, "target");
	const char *Method = luaL_checkstring(L, 2);
	if (!strcmp(Method, "depends")) {
		lua_pushcfunction(L, target_depends);
		return 1;
	}
	if (!strcmp(Method, "build")) {
		lua_pushcfunction(L, target_build);
		return 1;
	}
	if (!strcmp(Method, "scan")) {
		lua_pushcfunction(L, target_scan_new);
		return 1;
	}
	if (Target->Class == FileClass) {
		if (!strcmp(Method, "dir")) {
			lua_pushcfunction(L, target_file_dir);
			return 1;
		}
		if (!strcmp(Method, "basename")) {
			lua_pushcfunction(L, target_file_basename);
			return 1;
		}
		if (!strcmp(Method, "exists")) {
			lua_pushcfunction(L, target_file_exists);
			return 1;
		}
		if (!strcmp(Method, "copy")) {
			lua_pushcfunction(L, target_file_copy);
			return 1;
		}
	}
	return 0;
}

target_t *target_find(const char *Id) {
	target_t *Target = (target_t *)map_get(TargetCache, Id);
	if (Target) return Target;
	if (!memcmp(Id, "file", 4)) return target_file_check(Id + 5, Id[5] == '/');
	if (!memcmp(Id, "symb", 4)) {
		Id = concat(Id, 0);
		target_symb_t *Target = (target_symb_t *)target_new(SymbClass, Id);
		const char *Name;
		for (Name = Id + strlen(Id); --Name > Id + 5;) {
			if (*Name == '/') break;
		}
		size_t PathLength = Name - Id - 5;
		char *Path = GC_malloc_atomic(PathLength);
		memcpy(Path, Id + 5, PathLength);
		Path[PathLength] = 0;
		Target->Path = Path;
		Target->Name = Name + 1;
		return (target_t *)Target;
	}
	return 0;
}

target_t *target_get(const char *Id) {
	return (target_t *)map_get(TargetCache, Id);
}

bool target_print_fn(const struct map_t_node *Node, void *Data) {
	target_t *Target = (target_t *)Node->data;
	printf("%s\n", Target->Id);
	return true;
}

void target_list() {
	HXmap_qfe(TargetCache, target_print_fn, 0);
}

static const luaL_Reg TargetMethods[] = {
	{"__tostring", target_tostring},
	{"__concat", target_concat},
	{"__index", target_index},
	{"__call", target_depends},
	{"__div", target_div},
	{"__mod", target_mod},
	{0, 0}
};

void target_init() {
	luaL_newmetatable(L, "target");
	luaL_setfuncs(L, TargetMethods, 0);
	TargetMetatable = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_pushcfunction(L, build_scan_target);
	BuildScanTarget = luaL_ref(L, LUA_REGISTRYINDEX);
	TargetCache = HXmap_init(HXMAPT_DEFAULT, HXMAP_SCKEY);
}
