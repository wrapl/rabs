#ifndef UTIL_H
#define UTIL_H

char *concat(const char *S, ...) __attribute__ ((sentinel));
const char *match_prefix(const char *Subject, const char *Prefix);

#endif
