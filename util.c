#include "util.h"
#include <gc/gc.h>
#include <string.h>
#include <stdarg.h>

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
	char *C = GC_malloc_atomic(L + 1);
	char *P = stpcpy(C, S);
	va_start(Args, S);
	for (;;) {
		const char *T = va_arg(Args, const char *);
		if (!T) break;
		P = stpcpy(P, T);
	}
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