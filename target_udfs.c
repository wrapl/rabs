#include "target_udfs.h"

#include <string.h>
#include "rabs.h"
#include "util.h"
#include "cache.h"
#include "targetcache.h"

struct target_udfs_t {
	TARGET_FIELDS
};

ml_type_t *UdfsTargetT;

static stringmap_t UdfsTypes[1] = {{STRINGMAP_INIT}};

time_t target_udfs_hash(target_udfs_t *Target, time_t PreviousTime, unsigned char PreviousHash[SHA256_BLOCK_SIZE]) {
	// TODO: write this
	return 0;
}

ml_value_t *target_udfs_new(void *Data, int Count, ml_value_t **Args) {
	// TODO: write this
	ML_CHECK_ARG_COUNT(1);
	ML_CHECK_ARG_TYPE(0, MLStringT);
	const char *Name = ml_string_value(Args[0]);
	const char *Id = concat("udfs:", CurrentContext->Path, "::", Name, NULL);
	target_t **Slot = targetcache_lookup(Id);
	if (!Slot[0]) {
		target_udfs_t *Target = target_new(target_udfs_t, UdfsTargetT, Id, Slot);
		// TODO: write this
	}
	return (ml_value_t *)Slot[0];
}

int target_udfs_missing(target_udfs_t *Target) {
	// TODO: write this
	return 0;
}

target_t *target_udfs_create(const char *Id, context_t *BuildContext, target_t **Slot) {
	target_udfs_t *Target = target_new(target_udfs_t, UdfsTargetT, Id, Slot);
	// TODO: write this
	Target->BuildContext = BuildContext;
	return (target_t *)Target;
}

ml_value_t *target_udfs_register(void *Data, int Count, ml_value_t **Args) {
	ML_CHECK_ARG_COUNT(1);
	ML_CHECK_ARG_TYPE(0, MLStringT);

	return MLNil;
}

void target_udfs_init() {
	UdfsTargetT = ml_type(TargetT, "udfs-target");
}
