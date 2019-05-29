#ifndef RABS_H
#define RABS_H

#include <pthread.h>
#include "minilang.h"
#include "minilang/ml_macros.h"

typedef struct target_t target_t;

typedef enum {BUILD_IDLE, BUILD_WAIT, BUILD_EXEC} build_thread_status_t;

typedef struct build_thread_t build_thread_t;

struct build_thread_t {
	build_thread_t *Next;
	target_t *Target;
	pthread_t Handle;
	int Id;
	build_thread_status_t Status;
	char Command[32];
};

extern const char *RootPath, *SystemName;
extern __thread const char *CurrentDirectory;
extern __thread build_thread_t *CurrentThread;

ml_value_t *rabs_global(const char *Name);

#define CURRENT_VERSION "2.1.3"
#define WORKING_VERSION "2.0.6"

#endif
