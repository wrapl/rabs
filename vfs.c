#include <gc/gc.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include "vfs.h"
#include "util.h"

#define new(T) ((T *)GC_MALLOC(sizeof(T)))

struct vmount_t {
	const vmount_t *Previous;
	const char *Path;
	const char *Target;
};

extern const char *RootPath;

const vmount_t *vfs_mount(const vmount_t *Previous, const char *Path, const char *Target) {
	vmount_t *Mount = new(vmount_t);
	Mount->Previous = Previous;
	Mount->Path = concat(RootPath, Path, NULL);
	if (Target[0] != '/') {
		Mount->Target = concat(RootPath, Target, NULL);
	} else {
		Mount->Target = concat(Target, NULL);
	}
	return Mount;
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

char *vfs_resolve(const vmount_t *Mount, const char *Path) {
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

int vfs_resolve_foreach(const vmount_t *Mount, const char *Path, void *Data, int (*callback)(void *Data, const char *Path)) {
	struct stat Stat[1];
	if (stat(Path, Stat) == 0) if (callback(Data, concat(Path, 0))) return 1;
	return vfs_resolve_foreach0(Mount, Path, Data, callback);
}

static char *unsolve0(const vmount_t *Mount, const char *Path) {
	while (Mount) {
		const char *Suffix = match_prefix(Path, Mount->Target);
		if (Suffix) {
			char *Resolved = concat(Mount->Path, Suffix, NULL);
			struct stat Stat[1];
			if (stat(Resolved, Stat) == 0) return Resolved;
			Resolved = unsolve0(Mount->Previous, Resolved);
			if (Resolved) return Resolved;
		}
		Mount = Mount->Previous;
	}
	return 0;
}

char *vfs_unsolve(const vmount_t *Mount, const char *Path) {
	const char *Orig = Path;
	while (Mount) {
		const char *Suffix = match_prefix(Path, Mount->Target);
		if (Suffix) Path = concat(Mount->Path, Suffix, NULL);
		Mount = Mount->Previous;
	}
	if (Path == Orig) Path = concat(Path, NULL);
	return (char *)Path;
}

void vfs_init() {
	
}
