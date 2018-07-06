#include "util.h"
#include <gc/gc.h>
#include <string.h>
#include <stdarg.h>

#define snew(N) ((char *)GC_MALLOC_ATOMIC(N + 1))

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
