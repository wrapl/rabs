#ifndef RABS_H
#define RABS_H

#include <pthread.h>
#include "minilang.h"
#include "minilang/ml_macros.h"

extern const char *RootPath, *SystemName;
extern __thread const char *CurrentDirectory;

#define CURRENT_VERSION "2.0.4"
#define WORKING_VERSION "2.0.4"

#endif
