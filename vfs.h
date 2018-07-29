#ifndef VFS_H
#define VFS_H

void vfs_mount(const char *Path, const char *Target, int Absolute);

char *vfs_resolve(const char *Path);
char *vfs_unsolve(const char *Path);

const char **vfs_resolve_all(const char *Path);

int vfs_resolve_foreach(const char *Path, void *Data, int (*callback)(void *Data, const char *Path));

#endif
