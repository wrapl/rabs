#include <extras.h>
#include "stringbuffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <regex.h>
#include <gc.h>

#define new(T) ((T *)GC_MALLOC(sizeof(T)))
#define anew(T, N) ((T *)GC_MALLOC((N) * sizeof(T)))
#define snew(N) ((char *)GC_MALLOC_ATOMIC(N))
#define xnew(T, N, U) ((T *)GC_MALLOC(sizeof(T) + (N) * sizeof(U)))

typedef struct file_t file_t;

struct file_t {
	const ml_type_t *Type;
	FILE *Handle;
};

ml_type_t FileT[1] = {{
	AnyT, "file",
	ml_default_hash,
	ml_default_call,
	ml_default_deref,
	ml_default_assign,
	ml_default_next,
	ml_default_key
}};

static ml_value_t *file_read_line(ml_t *ML, void *Data, int Count, ml_value_t **Args) {
	file_t *File = (file_t *)Args[0];
	char *Line = 0;
	size_t Length;
	if (getline(&Line, &Length, File->Handle) < 0) return feof(File->Handle) ? Nil : ml_error("FileError", "error reading from file");
	return ml_string(Line, Length);
}

static ml_value_t *file_read_count(ml_t *ML, void *Data, int Count, ml_value_t **Args) {
	file_t *File = (file_t *)Args[0];
	if (feof(File->Handle)) return Nil;
	ssize_t Requested = ml_integer_value(Args[1]);
	char *Chars = snew(Requested + 1);
	ssize_t Actual = fread(Chars, 1, Requested, File->Handle);
	if (Actual == 0) return Nil;
	if (Actual < 0) return ml_error("FileError", "error reading from file");
	Chars[Actual] = 0;
	return ml_string(Chars, Actual);
}

static ml_value_t *file_write_string(ml_t *ML, void *Data, int Count, ml_value_t **Args) {
	file_t *File = (file_t *)Args[0];
	const char *Chars = ml_string_value(Args[1]);
	ssize_t Remaining = ml_string_length(Args[1]);
	while (Remaining > 0) {
		ssize_t Actual = fwrite(Chars, 1, Remaining, File->Handle);
		if (Actual < 0) return ml_error("FileError", "error writing to file");
		Chars += Actual;
		Remaining -= Actual;
	}
	return Args[0];
}

static int file_write_buffer_chars(const char *Chars, size_t Remaining, file_t *File) {
	while (Remaining > 0) {
		ssize_t Actual = fwrite(Chars, 1, Remaining, File->Handle);
		if (Actual < 0) return 1;
		Chars += Actual;
		Remaining -= Actual;
	}
	return 0;
}

static ml_value_t *file_write_buffer(ml_t *ML, void *Data, int Count, ml_value_t **Args) {
	file_t *File = (file_t *)Args[0];
	stringbuffer_t *Buffer = (stringbuffer_t *)Args[1];
	if (stringbuffer_foreach(Buffer, File, (void *)file_write_buffer_chars)) return ml_error("FileError", "error writing to file");
	return Args[0];
}

static ml_value_t *file_eof(ml_t *ML, void *Data, int Count, ml_value_t **Args) {
	file_t *File = (file_t *)Args[0];
	if (feof(File->Handle)) return Args[0];
	return Nil;
}

static ml_value_t *file_close(ml_t *ML, void *Data, int Count, ml_value_t **Args) {
	file_t *File = (file_t *)Args[0];
	fclose(File->Handle);
	return Nil;
}

ml_value_t *file_open(ml_t *ML, void *Data, int Count, ml_value_t **Args) {
	const char *Path = ml_string_value(Args[0]);
	const char *Mode = ml_string_value(Args[1]);
	FILE *Handle = fopen(Path, Mode);
	if (!Handle) return ml_error("FileError", "failed to open %s in mode %s", Path, Mode);
	file_t *File = new(file_t);
	File->Type = FileT;
	File->Handle = Handle;
	return (ml_value_t *)File;
}

ml_value_t *string_match(ml_t *ML, void *Data, int Count, ml_value_t **Args) {
	const char *Subject = ml_string_value(Args[0]);
	const char *Pattern = ml_string_value(Args[1]);
	regex_t Regex[1];
	int Error = regcomp(Regex, Pattern, REG_EXTENDED);
	if (Error) {
		size_t ErrorSize = regerror(Error, Regex, 0, 0);
		char *ErrorMessage = snew(ErrorSize + 1);
		regerror(Error, Regex, ErrorMessage, ErrorSize);
		return ml_error("RegexError", ErrorMessage);
	}
	regmatch_t Matches[Regex->re_nsub];
	switch (regexec(Regex, Subject, Regex->re_nsub, Matches, 0)) {
	case REG_NOMATCH:
		regfree(Regex);
		return Nil;
	case REG_ESPACE: {
		regfree(Regex);
		size_t ErrorSize = regerror(REG_ESPACE, Regex, 0, 0);
		char *ErrorMessage = snew(ErrorSize + 1);
		regerror(Error, Regex, ErrorMessage, ErrorSize);
		return ml_error("RegexError", ErrorMessage);
	}
	default: {
		ml_value_t *Results = ml_list();
		for (int I = 0; I < Regex->re_nsub; ++I) {
			regoff_t Start = Matches[I].rm_so;
			if (Start >= 0) {
				size_t Length = Matches[I].rm_eo - Start;
				char *Chars = snew(Length + 1);
				memcpy(Chars, Subject + Start, Length);
				Chars[Length] = 0;
				ml_list_append(Results, ml_string(Chars, Length));
			} else {
				ml_list_append(Results, Nil);
			}
		}
		regfree(Regex);
		return Results;
	}
	}
}

void extras_init() {
	ml_method_by_name("read", 0, file_read_line, FileT, 0);
	ml_method_by_name("read", 0, file_read_count, FileT, IntegerT, 0);
	ml_method_by_name("write", 0, file_write_string, FileT, StringT, 0);
	ml_method_by_name("write", 0, file_write_buffer, FileT, StringBufferT, 0);
	ml_method_by_name("eof", 0, file_eof, FileT, 0);
	ml_method_by_name("close", 0, file_close, FileT, 0);
	ml_method_by_name("/", 0, string_match, StringT, StringT, 0);
}
