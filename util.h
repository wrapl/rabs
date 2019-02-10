#ifndef UTIL_H
#define UTIL_H

#ifdef __MINGW32__
char *stpcpy (char *__restrict Dest, const char *__restrict Src);
#endif

char *concat(const char *S, ...) __attribute__ ((sentinel));
const char *match_prefix(const char *Subject, const char *Prefix);
int mkdir_p(char *Path);

#endif
