#ifndef RABS_H
#define RABS_H

#include <pthread.h>
#include "minilang.h"

extern const char *RootPath, *SystemName;
extern __thread const char *CurrentDirectory;
void restart();

#endif
