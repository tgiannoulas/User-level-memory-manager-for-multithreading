/* Compile with gcc -Wall -g my_prog.c -lmy_lib -L. -o my_prog -lpthread
 * Make sure you have appended the location of the .so to the environment
 * variable LD_LIBRARY_PATH */

#include <stdio.h>
#include "memorylib/memory.h"
#include <unistd.h>


int main (int argc, char *argv[]) {

	void *ptr1 = my_malloc(4);
	void *ptr2 = my_malloc(4);
	my_free(ptr1);
	my_free(ptr2);

	return 0;
}
