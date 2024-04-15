#include <gc/gc.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include "vfs.h"
#include "util.h"

#define new(T) ((T *)GC_MALLOC(sizeof(T)))

typedef struct vmount_t vmount_t;

struct vmount_t {
	const vmount_t *Previous;
	const char *Path;
	const char *Target;
};

extern const char *RootPath;
static vmount_t *Mounts = 0;

void vfs_mount(const char *Path, const char *Target, int Absolute) {
	vmount_t *Mount = new(vmount_t);
	Mount->Previous = Mounts;
	Mount->Path = concat(RootPath, Path, NULL);
	if (Absolute) {
		Mount->Target = concat(Target, NULL);
	} else {
		Mount->Target = concat(RootPath, Target, NULL);
	}
	Mounts = Mount;
}

static char *resolve0(const vmount_t *Mount, const char *Path) {
	while (Mount) {
		const char *Suffix = match_prefix(Path, Mount->Path);
		if (Suffix) {
			char *Resolved = concat(Mount->Target, Suffix, NULL);
			struct stat Stat[1];
			if (stat(Resolved, Stat) == 0) return Resolved;
			Resolved = resolve0(Mount->Previous, Resolved);
			if (Resolved) return Resolved;
		}
		Mount = Mount->Previous;
	}
	return 0;
}

char *vfs_resolve(const char *Path) {
	const vmount_t *Mount = Mounts;
	struct stat Stat[1];
	if (stat(Path, Stat) == 0) return concat(Path, NULL);
	return resolve0(Mount, Path) ?: concat(Path, NULL);
}

static int vfs_resolve_foreach0(const vmount_t *Mount, const char *Path, void *Data, int (*callback)(void *Data, const char *Path)) {
	while (Mount) {
		const char *Suffix = match_prefix(Path, Mount->Path);
		if (Suffix) {
			char *Resolved = concat(Mount->Target, Suffix, NULL);
			struct stat Stat[1];
			if (stat(Resolved, Stat) == 0) if (callback(Data, Resolved)) return 1;
			if (vfs_resolve_foreach0(Mount->Previous, Resolved, Data, callback)) return 1;
		}
		Mount = Mount->Previous;
	}
	return 0;
}

int vfs_resolve_foreach(const char *Path, void *Data, int (*callback)(void *Data, const char *Path)) {
	const vmount_t *Mount = Mounts;
	struct stat Stat[1];
	if (stat(Path, Stat) == 0) if (callback(Data, concat(Path, NULL))) return 1;
	return vfs_resolve_foreach0(Mount, Path, Data, callback);
}

char *vfs_unsolve(const char *Path) {
	const vmount_t *Mount = Mounts;
	const char *Orig = Path;
	while (Mount) {
		//printf("%s -> %s\n", Mount->Path, Mount->Target);
		const char *Suffix = match_prefix(Path, Mount->Target);
		if (Suffix) Path = vfs_unsolve(concat(Mount->Path, Suffix, NULL));
		Mount = Mount->Previous;
	}
	if (Path == Orig) Path = concat(Path, NULL);
	return (char *)Path;
}
