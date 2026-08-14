#include <stdio.h>

int lib_bss = 0;
int lib_data = 75;
extern const int lib_rodata = 7497312;

extern "C" void lib_func(void) { printf("Hello! My name is lib_func.\n"); }
