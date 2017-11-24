#ifndef RABS_H
#define RABS_H

#include <pthread.h>
#include "minilang.h"

extern pthread_mutex_t GlobalLock[1];
extern const char *RootPath, *SystemName;
extern ml_value_t *StringifyMethod;
extern __thread const char *CurrentPath;

#endif
