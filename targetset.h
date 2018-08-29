#ifndef TARGETSET_H
#define TARGETSET_H

typedef struct target_t target_t;
typedef struct targetset_t targetset_t;

struct targetset_t {
	target_t **Targets;
	int Size, Space;
};

#define TARGETSET_INIT {0,}

void targetset_init(targetset_t *Set, int Size);
int targetset_insert(targetset_t *Set, target_t *Target);
int targetset_foreach(targetset_t *Set, void *Data, int (*callback)(target_t *, void *));

#define targetset_size(Set) (Set->Size - Set->Space)

#endif
