#ifndef FILE_H
#define FILE_H

#include "minilang.h"

void extras_init();

ml_value_t *file_open(ml_t *ML, void *Data, int Count, ml_value_t **Args);

#endif
