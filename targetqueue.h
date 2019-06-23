#ifndef TARGETQUEUE_H
#define TARGETQUEUE_H

#include "target.h"

#define PRIORITY_INVALID -1

void targetqueue_init();
void targetqueue_insert(target_t *Target);
void target_priority_invalidate(target_t *Target);
target_t *targetqueue_next();

#endif
