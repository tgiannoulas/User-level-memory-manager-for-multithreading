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

#define ARRAY_SIZE 100
#define MALLOC_SIZE 8
#define PTHREADS_NUM 21

void *ptr[(PTHREADS_NUM-1) * ARRAY_SIZE];
int th0_ready = 0;

/**
 * This functions tests if the cmp&swap works when a thread remotely frees
 * The thread with id 0 allocated all the memory and the other threads wait and
 * then each thread frees a portion of this memory
 * In order to see the results
 		* define the MEMORYLIB_DEBUG at memory.c, or comment out the ifdef at the
 		* printf that prints the message, @ my_free() at the remotely_free part
 		* set ARRAY_SIZE 100
 		* set MALLOC_SIZE 8
 		* set PTHREADS_NUM 21
 		* Set global variables
 			* void *ptr[(PTHREADS_NUM-1) * ARRAY_SIZE];
 			* int th0_ready = 0;
 * @param  id [id range from 0 to PTHREADS_NUM - 1]
 */
void th_test_cmp_swap_rem_free(int *id) {
	if (*id == 0) {
		// thread 1 mallocs all the memory
		for (int i = 0; i < (PTHREADS_NUM-1) * ARRAY_SIZE; i++) {
			ptr[i] = my_malloc(MALLOC_SIZE);
		}
		th0_ready = 1;
		sleep(1);
		print_less_heap();
	}
	else {
		// the other threads frees a portion of the memory allocated by thread 1
		while (th0_ready != 1) {}
		for (int i = (*id-1) * ARRAY_SIZE; i < *id * ARRAY_SIZE; i++) {
			my_free(ptr[i]);
		}
	}
}

void test_cmp_swap_rem_free() {
	pthread_t pthreads[PTHREADS_NUM];
	int id[PTHREADS_NUM];

	for (int i = 0; i < PTHREADS_NUM; i++) {
		id[i] = i;
		if (pthread_create(&pthreads[i], NULL, (void*)th_test_cmp_swap_rem_free,	&id[i]) != 0) {
			perror("pthread_create\n");
			exit(1);
		}
	}

	for (int i = 0; i < PTHREADS_NUM; i++) {
		pthread_join(pthreads[i], NULL);
	}
}

void th_test_malloc() {
	long int *ptr[ARRAY_SIZE];

	for (int i = 0; i < ARRAY_SIZE; i++) {
		ptr[i] = my_malloc(MALLOC_SIZE);
	}

	print_less_heap();

	for (int i = 0; i < ARRAY_SIZE; i++) {
		my_free(ptr[i]);
	}

	print_less_heap();
}

void test_malloc() {
	pthread_t pthreads[PTHREADS_NUM];

	for (int i = 0; i < PTHREADS_NUM; i++) {
		if (pthread_create(&pthreads[i], NULL, (void*)th_test_malloc, NULL) != 0) {
			perror("pthread_create\n");
			exit(1);
		}
	}

	for (int i = 0; i < PTHREADS_NUM; i++) {
		pthread_join(pthreads[i], NULL);
	}
}

int main (int argc, char *argv[]) {

	test_cmp_swap_rem_free();

	return 0;
}
