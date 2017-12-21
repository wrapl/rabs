#ifndef RABS_H
#define RABS_H

#include <pthread.h>
#include "minilang.h"

extern pthread_mutex_t GlobalLock[1];
extern const char *RootPath, *SystemName;
extern __thread const char *CurrentPath;

#endif
