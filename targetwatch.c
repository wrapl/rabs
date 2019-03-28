#include "targetwatch.h"
#include "target.h"
#include "minilang/stringmap.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <gc/gc.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/inotify.h>

#define INITIAL_SIZE 256

typedef struct directory_t {
	const char *Path;
	stringmap_t Files[1];
} directory_t;

static directory_t **DirectoriesByHandle = 0;
static size_t MaxHandles = 0;
static stringmap_t DirectoriesByName[1] = {STRINGMAP_INIT};
static int WatchHandle;

void targetwatch_init() {
	MaxHandles = INITIAL_SIZE;
	DirectoriesByHandle = anew(directory_t *, MaxHandles);
	WatchHandle = inotify_init();
}

void targetwatch_add(const char *FilePath) {
	struct stat Stat[1];
	if (stat(FilePath, Stat)) return;
	if (S_ISDIR(Stat->st_mode)) {
		/*directory_t **Slot = (directory_t **)stringmap_slot(DirectoriesByName, FilePath);
		directory_t *Directory = Slot[0];
		if (!Directory) {
			printf("Adding watch on %s\n", FilePath);
			Directory = Slot[0] = new(directory_t);
			Directory->Path = FilePath;
			int Handle = inotify_add_watch(WatchHandle, FilePath, IN_CREATE | IN_DELETE | IN_MOVE | IN_CLOSE_WRITE);
			if (Handle > MaxHandles) {
				directory_t **NewWatches = anew(directory_t *, Handle + 1);
				memcpy(NewWatches, DirectoriesByHandle, MaxHandles * sizeof(directory_t *));
				DirectoriesByHandle = NewWatches;
				MaxHandles = Handle;
			}
			DirectoriesByHandle[Handle] = Directory;
		}*/
	} else {
		int Length = strlen(FilePath);
		char *DirectoryPath = snew(Length + 1);
		char *FileName = stpcpy(DirectoryPath, FilePath);
		while (FileName[-1] != '/') --FileName;
		FileName[-1] = 0;
		directory_t **Slot = (directory_t **)stringmap_slot(DirectoriesByName, DirectoryPath);
		directory_t *Directory = Slot[0];
		if (!Directory) {
			Directory = Slot[0] = new(directory_t);
			Directory->Path = DirectoryPath;
			int Handle = inotify_add_watch(WatchHandle, DirectoryPath, IN_CREATE | IN_DELETE | IN_MOVE | IN_CLOSE_WRITE);
			if (Handle > MaxHandles) {
				directory_t **NewWatches = anew(directory_t *, Handle + 1);
				memcpy(NewWatches, DirectoriesByHandle, MaxHandles * sizeof(directory_t *));
				DirectoriesByHandle = NewWatches;
				MaxHandles = Handle;
			}
			DirectoriesByHandle[Handle] = Directory;
		}
		printf("Adding watch on %s in %s\n", FileName, DirectoryPath);
		stringmap_insert(Directory->Files, FileName, FileName);
	}
}

#define BUFFER_SIZE (sizeof(struct inotify_event) + NAME_MAX + 1)

void targetwatch_wait(void (restart)()) {
	char Buffer[BUFFER_SIZE];
	for (;;) {
		ssize_t Bytes = read(WatchHandle, Buffer, BUFFER_SIZE);
		char *Next = Buffer, *Limit = Buffer + Bytes;
		pthread_mutex_lock(InterpreterLock);
		do {
			struct inotify_event *Event = (struct inotify_event *)Next;
			directory_t *Directory = DirectoriesByHandle[Event->wd];
			if (Directory) {
				printf("Event %x for %s in %s\n", Event->mask, Event->name, Directory->Path);
				if (Event->mask & IN_CLOSE_WRITE) {
					if (stringmap_search(Directory->Files, Event->name)) {
						restart();
					}
				}
			}
			Next += sizeof(struct inotify_event) + Event->len;
		} while (Next < Limit);
		pthread_mutex_unlock(InterpreterLock);
	}

}
