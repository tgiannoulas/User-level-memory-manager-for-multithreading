/* Compile with g++ -Wall -shared -fPIC memory.c -o libmemory.so
 * It is important to compile all files that comprise the library
 * to the shared library in one step, rather than creating object
 * files first and then building the shared library.
 */

#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <unistd.h>
#include <sys/mman.h>

#define handle_error(msg) char* error; asprintf(&error, "File: %s, Line: %d: %s", __FILE__, __LINE__, msg); perror(error); exit(EXIT_FAILURE);

//#define MEMORYLIB_DEBUG
#define CLASSES 10

#define MAX_SIZE_SMALL_OBJ 2048

#define PG_BLOCK_HEADER_SIZE 128
#define OBJ_IN_PG_BLOCK_HINT 1024
#define MIN_PG_BLOCK_SIZE 16384
#define MAX_PG_BLOCK_SIZE 262144

struct pg_block_header {
	struct pg_block_header *next;			// Used by the lists
	struct pg_block_header *prev;			// Used by the lists
	void *remotely_freed_LIFO;				// LIFO TODO: don't know exactly what it is
	int id;														// TODO: probably not an integer
	unsigned int object_size;					// The size of each oblject
	void *unallocated_ptr;						// Points to the first unallocated object
	void *freed_LIFO;									// Head of LIFO that saves freed objects
	unsigned int unallocated_objects;	// Number of unallocated object in the pg_block
	unsigned int freed_objects;				// Number of free objects in the pg_block
};
typedef struct pg_block_header pg_block_header_t;

struct class_info{
	int memory_size;
	int pg_block_size;
	int obj_in_pg_block;
	int wasted_obj_pg_header;
	int wasted_obj_ptr_per_pg;
	int wasted_obj_ptr_total;
};
typedef struct class_info class_info_t;

// Global Variables
class_info_t class_info[CLASSES];		// At element i there are info for memory_class i
int pg_size;

struct thread {
	int id;
	void *heap[CLASSES];

	thread() {
		#ifdef MEMORYLIB_DEBUG
		printf("thread: Implicitly caught thread start\n");
		#endif
		for (int i=0; i<CLASSES; i++) {
			heap[i] = NULL;
		}
	}

	~thread() {
		#ifdef MEMORYLIB_DEBUG
		printf("~thread: Implicitly caught thread end\n");
		#endif
	}
};

typedef struct thread thread_t;

// Given the size return the memory_class that it belongs to
extern "C" int get_memory_class(size_t size) {
	int memory_class = 0;
	size--;
	while(size > 3) {
		size = size>>1;
		memory_class++;
	}
	return memory_class;
}

extern "C" void pg_block_init(void *pg_block, int memory_class) {
	// The first 8 bytes are the pointer to the pg_block_header
	// At the start of every pg there is a pointer to the pg_block_header
	pg_block_header_t *pg_ptr = (pg_block_header_t*) pg_block;
	// After the pointer is the pg_block_header
	pg_block_header_t *pg_block_header = (pg_block_header_t*) ((char*)pg_ptr +
		sizeof(pg_ptr));

	// Initialize pg_block_header fields
	pg_block_header->remotely_freed_LIFO = NULL;
	pg_block_header->id = 0; // TODO: find out what will be the id
	pg_block_header->object_size = class_info[memory_class].memory_size;
	pg_block_header->unallocated_ptr = (char*)pg_block + class_info[memory_class].
		memory_size * class_info[memory_class].wasted_obj_pg_header;
	pg_block_header->freed_LIFO = NULL;
	pg_block_header->unallocated_objects = class_info[memory_class].
		obj_in_pg_block;
	pg_block_header->freed_objects = 0;

	// Write the ptr to the pg_block_header at the start of every pg
	// TODO: Optimization, pointer is 16KB alligned
	for (int i = 0; i < class_info[memory_class].pg_block_size / pg_size; i++) {
		pg_block_header_t **ptr = (pg_block_header_t**) ((char*)pg_block + i * pg_size);
		*ptr = pg_block_header;
	}
}

// Find the correct size for
extern "C" void *pg_block_alloc(int memory_class) {

	void *pg_block = mmap(NULL, class_info[memory_class].pg_block_size,
		PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (pg_block == MAP_FAILED) { handle_error("mmap failed"); }

	pg_block_init(pg_block, memory_class);

	return pg_block;
}

extern "C" void *my_malloc(size_t size) {
	// We define a thread_local variable, namely a variable that will be per-thread.
	// We also make it static, in order to persist for the lifetime of the thread.
	// When the variable comes to life, the constructor is executed (thread).
	// When the variable comes out of scope - namely at the end of the life of the thread,
	// given that it is static, the destructor is executed (~thread).
	thread_local static thread_t th;

	if (size <= 0) {
		printf("my_malloc: Wrong size\n");
		return NULL;
	}
	else if (size > MAX_SIZE_SMALL_OBJ) {
		// TODO: big object
		printf("my_malloc: Big object, not supported yet\n");
		return NULL;
	}

	int memory_class = get_memory_class(size);
	// if there is a pg_block check it
	if (th.heap[memory_class] == NULL) {
		// if there is no pg_block ask pg manager for a pg_block
		th.heap[memory_class] = pg_block_alloc(memory_class);
		printf("pg_block: %p\n", th.heap[memory_class]);
	}
	else {
		// if there is free memory in the pg_block good

	}


	return NULL;
}

extern "C" void my_free(void *ptr) {}

// With the following we can define functions to be called when we enter the
// library for the first time and when we exit the library.
__attribute__((constructor)) static void initializer(void) {
	#ifdef MEMORYLIB_DEBUG
	printf("!!! Library loaded !!!\n");
	#endif
	pg_size = getpagesize();

	/*---------- Initialize class_info ----------*/
	int memory_size = 2;
	for (int i = 0; i < CLASSES; i++) {

		/*---------- Initialize memory_size ----------*/
		class_info[i].memory_size = memory_size = memory_size<<1;

		/*---------- Initialize pg_block_size and obj_in_pg_block ----------*/
		// Initial estimation
		class_info[i].pg_block_size = OBJ_IN_PG_BLOCK_HINT *
			class_info[i].memory_size;

		// Normalize pg_block_size between MIN_PG_BLOCK_SIZE and MAX_PG_BLOCK_SIZE
		if (class_info[i].pg_block_size < MIN_PG_BLOCK_SIZE) {
			class_info[i].pg_block_size = MIN_PG_BLOCK_SIZE;
		}
		else if (class_info[i].pg_block_size > MAX_PG_BLOCK_SIZE) {
			class_info[i].pg_block_size = MAX_PG_BLOCK_SIZE;
		}
		class_info[i].obj_in_pg_block = class_info[i].pg_block_size /
			class_info[i].memory_size;

		// Measure the waste for the pg_block_header
		class_info[i].wasted_obj_pg_header = PG_BLOCK_HEADER_SIZE /
			class_info[i].memory_size;
		if (class_info[i].wasted_obj_pg_header == 0) {
			class_info[i].wasted_obj_pg_header = 1;
		}

		// Measure the waste/pg for the pointer to the pg_block_header
		// TODO: Optimization, pointer is 16KB alligned
		class_info[i].wasted_obj_ptr_per_pg = sizeof(pg_block_header_t *) /
			class_info[i].memory_size;
		if (class_info[i].wasted_obj_ptr_per_pg == 0) {
			class_info[i].wasted_obj_ptr_per_pg = 1;
		}
		// Total waste for all the pgs
		class_info[i].wasted_obj_ptr_total = class_info[i].wasted_obj_ptr_per_pg *
			class_info[i].pg_block_size / pg_size - 1;

		// Measure how many objects in total are gonna be available in the pg_block
		class_info[i].obj_in_pg_block -= class_info[i].wasted_obj_pg_header +
			class_info[i].wasted_obj_ptr_total;

		#ifdef MEMORYLIB_DEBUG
		printf("class: %d\n", i);
		printf("\tmemory_size: %d\n", class_info[i].memory_size);
		printf("\tpg_block_size: %d\n", class_info[i].pg_block_size);
		printf("\tobj_in_pg_block: %d\n", class_info[i].obj_in_pg_block);
		printf("\twasted_obj_pg_header: %d\n", class_info[i].wasted_obj_pg_header);
		printf("\twasted_obj_ptr_per_pg: %d\n", class_info[i].wasted_obj_ptr_per_pg);
		printf("\twasted_obj_ptr_total: %d\n", class_info[i].wasted_obj_ptr_total);
		#endif
	}
}

__attribute__((destructor)) static void terminator(void) {
	#ifdef MEMORYLIB_DEBUG
	printf("!!! Library unloading !!!\n");
	#endif
}
