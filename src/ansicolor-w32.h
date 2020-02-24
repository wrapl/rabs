#ifndef ANSICOLOR_W32_H
#define ANSICOLOR_W32_H

#include <stdio.h>

int _fprintf_w32(FILE* fp, const char* format, ...);
int _fputs_w32(FILE* fp, const char* s);

#define fprintf(...) _fprintf_w32(__VA_ARGS__)
#define printf(...) _fprintf_w32(stdout, __VA_ARGS__)
#define fputs(fp, x) _fprintf_w32(fp, x);
#define puts(x) _fprintf_w32(stdout, x);

#endif
