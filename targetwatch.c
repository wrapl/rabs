#include "targetwatch.h"
#include "target.h"
#include "minilang/stringmap.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <gc/gc.h>
#include <sys/inotify.h>
#include <limits.h>

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

void targetwatch_add(const char *FilePath, target_t *Target) {
	int Length = strlen(FilePath);
	char *DirectoryPath = snew(Length + 1);
	char *FileName = stpcpy(DirectoryPath, FilePath);
	while (FileName[-1] != '/') --FileName;
	FileName[-1] = 0;
	printf("Adding watch on %s in %s\n", FileName, DirectoryPath);
	directory_t **Slot = (directory_t **)stringmap_slot(DirectoriesByName, DirectoryPath);
	directory_t *Directory = Slot[0];
	if (!Directory) {
		Directory = Slot[0] = new(directory_t);
		Directory->Path = DirectoryPath;
		int Handle = inotify_add_watch(WatchHandle, DirectoryPath, IN_CLOSE_WRITE);
		if (Handle > MaxHandles) {
			directory_t **NewWatches = anew(directory_t *, Handle + 1);
			memcpy(NewWatches, DirectoriesByHandle, MaxHandles * sizeof(directory_t *));
			DirectoriesByHandle = NewWatches;
			MaxHandles = Handle;
		}
		DirectoriesByHandle[Handle] = Directory;
	}
	stringmap_insert(Directory->Files, FileName, Target);
}

#define BUFFER_SIZE (sizeof(struct inotify_event) + NAME_MAX + 1)

void targetwatch_wait() {
	char Buffer[BUFFER_SIZE];
	for (;;) {
		ssize_t Bytes = read(WatchHandle, Buffer, BUFFER_SIZE);
		char *Next = Buffer, *Limit = Buffer + Bytes;
		pthread_mutex_lock(GlobalLock);
		do {
			struct inotify_event *Event = (struct inotify_event *)Next;
			directory_t *Directory = DirectoriesByHandle[Event->wd];
			if (Directory) {
				printf("Event %x for %s in %s\n", Event->mask, Event->name, Directory->Path);
				target_t *Target = stringmap_search(Directory->Files, Event->name);
				if (Target) target_recheck(Target);
			}
			Next += sizeof(struct inotify_event) + Event->len;
		} while (Next < Limit);
		pthread_mutex_unlock(GlobalLock);
	}

}
