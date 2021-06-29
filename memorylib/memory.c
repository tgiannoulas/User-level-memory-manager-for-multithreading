/* Compile with g++ -Wall -shared -fPIC memory.c -o libmemory.so
 * It is important to compile all files that comprise the library
 * to the shared library in one step, rather than creating object
 * files first and then building the shared library.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>
#include <limits.h>
#include "list.h"
#include "atomic.h"

#define handle_error(msg) char* error; asprintf(&error, "File: %s, Line: %d: %s", __FILE__, __LINE__, msg); perror(error); exit(EXIT_FAILURE);

//#define MEMORYLIB_DEBUG

#define CLASSES 10
// 0 : 1-4
// 1 : 5-8
// 2 : 9-16
// 3 : 17-32
// 4 : 33-64
// 5 : 65-128
// 6 : 129-256
// 7 : 257-512
// 8 : 513-1024
// 9 : 1025-2048

#define MAX_SIZE_SMALL_OBJ 2048

#define PG_BLOCK_HEADER_SIZE 128
#define OBJ_IN_PG_BLOCK_HINT 1024
#define MIN_PG_BLOCK_SIZE 16384
#define MAX_PG_BLOCK_SIZE 262144

#define MAX_PRINT_LIFO 10

struct pg_block_header {
	struct pg_block_header *next;			// Used by the lists
	struct pg_block_header *prev;			// Used by the lists
	void *remotely_freed_LIFO;				// LIFO TODO: don't know exactly what it is
	pthread_t id;											// Thread id
	unsigned int object_size;					// The size of each oblject
	void *unallocated_ptr;						// Points to the first unallocated object
	void *freed_LIFO;									// Head of LIFO that saves freed objects
	unsigned int unallocated_objects;	// Number of unallocated object in the pg_block
	unsigned int freed_objects;				// Number of free objects in the pg_block
};
typedef struct pg_block_header pg_block_header_t;

struct class_info{
	unsigned int memory_size;
	unsigned int pg_block_size;
	unsigned int number_of_pages;
	unsigned int obj_in_pg_block;
	unsigned int wasted_obj_pg_header;
	unsigned int wasted_obj_ptr_per_pg;
	unsigned int wasted_obj_ptr_total;
};
typedef struct class_info class_info_t;

struct thread {
	pthread_t id;
	list_t heap[CLASSES];

	thread() {
		id = pthread_self();
		#ifdef MEMORYLIB_DEBUG
		printf("thread: Implicitly caught thread start, th: %ld\n", id);
		#endif
		for (int i=0; i<CLASSES; i++) {
			list_init(&heap[i]);
		}
	}

	~thread() {
		#ifdef MEMORYLIB_DEBUG
		printf("~thread: Implicitly caught thread end, th: %ld\n", id);
		#endif
	}
};
typedef struct thread thread_t;

// Global Variables
class_info_t class_info[CLASSES];		// Info for memory_classes
thread_local thread_t *th = NULL;
int pg_size;

// If u want to print ptr in binary pass the size and the pointer to ptr
extern "C" void printBits(size_t const size, void const *ptr) {
	unsigned char *b = (unsigned char*) ptr;
	unsigned char byte;
	int i, j;
	for (i = size-1; i >= 0; i--) {
		for (j = 7; j >= 0; j--) {
			byte = (b[i] >> j) & 1;
			printf("%u", byte);
		}
		putchar(' ');
	}
	puts("");
}

extern "C" void print_memory_class(unsigned int memory_class) {
	printf("---------- Memory Class %u ----------\n", memory_class);
	printf("memory_size: %u\n", class_info[memory_class].memory_size);
	printf("pg_block_size: %u\n", class_info[memory_class].pg_block_size);
	printf("number_of_pages: %u\n", class_info[memory_class].number_of_pages);
	printf("obj_in_pg_block: %u\n", class_info[memory_class].obj_in_pg_block);
	printf("wasted_obj_pg_header: %u\n", class_info[memory_class].wasted_obj_pg_header);
	printf("wasted_obj_ptr_per_pg: %u\n", class_info[memory_class].wasted_obj_ptr_per_pg);
	printf("wasted_obj_ptr_total: %u\n", class_info[memory_class].wasted_obj_ptr_total);
	printf("------------------------------------\n");
}

// Returns 4 Byte-pseudo ptr
extern "C" int ptr_to_pseudo_ptr(void *ptr) {
	return ((long int)ptr & UINT_MAX);
}

// Returns 4 Byte-pseudo ptr
extern "C" void *pseudo_ptr_to_ptr(int *pseudo_ptr) {
	if (*pseudo_ptr == 0)
		return NULL;
	return (void*) (
		((long unsigned int)pseudo_ptr & ((long unsigned int)~0 - UINT_MAX)) +
		*(unsigned int*)pseudo_ptr
	);
}

extern "C" void print_pseudo_LIFO(void *lifo) {
	int i = 0;
	while (lifo != NULL) {
		if (i < MAX_PRINT_LIFO)
			printf("%p->", lifo);
		else if (i == MAX_PRINT_LIFO + 1)
			printf(".....->");
		lifo = pseudo_ptr_to_ptr((int*)lifo);
		i++;
	}
	printf("%p\n", lifo);
}

extern "C" void print_LIFO(void *lifo) {
	int i = 0;
	while (lifo != NULL) {
		if (i < MAX_PRINT_LIFO)
			printf("%p->", lifo);
		else if (i == MAX_PRINT_LIFO + 1)
			printf(".....->");
		lifo = *(void**)lifo;
		i++;
	}
	printf("%p\n", lifo);
}

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

extern "C" void print_pg_block_header(pg_block_header_t *pg_block_header) {
	printf("pg_block_header: %p|  unallocated_objects: %4u|  freed_objects: %4u, remotely_freed_LIFO %d\n",
	pg_block_header, pg_block_header->unallocated_objects,
	pg_block_header->freed_objects,
	pg_block_header->remotely_freed_LIFO==NULL?0:1);
	printf("freed_LIFO: ");
	if (get_memory_class(pg_block_header->object_size) == 0) {
		print_pseudo_LIFO(pg_block_header->freed_LIFO);
	}
	else {
		print_LIFO(pg_block_header->freed_LIFO);
	}
	printf("remotely_freed_LIFO: ");
	if (get_memory_class(pg_block_header->object_size) == 0) {
		print_pseudo_LIFO(pg_block_header->remotely_freed_LIFO);
	}
	else {
		print_LIFO(pg_block_header->remotely_freed_LIFO);
	}
}

extern "C" void print_less_pg_block_header(pg_block_header_t *pg_block_header) {
	printf("pg_block_header: %p|  unallocated_objects: %4u|  freed_objects: %4u, remotely_freed_LIFO %d\n",
	pg_block_header, pg_block_header->unallocated_objects,
	pg_block_header->freed_objects,
	pg_block_header->remotely_freed_LIFO==NULL?0:1);
}

extern "C" void print_heap() {
	printf("--------------- Heap ---------------\n");
	for (int i = 0; i < CLASSES; i++) {
		if (th->heap[i].size == 0)
			continue;
		printf("th: %ld, class: %d, object_size: %d, objects_in_pg_block: %d, pg_blocks: %d\n",
			th->id, i, class_info[i].memory_size, class_info[i].obj_in_pg_block,
			th->heap[i].size);
		pg_block_header_t *pg_block_header = (pg_block_header_t*)
			list_get_front(&th->heap[i]);
		for (int j = 0; j < th->heap[i].size; j++) {
			printf("th: %ld, pg_block: %2d|  ",th->id, j);
			print_pg_block_header(pg_block_header);
			pg_block_header = (pg_block_header_t*)list_get_next(pg_block_header);
		}
	}
	printf("------------------------------------\n");
}

extern "C" void print_less_heap() {
	printf("--------------- Heap ---------------\n");
	for (int i = 0; i < CLASSES; i++) {
		if (th->heap[i].size == 0)
			continue;
		printf("th: %ld, class: %d, object_size: %d, objects_in_pg_block: %d, pg_blocks: %d\n",
			th->id, i, class_info[i].memory_size, class_info[i].obj_in_pg_block,
			th->heap[i].size);
		pg_block_header_t *pg_block_header = (pg_block_header_t*)
			list_get_front(&th->heap[i]);
		for (int j = 0; j < th->heap[i].size; j++) {
			printf("th: %ld, pg_block: %2d|  ",th->id, j);
			print_less_pg_block_header(pg_block_header);
			pg_block_header = (pg_block_header_t*)list_get_next(pg_block_header);
		}
	}
	printf("------------------------------------\n");
}

extern "C" int pseudo_lifo_size(void *lifo) {
	int size = 0;
	while (lifo != NULL) {
		size++;
		lifo = pseudo_ptr_to_ptr((int*)lifo);
	}
	return size;
}

extern "C" int lifo_size(void *lifo) {
	int size = 0;
	while (lifo != NULL) {
		size++;
		lifo = *(void**)lifo;
	}
	return size;
}

extern "C" void *cmp_and_swap(void** address, void* new_ptr) {
	void *old_ptr = *address;

	while (compare_and_swap_ptr(address, old_ptr, new_ptr) == 0) {
		#ifdef MEMORYLIB_DEBUG
		printf("compare_and_swap_ptr failed, retry: %p\n", new_ptr);
		#endif
		old_ptr = *address;
	}

	return old_ptr;
}

// Returns the pointer to pg_block_header
extern "C" pg_block_header_t *pg_block_to_pg_block_header(void *pg_block) {
	pg_block_header_t *pg_block_header =
		(pg_block_header_t*)((char*)pg_block + sizeof(void*));
	return pg_block_header;
}

// Returns the pointer to pg_block
extern "C" void *pg_block_header_to_pg_block
	(pg_block_header_t *pg_block_header) {
	void *pg_block = (char*)pg_block_header - sizeof(void*);
	return pg_block;
}

// Returns 1 if pg_block is full, 0 if it's not full
extern "C" int pg_block_is_full(pg_block_header_t *pg_block_header) {
	if (pg_block_header->freed_objects == 0 &&
		pg_block_header->unallocated_objects == 0 &&
		pg_block_header->remotely_freed_LIFO == NULL) {
			return 1;
	}
	return 0;
}

// Returns 1 if pg_block is full, 0 if it's not full
extern "C" int pg_block_is_empty(pg_block_header_t *pg_block_header) {
	if (pg_block_header->freed_objects + pg_block_header->unallocated_objects ==
		class_info[get_memory_class(pg_block_header->object_size)].obj_in_pg_block
		&& pg_block_header->remotely_freed_LIFO == NULL) {
			return 1;
	}
	return 0;
}

// Initializes pg_block and pg_block_header
extern "C" void pg_block_init(void *pg_block, int memory_class) {
	// The first 8 bytes of every page in a pg_block are a pointer
	// to the pg_block_header
	// The pg_block_header is located in the first page of the pg_block,
	// after the 8 bytes pointer
	pg_block_header_t *pg_block_header = pg_block_to_pg_block_header(pg_block);

	// Initialize pg_block_header fields
	pg_block_header->next = NULL;
	pg_block_header->prev = NULL;
	pg_block_header->remotely_freed_LIFO = NULL;
	pg_block_header->id = th->id;
	pg_block_header->object_size = class_info[memory_class].memory_size;
	pg_block_header->unallocated_ptr = (char*)pg_block + class_info[memory_class].
		memory_size * class_info[memory_class].wasted_obj_pg_header;
	pg_block_header->freed_LIFO = NULL;
	pg_block_header->unallocated_objects = class_info[memory_class].
		obj_in_pg_block;
	pg_block_header->freed_objects = 0;

	// Write the ptr to the pg_block_header at the start of every pg
	// TODO: Optimization, pointer is 16KB alligned
	for (unsigned int i = 0; i < class_info[memory_class].number_of_pages; i++) {
		pg_block_header_t **ptr = (pg_block_header_t**) ((char*)pg_block + i * pg_size);
		*ptr = pg_block_header;
	}
}

// Allocates memory for memory_class pg_block
extern "C" void *pg_block_alloc(int memory_class) {

	void *pg_block = mmap(NULL, class_info[memory_class].pg_block_size,
		PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (pg_block == MAP_FAILED) { handle_error("mmap failed"); }

	pg_block_init(pg_block, memory_class);

	return pg_block;
}

// Frees memory for pg_block
extern "C" void pg_block_free(pg_block_header_t* pg_block_header) {
	void* pg_block = pg_block_header_to_pg_block(pg_block_header);
	int memory_class = get_memory_class(pg_block_header->object_size);
	if (munmap(pg_block, class_info[memory_class].pg_block_size) == -1)
		{ handle_error("munmap failed"); }
}

// Returns a pg_block that is not full
extern "C" pg_block_header *get_pg_block(int memory_class) {
	// Check if there is no pg_block at all
	if (list_is_empty(&th->heap[memory_class])) {
		list_insert_front(&th->heap[memory_class], pg_block_to_pg_block_header(
			pg_block_alloc(memory_class)));
	}

	// Get the first pg_block
	pg_block_header_t *pg_block_header = (pg_block_header_t*)list_get_front(
		&th->heap[memory_class]);
	// Check if its full - (just in case that i allocate an orphaned pg_block
	// that is already being used and is full)
	while (pg_block_is_full(pg_block_header)) {
		list_insert_front(&th->heap[memory_class], pg_block_to_pg_block_header(
			pg_block_alloc(memory_class)));
		pg_block_header = (pg_block_header_t*)list_get_front(
			&th->heap[memory_class]);
	}

	return pg_block_header;
}

// Given a pointer the function returns the address of the address page
extern "C" void *get_address_pg(void *ptr) {
	long int pg_mask = ~(pg_size-1);
	return ((void*)((long int)ptr & pg_mask));
}

// Given a pointer in a pg_block the function returns the pg_block_header
// of this pg_block
extern "C" pg_block_header_t *get_pg_block_header(void *ptr) {
	return (*(pg_block_header_t **)get_address_pg(ptr));
}

// Given a pg_block_header the function allocates an object and returns it
// If it fails, e.g. beacause the pg_block is full, it returns NULL
extern "C" void *obj_alloc(pg_block_header_t *pg_block_header) {
	void *obj;
	int memory_class = get_memory_class(pg_block_header->object_size);
	// Allocate an object
	if (pg_block_header->freed_objects > 0) {
		// Get object from the freed_LIFO
		obj = pg_block_header->freed_LIFO;
		if (memory_class == 0) {
			// Special case if obj_size is 4 bytes
			pg_block_header->freed_LIFO = pseudo_ptr_to_ptr((int*)obj);
		}
		else {
			pg_block_header->freed_LIFO = *(void**)obj;
		}
		pg_block_header->freed_objects--;
	}
	else if (pg_block_header->unallocated_objects > 0) {
		// Get object from the unallocated objects
		obj = pg_block_header->unallocated_ptr;
		pg_block_header->unallocated_ptr = ((char*)pg_block_header->unallocated_ptr
			+ pg_block_header->object_size);

		// Check if unallocated_ptr is the address of a pointer to pg_block_header
		void *pg = get_address_pg(pg_block_header->unallocated_ptr);
		if (pg == pg_block_header->unallocated_ptr) {
			// Skip wasted_objects_ptr_per_pg objects
			pg_block_header->unallocated_ptr =
				((char*)pg_block_header->unallocated_ptr + (pg_block_header->object_size
				* class_info[memory_class].wasted_obj_ptr_per_pg));
		}
		pg_block_header->unallocated_objects--;
	}
	else if (pg_block_header->remotely_freed_LIFO != NULL) {
		// Get the lifo from the remotely_freed_LIFO
		void *lifo = cmp_and_swap(&pg_block_header->remotely_freed_LIFO, NULL);

		// Get object from the lifo
		obj = lifo;
		// Save lifo to freed_LIFO
		if (memory_class == 0) {
			// Special case if obj_size is 4 bytes
			pg_block_header->freed_LIFO = pseudo_ptr_to_ptr((int*)obj);
			// Count the free_objects
			pg_block_header->freed_objects = pseudo_lifo_size(pg_block_header->freed_LIFO);
		}
		else {
			pg_block_header->freed_LIFO = *(void**)obj;
			// Count the free_objects
			pg_block_header->freed_objects = lifo_size(pg_block_header->freed_LIFO);
		}

	}
	else {
		// There is no object to allocate, Don't know if this ever happens
	}

	// If I just took the last object, move pg_block at the end of the list
	if (pg_block_is_full(pg_block_header) &&
		list_get_back(&th->heap[memory_class]) != pg_block_header) {
		list_remove(&th->heap[memory_class], pg_block_header);
		list_insert_back(&th->heap[memory_class],	pg_block_header);
		}
	return obj;
}

extern "C" void *my_malloc(size_t size) {
	// We define a thread_local variable, that will be per-thread.
	// We also make it static, in order to persist for the lifetime of the thread.
	// When the variable comes to life, the constructor is executed (thread).
	// When the variable comes out of scope, at the end of the life of the thread,
	// given that it is static, the destructor is executed (~thread).
	thread_local static thread_t my_th;
	th = &my_th;

	// Check input
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

	// Get a pg_block
	pg_block_header_t *pg_block_header = get_pg_block(memory_class);
	// Get an object
	void *obj = obj_alloc(pg_block_header);

	#ifdef MEMORYLIB_DEBUG
		printf("EVENT, my_malloc: alocated %p\n", obj);
		//print_less_heap();
		print_heap();
	#endif
	return obj;
}

extern "C" void my_free(void *ptr) {
	pg_block_header_t *pg_block_header = get_pg_block_header(ptr);
	int memory_class = get_memory_class(pg_block_header->object_size);

	// Rearange remotely_freed_LIFO
	if (th == NULL || pg_block_header->id != th->id) {
		// Check if th is NULL which means the thread_t hasn't been initiated
		// because my_malloc hasn't been called by this thread
		void *old_ptr = cmp_and_swap(&pg_block_header->remotely_freed_LIFO, ptr);

		if (memory_class == 0) {
			// Special case if obj_size is 4 bytes, save pseudo_ptr
			*(int*)ptr = ptr_to_pseudo_ptr(old_ptr);
		}
		else {
			*(void**)ptr = old_ptr;
		}

		#ifdef MEMORYLIB_DEBUG
			printf("EVENT, my_free: remote free %p\n", ptr);
		#endif

		return;
	}

	// Rearange freed_LIFO
	if (memory_class == 0) {
		// Special case if obj_size is 4 bytes, save pseudo_ptr
		*(int*)ptr = ptr_to_pseudo_ptr(pg_block_header->freed_LIFO);
	}
	else {
		*(void**)ptr = pg_block_header->freed_LIFO;
	}
	pg_block_header->freed_LIFO = ptr;
	pg_block_header->freed_objects++;

	#ifdef MEMORYLIB_DEBUG
		printf("EVENT, my_free: free %p\n", ptr);
		//print_less_heap();
		print_heap();
	#endif

	if (pg_block_is_empty(pg_block_header)) {
		// If the pg_block is empty, free it
		list_remove(&th->heap[memory_class], pg_block_header);
		pg_block_free(pg_block_header);
	}
	else if (list_get_front(&th->heap[memory_class]) != pg_block_header) {
		// Move the pg_block to the beginning of the list
		list_remove(&th->heap[memory_class], pg_block_header);
		list_insert_front(&th->heap[memory_class], pg_block_header);
	}
}

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
		class_info[i].number_of_pages = class_info[i].pg_block_size / pg_size;
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
			(class_info[i].number_of_pages - 1);

		// Measure how many objects in total are gonna be available in the pg_block
		class_info[i].obj_in_pg_block -= class_info[i].wasted_obj_pg_header +
			class_info[i].wasted_obj_ptr_total;


		#ifdef MEMORYLIB_DEBUG
		print_memory_class(i);
		#endif
	}
	#ifdef MEMORYLIB_DEBUG
	printf("\n\n\n\n\n");
	#endif
}

__attribute__((destructor)) static void terminator(void) {
	#ifdef MEMORYLIB_DEBUG
	printf("!!! Library unloading !!!\n");
	#endif
}
