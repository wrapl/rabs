#ifndef LIBRARY_H
#define LIBRARY_H

#include "minilang.h"
#include "minilang/stringmap.h"

void library_init();
ml_value_t *library_load(const char *Path, stringmap_t *Globals);

#endif
