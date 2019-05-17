#ifndef TARGETQUEUE_H
#define TARGETQUEUE_H

#include "target.h"

void targetqueue_init();
void targetqueue_insert(target_t *Target);
target_t *targetqueue_next();

#endif
