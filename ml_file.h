#ifndef ML_FILE_H
#define ML_FILE_H

#include "minilang.h"

void ml_file_init();

ml_value_t *ml_file_open(void *Data, int Count, ml_value_t **Args);

#endif
