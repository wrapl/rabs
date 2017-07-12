#ifndef TARGET_H
#define TARGET_H

#include <time.h>
#include "sha256.h"
#include "minilang.h"

typedef struct target_t target_t;
typedef struct target_class_t target_class_t;

struct target_class_t {
	size_t Size;
	const ml_type_t *Type;
	void (*tostring)(target_t *Target, luaL_Buffer *Buffer);
	time_t (*hash)(target_t *Target, time_t FileTime, int8_t Hash[SHA256_BLOCK_SIZE]);
	int (*missing)(target_t *Target);
};

#define TARGET_FIELDS \
	const target_class_t *Class; \
	ml_value_t *ML, *Build; \
	int LastUpdated; \
	struct context_t *BuildContext; \
	const char *Id; \
	struct HXmap *Depends; \
	int8_t Hash[SHA256_BLOCK_SIZE];

struct target_t {
	TARGET_FIELDS
};

void target_init();

int target_dir_new(lua_State *L);
int target_file_new(lua_State *L);
int target_meta_new(lua_State *L);

target_t *target_symb_new(const char *Name);

int target_tostring(lua_State *L);
void target_depends_add(target_t *Target, target_t *Depend);
void target_update(target_t *Target);
void target_query(target_t *Target);
void target_depends_auto(target_t *Depend);
target_t *target_find(const char *Id);
target_t *target_get(const char *Id);
void target_push(target_t *Target);
void target_list();

#endif
