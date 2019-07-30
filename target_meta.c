#include "target_meta.h"

#include <string.h>
#include "rabs.h"
#include "util.h"
#include "cache.h"
#include "targetcache.h"

struct target_meta_t {
	TARGET_FIELDS
	const char *Name;
};

ml_type_t *MetaTargetT;

time_t target_meta_hash(target_meta_t *Target, time_t PreviousTime, unsigned char PreviousHash[SHA256_BLOCK_SIZE], int DependsLastUpdated) {
	if (DependsLastUpdated == CurrentIteration) {
		memset(Target->Hash, 0, SHA256_BLOCK_SIZE);
		memcpy(Target->Hash, &DependsLastUpdated, sizeof(DependsLastUpdated));
	} else {
		memcpy(Target->Hash, PreviousHash, SHA256_BLOCK_SIZE);
	}
	return 0;
}

ml_value_t *target_meta_new(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	const char *Name = ml_string_value(Args[0]);
	const char *Id = concat("meta:", CurrentContext->Path, "::", Name, NULL);
	target_t **Slot = targetcache_lookup(Id);
	if (!Slot[0]) {
		target_meta_t *Target = target_new(target_meta_t, MetaTargetT, Id, Slot);
		Target->Name = Name;
	}
	return (ml_value_t *)Slot[0];
}

target_t *target_meta_create(const char *Id, context_t *BuildContext, target_t **Slot) {
	target_meta_t *Target = target_new(target_meta_t, MetaTargetT, Id, Slot);
	const char *Name;
	for (Name = Id + strlen(Id) - 1; --Name > Id + 8;) {
		if (Name[0] == ':' && Name[1] == ':') break;
	}
	Target->Name = Name;
	Target->BuildContext = BuildContext;
	return (target_t *)Target;
}

void target_meta_init() {
	MetaTargetT = ml_type(TargetT, "meta-target");
}
