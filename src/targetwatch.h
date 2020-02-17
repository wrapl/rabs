#ifndef TARGETWATCH_H
#define TARGETWATCH_H

typedef struct target_t target_t;

void targetwatch_init();
void targetwatch_add(const char *FilePath);
void targetwatch_wait(void (restart)());

#endif
