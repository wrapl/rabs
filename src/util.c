#include "util.h"
#include <gc/gc.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#define snew(N) ((char *)GC_MALLOC_ATOMIC(N + 1))

#ifdef __MINGW32__
extern char *stpcpy (char *__restrict Dest, const char *__restrict Src) {
	int Length = strlen(Src);
	strcpy(Dest, Src);
	return Dest + Length;
}
#endif

char *concat(const char *S, ...) {
	size_t L = strlen(S);
	va_list Args;
	va_start(Args, S);
	for (;;) {
		const char *T = va_arg(Args, const char *);
		if (!T) break;
		L += strlen(T);
	}
	va_end(Args);
	char *C = snew(L + 1);
	char *P = stpcpy(C, S);
	va_start(Args, S);
	for (;;) {
		const char *T = va_arg(Args, const char *);
		if (!T) break;
		P = stpcpy(P, T);
	}
	va_end(Args);
	return C;
}

const char *match_prefix(const char *Subject, const char *Prefix) {
	while (*Prefix) {
		if (*Prefix != *Subject) return 0;
		++Prefix;
		++Subject;
	}
	return Subject;
}

const char *relative_path(const char *Path, const char *Base) {
	const char *Relative = Path;
	while (*Base) {
		if (*Base != *Relative) return Path;
		++Base;
		++Relative;
	}
	if (Relative[0] == '/') return Relative + 1;
	return Path;
}

int mkdir_p(char *Path) {
	if (!Path[0]) return -1;
	struct stat Stat[1];
	for (char *P = Path + 1; P[0]; ++P) {
		if (P[0] == '/') {
			P[0] = 0;

#ifdef __MINGW32__
			if (stat(Path, Stat) < 0) {
				int Result = mkdir(Path);
#else
			if (lstat(Path, Stat) < 0) {
				int Result = mkdir(Path, 0777);
#endif
				if (Result < 0) return Result;
			}
			P[0] = '/';
		}
	}

#ifdef __MINGW32__
	if (stat(Path, Stat) < 0) {
		int Result = mkdir(Path);
#else
	if (lstat(Path, Stat) < 0) {
		int Result = mkdir(Path, 0777);
#endif
		if (Result < 0) return Result;
	}
	return 0;
}
