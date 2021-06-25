/* Compile with gcc -Wall -g my_prog.c -lmy_lib -L. -o my_prog -lpthread
 * Make sure you have appended the location of the .so to the environment
 * variable LD_LIBRARY_PATH */

#include <stdio.h>
#include "memorylib/memory.h"

#define ARRAY_SIZE 8
#define MALLOC_SIZE 4

int main (int argc, char *argv[]) {

	long int *ptr[ARRAY_SIZE];

	for (int i = 0; i < ARRAY_SIZE; i++) {
		ptr[i] = my_malloc(MALLOC_SIZE);
	}

	print_heap();

	for (int i = 0; i < ARRAY_SIZE; i++) {
		my_free(ptr[i]);
	}

	print_heap();

	return 0;
}
