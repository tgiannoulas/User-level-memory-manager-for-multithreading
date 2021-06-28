/* Compile with gcc -Wall -g my_prog.c -lmy_lib -L. -o my_prog -lpthread
 * Make sure you have appended the location of the .so to the environment
 * variable LD_LIBRARY_PATH
 * export LD_LIBRARY_PATH=path*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include "memorylib/memory.h"

#define ARRAY_SIZE 1000
#define MALLOC_SIZE 1024
#define PTHREADS_NUM 1

void *foo() {
	long int *ptr[ARRAY_SIZE];

	for (int i = 0; i < ARRAY_SIZE; i++) {
		ptr[i] = my_malloc(MALLOC_SIZE);
	}

	print_less_heap();

	for (int i = 0; i < ARRAY_SIZE; i++) {
		my_free(ptr[i]);
	}

	print_less_heap();
	return NULL;
}

int main (int argc, char *argv[]) {
	pthread_t pthreads[PTHREADS_NUM];

	for (int i = 0; i < PTHREADS_NUM; i++) {
		if (pthread_create(&pthreads[i], NULL, foo, NULL) != 0) {
			perror("pthread_create\n");
			exit(1);
		}
	}

	for (int i = 0; i < PTHREADS_NUM; i++) {
		pthread_join(pthreads[i], NULL);
	}

	return 0;
}
