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
		printf("Growing target cache...\n");
		size_t NewCacheSize = CacheSize;
		do NewCacheSize *= 2; while (Index >= NewCacheSize);
		target_t **NewCache = anew(target_t *, NewCacheSize);
		memcpy(NewCache, Cache, CacheSize * sizeof(target_t *));
		memset(NewCache + CacheSize, 0, CacheSize * sizeof(target_t *));
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
		printf("Growing target cache...\n");
		size_t NewCacheSize = CacheSize * 2;
		do NewCacheSize *= 2; while (Index >= NewCacheSize);
		target_t **NewCache = anew(target_t *, NewCacheSize);
		memcpy(NewCache, Cache, CacheSize * sizeof(target_t *));
		memset(NewCache + CacheSize, 0, CacheSize * sizeof(target_t *));
		CacheSize = NewCacheSize;
		Cache = NewCache;
	}
	return (target_index_slot){Cache + Index, Index};
}
