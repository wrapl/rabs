#ifndef TARGETCACHE_H
#define TARGETCACHE_H

#include <stddef.h>

typedef struct target_t target_t;

typedef struct {target_t **Slot; const char *Id;} target_id_slot;
typedef struct {target_t **Slot; size_t Index;} target_index_slot;

void targetcache_init();
target_id_slot targetcache_index(size_t Index);
target_index_slot targetcache_lookup(const char *Id);
int targetcache_size();

#endif
