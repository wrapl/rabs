#ifndef TARGETCACHE_H
#define TARGETCACHE_H

#include <stddef.h>

typedef struct target_t target_t;

void targetcache_init(int CacheSize);
struct target_t **targetcache_lookup(const char *Id, size_t IdLength);
int targetcache_size();

#endif
