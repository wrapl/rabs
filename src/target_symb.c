#include "target_symb.h"

#include "rabs.h"
#include "cache.h"
#include "util.h"
#include "targetcache.h"
#include <string.h>
#include <gc/gc.h>

struct target_symb_t {
	target_t Base;
	const char *Name;
	context_t *Context;
};

static ml_value_t *symb_target_deref(target_symb_t *Target) {
	return context_symb_get(Target->Context, Target->Name) ?: rabs_global(Target->Name);
}

static ml_value_t *symb_target_assign(target_symb_t *Target, ml_value_t *Value) {
	context_symb_set(Target->Context, Target->Name, Value);
	return Value;
}

static void symb_target_call(ml_state_t *Caller, target_symb_t *Target, int Count, ml_value_t **Args) {
	ml_value_t *Value = symb_target_deref(Target);
	return ml_call(Caller, Value, Count, Args);
}

ML_TYPE(SymbTargetT, (TargetT), "symb-target",
	.deref = (void *)symb_target_deref,
	.assign = (void *)symb_target_assign,
	.call = (void *)symb_target_call
);

time_t target_symb_hash(target_symb_t *Target, time_t PreviousTime, unsigned char PreviousHash[SHA256_BLOCK_SIZE]) {
	ml_value_t *Value = context_symb_get(Target->Context, Target->Name) ?: MLNil;
	target_value_hash(Value, Target->Base.Hash);
	return 0;
}

target_t *target_symb_new(context_t *Context, const char *Name) {
	char *Id;
	asprintf(&Id, "symb:%s/%s", Context->Name, Name);
	target_index_slot R = targetcache_insert(Id);
	if (!R.Slot[0]) {
		target_symb_t *Target = target_new(target_symb_t, SymbTargetT, Id, R.Index, R.Slot);
		Target->Context = Context;
		Target->Name = Name;
	}
	return R.Slot[0];
}

void target_symb_update(const char *Name) {
	target_t *Target = target_symb_new(CurrentContext, Name);
	unsigned char Previous[SHA256_BLOCK_SIZE];
	int LastUpdated, LastChecked;
	time_t FileTime = 0;
	cache_hash_get(Target, &LastUpdated, &LastChecked, &FileTime, Previous);
	FileTime = target_hash(Target, FileTime, Previous, 0);
	if (!LastUpdated || memcmp(Previous, Target->Hash, SHA256_BLOCK_SIZE)) {
		Target->LastUpdated = CurrentIteration;
		cache_hash_set(Target, FileTime);
	} else {
		Target->LastUpdated = LastUpdated;
		cache_last_check_set(Target, FileTime);
	}
}

target_t *target_symb_create(const char *Id, context_t *BuildContext, size_t Index, target_t **Slot) {
	target_symb_t *Target = target_new(target_symb_t, SymbTargetT, Id, Index, Slot);
	const char *Name;
	for (Name = Id + strlen(Id); --Name > Id + 5;) {
		if (*Name == '/') break;
	}
	size_t PathLength = Name - Id - 5;
	char *Path = snew(PathLength + 1);
	memcpy(Path, Id + 5, PathLength);
	Path[PathLength] = 0;
	Target->Context = context_make(Path);
	Target->Name = Name + 1;
	return (target_t *)Target;
}

void target_symb_init() {
#include "target_symb_init.c"
}
