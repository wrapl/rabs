#ifndef VFS_H
#define VFS_H

typedef struct vmount_t vmount_t;

const vmount_t *vfs_mount(const vmount_t *Previous, const char *Path, const char *Target);

char *vfs_resolve(const vmount_t *Mount, const char *Path);
char *vfs_unsolve(const vmount_t *Mount, const char *Path);

const char **vfs_resolve_all(const vmount_t *Mount, const char *Path);

int vfs_resolve_foreach(const vmount_t *Mount, const char *Path, void *Data, int (*callback)(void *Data, const char *Path));

void vfs_init();

#endif
