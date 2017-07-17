#ifndef CACHE_H
#define CACHE_H

#include <time.h>
#include "sha256.h"
#include "stringmap.h"

void cache_open(const char *RootPath);
void cache_close();

void cache_hash_get(const char *Id, int *LastUpdated, int *LastChecked, time_t *FileTime, BYTE Digest[SHA256_BLOCK_SIZE]);
void cache_hash_set(const char *Id, time_t FileTime, BYTE Digest[SHA256_BLOCK_SIZE]);
void cache_last_check_set(const char *Id);

stringmap_t *cache_depends_get(const char *Id);
void cache_depends_set(const char *Id, stringmap_t *Scans);

stringmap_t *cache_scan_get(const char *Id);
void cache_scan_set(const char *Id, stringmap_t *Scans);

extern int CurrentVersion;

#endif
