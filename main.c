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

#define ARRAY_SIZE 65
#define MALLOC_SIZE 2048
#define PTHREADS_NUM 1

void *ptr[ARRAY_SIZE];

void *my_array_test_termination[64];
void *my_array_test_large_obj[100000];

void *my_array_test_cmp_swap_rem_free[2029];
int th0_ready = 0;

/**
 * This functions tests if the cmp&swap works when a thread remotely frees
 * and when the thread that allocated transfers the remotely_freed_LIFO to the
 * freed_LIFO
 * The thread with id 0 allocated all the memory and the other threads wait and
 * then each thread frees a portion of this memory
 * Then the thread 0 mallocs one object and bacause there are no available
 * objects in unallocated and freed_LIFO it brings the objects from
 * remotely_freed_LIFO
 * In order to see the results
 		* define the MEMORYLIB_DEBUG at memory.c, or comment out the ifdef at the
 		* printf that prints the message, @ my_free() at the remotely_free part
 * @param  id [range from 0 to pthread_num - 1]
 */
void th_test_cmp_swap_rem_free(int *id) {
	int array_size = 100;
	int malloc_size = 8;

	if (*id == 0) {
		// thread 1 mallocs all the memory
		for (int i = 0; i < 2029; i++) {
			my_array_test_cmp_swap_rem_free[i] = my_malloc(malloc_size);
		}
		th0_ready = 1;
		sleep(1);
		print_heap();

		void *foo = my_malloc(malloc_size);
		print_heap();

		my_free(foo);
		print_heap();

	}
	else {
		// the other threads frees a portion of the memory allocated by thread 1
		while (th0_ready != 1) {}
		for (int i = (*id-1) * array_size; i < *id * array_size; i++) {
			my_free(my_array_test_cmp_swap_rem_free[i]);
		}
		if (*id == 1) {
			for (int i = 2000; i < 2029; i++) {
				my_free(my_array_test_cmp_swap_rem_free[i]);
			}
		}
	}
}

void test_cmp_swap_rem_free() {
	int pthread_num = 21;

	pthread_t pthreads[pthread_num];
	int id[pthread_num];

	for (int i = 0; i < pthread_num; i++) {
		id[i] = i;
		if (pthread_create(&pthreads[i], NULL, (void*)th_test_cmp_swap_rem_free,	&id[i]) != 0) {
			perror("pthread_create\n");
			exit(1);
		}
	}

	for (int i = 0; i < pthread_num; i++) {
		pthread_join(pthreads[i], NULL);
	}
}

/**
 * This function tests the local and the global cache
 * Firstly 129 objects get allocated
 * this results to 2 full pg_blocks and a pg_block with just one object
 * To test the local cache first
 * The one object gets freed and another malloc is called
 * We see the effect in the local_cache output
 * To test the global cache first
 * All the objects gets freed
 * This results to 1 pg_block cached in local_cache and 1 global cache
 * The 3rd pg_block returns to the OS
 * Then we allocate again 129 objects
 * We see the effects in the local_cache and global_cache output
 */
void test_cache() {
	int array_size = 129;
	int malloc_size = 2048;

	void *my_array[array_size];

	for (int i = 0; i < array_size; i++) {
		my_array[i] = my_malloc(malloc_size);
	}

	printf("Malloc 129 obj, 2 full pg_blocks and 1 with just 1 obj\n");
	print_less_heap();
	print_local_cache();
	print_global_cache();

	my_free(my_array[array_size-1]);

	printf("Free the obj from the pg_block with the 1 obj\n");
	print_less_heap();
	print_local_cache();
	print_global_cache();

	my_array[array_size-1] = my_malloc(malloc_size);

	printf("Malloc one obj\n");
	print_less_heap();
	print_local_cache();
	print_global_cache();

	for (int i = 0; i < array_size; i++) {
		my_free(my_array[i]);
	}

	printf("Free 129 objs\n");
	print_less_heap();
	print_local_cache();
	print_global_cache();

	for (int i = 0; i < array_size; i++) {
		my_array[i] = my_malloc(malloc_size);
	}

	printf("Malloc 129 objs\n");
	print_less_heap();
	print_local_cache();
	print_global_cache();

	for (int i = 0; i < array_size; i++) {
		my_free(my_array[i]);
	}

}

/**
 * This function tests what happens when a thread exits
 * In this example 21 threads are created
 * Thread 0 allocates 2000 8B objects, enough to fit in one pg_block, max=2029
 * Then the other 20 threads free 100 objects reached
 * One of them, "the fastest" has to adopt the pg_block
 * @param id [range from 0 to pthread_num - 1]
 */
void th_test_termination(int *id) {
	int array_size = 100;
	int malloc_size = 8;

	if (*id == 0) {
		for (int i = 0; i < 2000; i++) {
			my_array_test_termination[i] = my_malloc(malloc_size);
		}
		printf("th: %d, Malloc 10 obj\n", *id);
		print_less_heap();
		th0_ready = 1;

	}
	else {
		while (th0_ready == 0) {}

		for (int i = (*id-1) * array_size; i < *id * array_size; i++) {
			my_free(my_array_test_termination[i]);
		}

		printf("th: %d, Free 100 obj\n", *id);
		print_heap();
		sleep(1);

	}
	if (*id == 1) {
		sleep(1);
		print_global_cache();
	}

}

void test_termination() {
	int pthread_num = 21;

	pthread_t pthreads[pthread_num];
	int id[pthread_num];

	for (int i = 0; i < pthread_num; i++) {
		id[i] = i;
		if (pthread_create(&pthreads[i], NULL, (void*)th_test_termination,	&id[i]) != 0) {
			perror("pthread_create\n");
			exit(1);
		}
	}

	for (int i = 0; i < pthread_num; i++) {
		pthread_join(pthreads[i], NULL);
	}
}

/**
 * This simple example just mallocs, reallocs and frees one object
 */
void test_realloc() {
	void *ptr = my_malloc(1024);
	print_less_heap();
	ptr = my_realloc(ptr, 2048);
	print_less_heap();
	my_free(ptr);
	print_less_heap();
}

/**
 * In this function thread 0 mallocs large objs and then 10 other threads
 * free these the same amount of objects
 * @param id [range from 0 to pthread_num - 1]
 */
void th_test_large_obj(int *id) {
	int array_size = 10000;
	int malloc_size = 100000;

	if (*id == 0) {
		for (int i = 0; i < 100000; i++) {
			my_array_test_large_obj[i] = my_malloc(malloc_size);
		}
		print_large_obj_table();
		th0_ready = 1;
		sleep(2);
		print_large_obj_table();
	}
	else {
		while (th0_ready == 0) {}

		for (int i = (*id-1) * array_size; i < *id * array_size; i++) {
			my_free(my_array_test_large_obj[i]);
		}
	}
}

void test_large_obj() {
	int pthread_num = 11;

	pthread_t pthreads[pthread_num];
	int id[pthread_num];

	for (int i = 0; i < pthread_num; i++) {
		id[i] = i;
		if (pthread_create(&pthreads[i], NULL, (void*)th_test_large_obj,	&id[i]) != 0) {
			perror("pthread_create\n");
			exit(1);
		}
	}

	for (int i = 0; i < pthread_num; i++) {
		pthread_join(pthreads[i], NULL);
	}
}

int main (int argc, char *argv[]) {

	if (argc != 2) {
		printf("False arguements\n");
		return -1;
	}

	int test = atoi(argv[1]);

	if (ex == 1) {
		test_cache();
	}
	else if (ex == 2) {
		test_cmp_swap_rem_free();
	}
	else if (ex == 3) {
		test_realloc();
	}
	else if (ex == 4) {
		test_termination();
	}
	else if (ex == 5) {
		test_large_obj();
	}

	return 0;
}
