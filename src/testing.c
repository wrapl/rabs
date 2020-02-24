#include <stdio.h>

extern __thread const char *Test;

int main(int Argc, char **Argv) {
	Test = "blah";
	puts("Hello world!");
	puts(Test);
	return 0;
}
