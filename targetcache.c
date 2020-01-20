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
	Cache = (target_t **)GC_MALLOC_IGNORE_OFF_PAGE(CacheSize * sizeof(target_t *));
}

target_id_slot targetcache_index(size_t Index) {
	if (Index >= CacheSize) {
		size_t NewCacheSize = CacheSize;
		do NewCacheSize *= 2; while (Index >= NewCacheSize);
		Cache = (target_t **)GC_REALLOC(Cache, NewCacheSize * sizeof(target_t *));
		CacheSize = NewCacheSize;
	}
	target_t *Target = Cache[Index];
	const char *Id = Target ? Target->Id : cache_target_index_to_id(Index);
	return (target_id_slot){Cache + Index, Id};
}

target_index_slot targetcache_insert(const char *Id) {
	size_t Index = cache_target_id_to_index(Id);
	if (Index >= CacheSize) {
		size_t NewCacheSize = CacheSize * 2;
		do NewCacheSize *= 2; while (Index >= NewCacheSize);
		Cache = (target_t **)GC_REALLOC(Cache, NewCacheSize * sizeof(target_t *));
		CacheSize = NewCacheSize;
	}
	return (target_index_slot){Cache + Index, Index};
}

target_index_slot targetcache_search(const char *Id) {
	size_t Index = cache_target_id_to_index_existing(Id);
	if (Index == 0xFFFFFFFF) return (target_index_slot){NULL, Index};
	if (Index >= CacheSize) {
		size_t NewCacheSize = CacheSize * 2;
		do NewCacheSize *= 2; while (Index >= NewCacheSize);
		Cache = (target_t **)GC_REALLOC(Cache, NewCacheSize * sizeof(target_t *));
		CacheSize = NewCacheSize;
	}
	return (target_index_slot){Cache + Index, Index};
}
