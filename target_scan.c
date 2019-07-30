#include "target_scan.h"

#include "rabs.h"
#include "util.h"
#include "cache.h"
#include "targetcache.h"
#include <string.h>
#include <gc/gc.h>

struct target_scan_t {
	TARGET_FIELDS
	const char *Name;
	target_t *Source;
	targetset_t *Scans;
	ml_value_t *Rebuild;
};

ml_type_t *ScanTargetT;

static int depends_hash_fn(target_t *Depend, unsigned char Hash[SHA256_BLOCK_SIZE]) {
	for (int I = 0; I < SHA256_BLOCK_SIZE; ++I) Hash[I] ^= Depend->Hash[I];
	return 0;
}

time_t target_scan_hash(target_scan_t *Target, time_t PreviousTime, unsigned char PreviousHash[SHA256_BLOCK_SIZE]) {
	targetset_t *Scans = cache_scan_get((target_t *)Target);
	if (Scans) targetset_foreach(Scans, Target->Hash, (void *)depends_hash_fn);
	return 0;
}

static ml_value_t *target_scan_source(void *Data, int Count, ml_value_t **Args) {
	target_scan_t *Target = (target_scan_t *)Args[0];
	return (ml_value_t *)Target->Source;
}

ml_value_t *target_scan_new(void *Data, int Count, ml_value_t **Args) {
	target_t *Source = (target_t *)Args[0];
	const char *Name = ml_string_value(Args[1]);
	const char *Id = concat("scan:", Source->Id, "::", Name, NULL);
	target_t **Slot = targetcache_lookup(Id);
	if (!Slot[0]) {
		target_scan_t *Target = target_new(target_scan_t, ScanTargetT, Id, Slot);
		Target->Source = Source;
		Target->Name = Name;
		targetset_insert(Target->Depends, Source);
	}
	return (ml_value_t *)Slot[0];
}

target_t *target_scan_create(const char *Id, context_t *BuildContext, target_t **Slot) {
	target_scan_t *Target = target_new(target_scan_t, ScanTargetT, Id, Slot);
	const char *Name;
	for (Name = Id + strlen(Id) - 1; --Name > Id + 5;) {
		if (Name[0] == ':' && Name[1] == ':') break;
	}
	size_t ParentIdLength = Name - Id - 5;
	char *ParentId = snew(ParentIdLength + 1);
	memcpy(ParentId, Id + 5, ParentIdLength);
	ParentId[ParentIdLength] = 0;
	Target->Source = target_find(ParentId);
	Target->Name = Name + 2;
	return (target_t *)Target;
}

void target_scan_init() {
	ScanTargetT = ml_type(TargetT, "scan-target");
	ml_method_by_name("source", 0, target_scan_source, ScanTargetT, NULL);
}
