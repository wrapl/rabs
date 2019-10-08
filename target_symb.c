#include "target_symb.h"

#include "rabs.h"
#include "cache.h"
#include "util.h"
#include "targetcache.h"
#include <string.h>
#include <gc/gc.h>

struct target_symb_t {
	TARGET_FIELDS
	const char *Name;
	context_t *Context;
};

ml_type_t *SymbTargetT;

static ml_value_t *symb_target_deref(ml_value_t *Ref) {
	target_symb_t *Target = (target_symb_t *)Ref;
	return context_symb_get(Target->Context, Target->Name) ?: rabs_global(Target->Name);
}

static ml_value_t *symb_target_assign(ml_value_t *Ref, ml_value_t *Value) {
	target_symb_t *Target = (target_symb_t *)Ref;
	context_symb_set(Target->Context, Target->Name, Value);
	return Value;
}

time_t target_symb_hash(target_symb_t *Target, time_t PreviousTime, unsigned char PreviousHash[SHA256_BLOCK_SIZE]) {
	ml_value_t *Value = context_symb_get(Target->Context, Target->Name) ?: MLNil;
	target_value_hash(Value, Target->Hash);
	return 0;
}

target_t *target_symb_new(const char *Name) {
	char *Id;
	asprintf(&Id, "symb:%s/%s", CurrentContext->Name, Name);
	target_index_slot R = targetcache_lookup(Id);
	if (!R.Slot[0]) {
		target_symb_t *Target = target_new(target_symb_t, SymbTargetT, Id, R.Index, R.Slot);
		Target->Context = CurrentContext;
		Target->Name = Name;
	}
	return R.Slot[0];
}

void target_symb_update(const char *Name) {
	target_t *Target = target_symb_new(Name);
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
	SymbTargetT = ml_type(TargetT, "symb-target");
	SymbTargetT->deref = symb_target_deref;
	SymbTargetT->assign = symb_target_assign;
}
