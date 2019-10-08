#include "targetcache.h"
#include "target.h"
#include "minilang/stringmap.h"
#include "minilang/ml_macros.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <gc/gc.h>
#include <cache.h>

static target_t **Cache;
static size_t CacheSize = 1;

void targetcache_init() {
	size_t InitialSize = cache_target_count();
	while (CacheSize < InitialSize) CacheSize *= 2;
	Cache = anew(target_t *, InitialSize);
}

target_id_slot targetcache_index(size_t Index) {
	if (Index >= CacheSize) {
		size_t NewCacheSize = CacheSize * 2;
		target_t **NewCache = anew(target_t *, NewCacheSize);
		memcpy(NewCache, Cache, CacheSize * sizeof(target_t *));
		GC_free(Cache);
		CacheSize = NewCacheSize;
		Cache = NewCache;
	}
	target_t *Target = Cache[Index];
	const char *Id = Target ? Target->Id : cache_target_index_to_id(Index);
	return (target_id_slot){Cache + Index, Id};
}

target_index_slot targetcache_lookup(const char *Id) {
	size_t Index = cache_target_id_to_index(Id);
	if (Index >= CacheSize) {
		size_t NewCacheSize = CacheSize * 2;
		target_t **NewCache = anew(target_t *, NewCacheSize);
		memcpy(NewCache, Cache, CacheSize * sizeof(target_t *));
		GC_free(Cache);
		CacheSize = NewCacheSize;
		Cache = NewCache;
	}
	return (target_index_slot){Cache + Index, Index};
}


target_t *target_find(const char *Id) {
	target_index_slot R = targetcache_lookup(Id);
	if (R.Slot[0]) return R.Slot[0];
	Id = GC_strdup(Id);
	if (!memcmp(Id, "file:", 5)) {
		return target_file_create(Id, CurrentContext, R.Index, R.Slot);
	}
	if (!memcmp(Id, "symb:", 5)) {
		return target_symb_create(Id, CurrentContext, R.Index, R.Slot);
	}
	if (!memcmp(Id, "expr:", 5)) {
		return target_expr_create(Id, CurrentContext, R.Index, R.Slot);
	}
	if (!memcmp(Id, "scan:", 5)) {
		return target_scan_create(Id, CurrentContext, R.Index, R.Slot);
	}
	if (!memcmp(Id, "meta:", 5)) {
		return target_meta_create(Id, CurrentContext, R.Index, R.Slot);
	}
	fprintf(stderr, "internal error: unknown target type: %s\n", Id);
	return 0;
}

target_t *target_load(const char *Id, size_t Index, target_t **Slot) {
	if (!memcmp(Id, "file:", 5)) {
		return target_file_create(Id, CurrentContext, Index, Slot);
	}
	if (!memcmp(Id, "symb:", 5)) {
		return target_symb_create(Id, CurrentContext, Index, Slot);
	}
	if (!memcmp(Id, "expr:", 5)) {
		return target_expr_create(Id, CurrentContext, Index, Slot);
	}
	if (!memcmp(Id, "scan:", 5)) {
		return target_scan_create(Id, CurrentContext, Index, Slot);
	}
	if (!memcmp(Id, "meta:", 5)) {
		return target_meta_create(Id, CurrentContext, Index, Slot);
	}
	fprintf(stderr, "internal error: unknown target type: %s\n", Id);
	return 0;
}
