#include "minilang.h"
#include "stringmap.h"
#include <stdio.h>
#include <gc.h>

static stringmap_t Globals[1] = {STRINGMAP_INIT};

static ml_value_t *global_get(ml_t *ML, void *Data, const char *Name) {
	return stringmap_search(Globals, Name) ?: Nil;
}

static ml_value_t *global_print(ml_t *ML, void *Data, int Count, ml_value_t **Args) {
	ml_value_t *StringMethod = ml_method("string");
	for (int I = 0; I < Count; ++I) {
		ml_value_t *Result = Args[I];
		if (Result->Type != StringT) {
			Result = ml_call(ML, StringMethod, 1, &Result);
			if (Result->Type == ErrorT) return Result;
			if (Result->Type != StringT) return ml_error("ResultError", "string method did not return string");
		}
		fputs(ml_string_value(Result), stdout);
	}
	fflush(stdout);
	return Nil;
}

int main(int Argc, const char *Argv[]) {
	stringmap_insert(Globals, "print", ml_function(0, global_print));
	ml_t *ML = ml_new(0, global_get);
	ml_value_t *Closure = ml_load(ML, Argv[1]);
	if (Closure->Type == ErrorT) {
		printf("Error: %s\n", ml_error_message(Closure));
		const char *Source;
		int Line;
		for (int I = 0; ml_error_trace(Closure, I, &Source, &Line); ++I) printf("\t%s:%d\n", Source, Line);
		return 1;
	}
	ml_value_t *Result = ml_call(ML, Closure, 0, 0);
	if (Result->Type == ErrorT) {
		printf("Error: %s\n", ml_error_message(Result));
		const char *Source;
		int Line;
		for (int I = 0; ml_error_trace(Result, I, &Source, &Line); ++I) printf("\t%s:%d\n", Source, Line);
		return 1;
	}
	return 0;
}
