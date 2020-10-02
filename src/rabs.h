#ifndef RABS_H
#define RABS_H

#include <pthread.h>
#include <unistd.h>
#include "minilang.h"
#include "ml_macros.h"

typedef struct target_t target_t;

typedef enum {BUILD_IDLE, BUILD_WAIT, BUILD_EXEC} build_thread_status_t;

typedef struct build_thread_t build_thread_t;

struct build_thread_t {
	build_thread_t *Next;
	target_t *Target;
	pthread_t Handle;
	int Id;
	pid_t Child;
	build_thread_status_t Status;
	char Command[32];
};

extern const char *RootPath, *SystemName;
extern __thread const char *CurrentDirectory;
extern __thread build_thread_t *CurrentThread;
extern __thread target_t *CurrentTarget;

ml_value_t *rabs_global(const char *Name);

#define CURRENT_VERSION 2, 11, 7
#define WORKING_VERSION 2, 10, 0

#endif
