#include "target_meta.h"

#include <string.h>
#include "rabs.h"
#include "util.h"
#include "cache.h"
#include "targetcache.h"

#undef ML_CATEGORY
#define ML_CATEGORY "meta"

struct target_meta_t {
	target_t Base;
	const char *Name;
};

ML_TYPE(MetaTargetT, (TargetT), "meta-target");

time_t target_meta_hash(target_meta_t *Target, time_t PreviousTime, unsigned char PreviousHash[SHA256_BLOCK_SIZE], int DependsLastUpdated) {
	if (DependsLastUpdated == CurrentIteration) {
		memset(Target->Base.Hash, 0, SHA256_BLOCK_SIZE);
		memcpy(Target->Base.Hash, &DependsLastUpdated, sizeof(DependsLastUpdated));
	} else {
		memcpy(Target->Base.Hash, PreviousHash, SHA256_BLOCK_SIZE);
	}
	return 0;
}

ml_value_t *target_meta_new(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	const char *Name = ml_string_value(Args[0]);
	char *Id;
	asprintf(&Id, "meta:%s::%s", CurrentContext->Path, Name);
	target_index_slot R = targetcache_insert(Id);
	if (!R.Slot[0]) {
		target_meta_t *Target = target_new(target_meta_t, MetaTargetT, Id, R.Index, R.Slot);
		Target->Name = Name;
	}
	if (Count > 1) {
		target_t *Target = R.Slot[0];
		Target->Build = Args[1];
		Target->BuildContext = CurrentContext;
	}
	return (ml_value_t *)R.Slot[0];
}

ML_FUNCTION(Meta) {
//<Name:string
//>metatarget
// Returns a new meta target in the current context with name :mini:`Name`.
	return target_meta_new(Data, Count, Args);
}

target_t *target_meta_create(const char *Id, context_t *BuildContext, size_t Index, target_t **Slot) {
	target_meta_t *Target = target_new(target_meta_t, MetaTargetT, Id, Index, Slot);
	const char *Name;
	for (Name = Id + strlen(Id) - 1; --Name > Id + 8;) {
		if (Name[0] == ':' && Name[1] == ':') break;
	}
	Target->Name = Name;
	Target->Base.BuildContext = BuildContext;
	return (target_t *)Target;
}

void target_meta_init() {
#include "target_meta_init.c"
}
