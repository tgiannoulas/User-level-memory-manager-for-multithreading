/* Compile with g++ -Wall -shared -fPIC memory.c -o libmemory.so
 * It is important to compile all files that comprise the library
 * to the shared library in one step, rather than creating object
 * files first and then building the shared library.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>
#include <limits.h>
#include "list.h"
#include "atomic.h"

#define handle_error(msg) char* error; asprintf(&error, "File: %s, Line: %d: %s", __FILE__, __LINE__, msg); perror(error); exit(EXIT_FAILURE);

#define MEMORYLIB_DEBUG

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
// Enough for 16GB concurrent memory allocation
#define LARGE_OBJ_TABLE_SIZE 33554432

#define MAX_PRINT_LIFO 10

// Global Variables
int cache_classes;
int pg_size;

struct pg_block_header {
	struct pg_block_header *next;				// Used by the lists
	struct pg_block_header *prev;				// Used by the lists
	volatile void *remotely_freed_LIFO;	// Head of LIFO that saves the remotel_freed_objects
	pthread_t id;												// Thread id
	unsigned int object_size;						// The size of each oblject
	void *unallocated_ptr;							// Points to the first unallocated object
	void *freed_LIFO;										// Head of LIFO that saves freed objects
	unsigned int unallocated_objects;		// Number of unallocated object in the pg_block
	unsigned int freed_objects;					// Number of free objects in the pg_block
};
typedef struct pg_block_header pg_block_header_t;
pg_block_header_t *global_cache[CLASSES];				// Global cache managed by the pg_manager

struct class_info{
	unsigned int memory_size;
	unsigned int pg_block_size;
	unsigned int number_of_pages;
	unsigned int obj_in_pg_block;
	unsigned int wasted_obj_pg_header;
	unsigned int wasted_obj_ptr_per_pg;
	unsigned int wasted_obj_ptr_total;
	unsigned int cache_class;
};
typedef struct class_info class_info_t;
class_info_t class_info[CLASSES];		// Info for memory_classes

struct large_obj_table {
	void *array;
	volatile void *freed_LIFO;
	volatile void *unallocated_ptr;
};
typedef struct large_obj_table large_obj_table_t;
large_obj_table_t large_obj_table;

extern "C" void print_pseudo_LIFO(volatile void *lifo);
extern "C" void print_LIFO(volatile void *lifo);
extern "C" void print_pg_block_header(pg_block_header_t *pg_block_header);
extern "C" void print_less_pg_block_header(pg_block_header_t *pg_block_header);
extern "C" void print_heap();
extern "C" void print_less_heap();

extern "C" void pg_block_free(pg_block_header_t* pg_block_header);
extern "C" int get_memory_class(size_t size);
extern "C" void *atomic_empty_lifo(volatile void** address);
extern "C" int pseudo_lifo_size(void *lifo);
extern "C" int lifo_size(void *lifo);

struct thread {
	pthread_t id;
	list_t heap[CLASSES];
	pg_block_header_t *local_cache[CLASSES];

	thread() {
		id = pthread_self();
		#ifdef MEMORYLIB_DEBUG
		printf("thread: Implicitly caught thread start, th: %ld\n", id);
		#endif
		for (int i=0; i<CLASSES; i++) {
			list_init(&heap[i]);
			local_cache[i] = NULL;
		}
	}

	~thread() {
		#ifdef MEMORYLIB_DEBUG
		printf("~thread: Implicitly caught thread end, th: %ld\n", id);
		#endif

		// Free local_cache
		for (int i = 0; i < cache_classes; i++) {
			if (local_cache[i] != NULL){
				pg_block_free(local_cache[i]);
			}
		}

		// Free pg_blocks
		for (int memory_class = 0; memory_class < CLASSES; memory_class++) {
			while (1) {
				pg_block_header_t * pg_block_header = (pg_block_header_t*)
					list_remove_front(&heap[memory_class]);
				if (pg_block_header == NULL)
					break;
				do {
					// Move remotely_freed_LIFO to freed_LIFO
					if (pg_block_header->remotely_freed_LIFO != NULL) {
						pg_block_header->freed_LIFO = atomic_empty_lifo(
							&pg_block_header->remotely_freed_LIFO);
						// Count the lifo_size
						if (memory_class == 0)
							pg_block_header->freed_objects += pseudo_lifo_size(
								pg_block_header->freed_LIFO);
						else
							pg_block_header->freed_objects += lifo_size(
								pg_block_header->freed_LIFO);
					}

					// if all of pg_block's obj are freed, free the pg_block
					if (pg_block_header->unallocated_objects + pg_block_header->
						freed_objects == class_info[memory_class].obj_in_pg_block) {
							pg_block_free(pg_block_header);
							break;
					}

					if (pg_block_header->id == id) {
						// Make pg_block orphaned
						pg_block_header->id = 0;
					}
					// if remotely_freed_LIFO isn't NULL repeat the processe
					// If it is NULL change it to 0x1 - orphaned
					if (compare_and_swap_ptr(&pg_block_header->remotely_freed_LIFO,
						NULL, (void*)1) == 0) {
						continue;
					}
					else
						break;
				} while (1);
			}
		}

	}
};
typedef struct thread thread_t;
thread_local thread_t *th = NULL;

extern "C" void *memory_alloc(size_t size) {
	void *mem = mmap(NULL, size, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mem == MAP_FAILED) { handle_error("mmap failed"); }
	return mem;
}

extern "C" void memory_dealloc(void* mem, size_t size) {
	if (munmap(mem, size) == -1) { handle_error("munmap failed"); }
}

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
	printf("cache_class: %u\n", class_info[memory_class].cache_class);
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

extern "C" void print_pseudo_LIFO(volatile void *lifo) {
	int i = 0;
	while (lifo != NULL && lifo != (void*)1) {
		if (i < MAX_PRINT_LIFO)
			printf("%p->", lifo);
		else if (i == MAX_PRINT_LIFO + 1)
			printf(".....->");
		lifo = pseudo_ptr_to_ptr((int*)lifo);
		i++;
	}
	printf("%p\n", lifo);
}

extern "C" void print_LIFO(volatile void *lifo) {
	int i = 0;
	while (lifo != NULL && lifo != (void*)1) {
		if (i < MAX_PRINT_LIFO)
			printf("%p->", lifo);
		else if (i == MAX_PRINT_LIFO + 1)
			printf(".....->");
		lifo = *(void**)lifo;
		i++;
	}
	printf("%p\n", lifo);
}

extern "C" void print_large_obj_table() {
	printf("--------------- large_obj_table ---------------\n");
	printf("array: %p|  unallocated: %p|\n",
		large_obj_table.array, large_obj_table.unallocated_ptr);

	printf("freed_LIFO: ");
	print_LIFO(large_obj_table.freed_LIFO);

	printf("table: ");
	int i = 0;
	for (void *tmp = large_obj_table.array; tmp < large_obj_table.unallocated_ptr;
		tmp = (void*)((long)tmp + 8)) {
			if (i < MAX_PRINT_LIFO)
				printf("%p->", *(void**)tmp);
			else if (i == MAX_PRINT_LIFO + 1)
				printf(".....");
			i++;
	}
	printf("\n");
}

extern "C" void print_local_cache() {
	for (int i = 0; i < cache_classes; i++) {
		printf("local_class[%d]  = %p|  ", i, th->local_cache[i]);
	}
	printf("\n");
}

extern "C" void print_global_cache() {
	for (int i = 0; i < cache_classes; i++) {
		printf("global_class[%d] = %p|  ", i, global_cache[i]);
	}
	printf("\n");
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
	printf("pg_block_header: %p|  unallocated_objects: %4u|  freed_objects: %4u, remotely_freed_LIFO %d|\n",
	pg_block_header, pg_block_header->unallocated_objects,
	pg_block_header->freed_objects,
	(pg_block_header->remotely_freed_LIFO == NULL ||
		pg_block_header->remotely_freed_LIFO == (void*)1)?0:1);
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
	printf("pg_block_header: %p|  unallocated_objects: %4u|  freed_objects: %4u|  remotely_freed_LIFO %d|\n",
	pg_block_header, pg_block_header->unallocated_objects,
	pg_block_header->freed_objects,
	(pg_block_header->remotely_freed_LIFO == NULL ||
		pg_block_header->remotely_freed_LIFO == (void*)1)?0:1);
}

extern "C" void print_heap() {
	printf("--------------- Heap ---------------\n");
	for (int i = 0; i < CLASSES; i++) {
		if (th->heap[i].size == 0)
			continue;
		printf("th: %ld, class: %d, object_size: %d, obj_in_pg_block: %d, pg_blocks: %d\n",
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
		printf("th: %ld, class: %d, object_size: %d, obj_in_pg_block: %d, pg_blocks: %d\n",
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

extern "C" void *atomic_empty_lifo(volatile void** address) {
	void *old_ptr = *(void**)address;
	void *new_ptr = NULL;

	while (compare_and_swap_ptr(address, old_ptr, new_ptr) == 0) {
		//#ifdef MEMORYLIB_DEBUG
		printf("atomic_empty_lifo: compare_and_swap failed, retry: %p\n", old_ptr);
		//#endif
		old_ptr = *(void**)address;
	}

	return old_ptr;
}

extern "C" void *atomic_pop(volatile void** address) {
	void *old_ptr, *new_ptr;
	do {
		old_ptr = *(void**)address;
		if (old_ptr == NULL)
			return NULL;
		new_ptr = *(void**)old_ptr;
	} while (compare_and_swap_ptr(address, old_ptr, new_ptr) == 0);
	return old_ptr;
}

extern "C" void *pseudo_atomic_pop(void** address) {
	void *old_ptr = *address;
	int *new_ptr = (int*)pseudo_ptr_to_ptr((int*)old_ptr);

	while (compare_and_swap_ptr(address, old_ptr, new_ptr) == 0) {
		#ifdef MEMORYLIB_DEBUG
		printf("pseudo_atomic_pop: compare_and_swap failed, retry: %p\n", old_ptr);
		#endif
		old_ptr = *address;
		new_ptr = (int*)pseudo_ptr_to_ptr((int*)old_ptr);
	}

	return old_ptr;
}

extern "C" void *atomic_push(volatile void** address, void* new_ptr) {
	void *old_ptr = *(void**)address;
	*(void**)new_ptr = old_ptr;

	while (compare_and_swap_ptr(address, old_ptr, new_ptr) == 0) {
		//#ifdef MEMORYLIB_DEBUG
		printf("atomic_push: compare_and_swap failed, retry: %p\n", new_ptr);
		//#endif
		old_ptr = *(void**)address;
		*(void**)new_ptr = old_ptr;
	}

	return old_ptr;
}

extern "C" void *pseudo_atomic_push(void** address, void* new_ptr) {
	void *old_ptr = *address;
	*(int*)new_ptr = ptr_to_pseudo_ptr(old_ptr);

	while (compare_and_swap_ptr(address, old_ptr, new_ptr) == 0) {
		//#ifdef MEMORYLIB_DEBUG
		printf("pseudo_atomic_push: compare_and_swap failed, retry: %p\n", new_ptr);
		//#endif
		old_ptr = *address;
		*(int*)new_ptr = ptr_to_pseudo_ptr(old_ptr);
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
extern "C" void pg_block_init(pg_block_header_t *pg_block_header,
	int memory_class) {
	// The first 8 bytes of every page in a pg_block are a pointer
	// to the pg_block_header
	// The pg_block_header is located in the first page of the pg_block,
	// after the 8 bytes pointer
	void *pg_block = pg_block_header_to_pg_block(pg_block_header);

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

// PgManager Allocates memory for memory_class pg_block
extern "C" pg_block_header *pg_block_alloc(int memory_class) {
	// Check to see if there is available pg_block in global_cache
	pg_block_header_t *old_ptr;
	if ((old_ptr = global_cache[class_info[memory_class].cache_class]) != NULL) {
		if (compare_and_swap_ptr(&global_cache[class_info[memory_class].cache_class]
			, old_ptr, NULL) != 0) {
			return old_ptr;
		}
	}
	// Otherwise, allocate memory from OS
	void *pg_block = memory_alloc(class_info[memory_class].pg_block_size);

	return pg_block_to_pg_block_header(pg_block);
}

// PgManager caches or deallocates a pg_block
extern "C" void pg_block_free(pg_block_header_t* pg_block_header) {
	void* pg_block = pg_block_header_to_pg_block(pg_block_header);
	int memory_class = get_memory_class(pg_block_header->object_size);

	// Check if the pg_block can be cached globally
	pg_block_header_t *old_ptr;
	if ((old_ptr = global_cache[class_info[memory_class].cache_class]) == NULL) {
		if (compare_and_swap_ptr(&global_cache[class_info[memory_class].cache_class]
			, old_ptr, pg_block_header) != 0) {
			return;
		}
	}
	// Otherwise, return memory to OS
	memory_dealloc(pg_block, class_info[memory_class].pg_block_size);
}

// Returns a pg_block that is not full
extern "C" pg_block_header *get_pg_block(int memory_class) {
	// Get the first pg_block
	pg_block_header_t *pg_block_header = (pg_block_header_t*)list_get_front(
		&th->heap[memory_class]);
	// Check if there are no pg_blocks or if the pg_block is full
	// (just in case that I allocate an orphaned pg_block
	// that is already being used and is full, check again if its full )
	while (list_is_empty(&th->heap[memory_class]) || pg_block_is_full(pg_block_header)) {
		if (th->local_cache[class_info[memory_class].cache_class] != NULL) {
			// Check local cache
			pg_block_header = th->local_cache[class_info[memory_class].cache_class];
			th->local_cache[class_info[memory_class].cache_class] = NULL;
		}
		else {
			// Allocate pg_block
			pg_block_header = pg_block_alloc(memory_class);
		}
		pg_block_init(pg_block_header, memory_class);

		list_insert_front(&th->heap[memory_class], pg_block_header);

		pg_block_header = (pg_block_header_t*)list_get_front(
			&th->heap[memory_class]);
	}

	return pg_block_header;
}

// Frees memory for pg_block
extern "C" void return_pg_block(pg_block_header_t* pg_block_header) {
	int memory_class = get_memory_class(pg_block_header->object_size);

	// Check if the pg_block can be cached locally
	if (th->local_cache[class_info[memory_class].cache_class] == NULL) {
		th->local_cache[class_info[memory_class].cache_class] = pg_block_header;
		return;
	}
	// Return memory to OS
	pg_block_free(pg_block_header);
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
		void *lifo = atomic_empty_lifo(&pg_block_header->remotely_freed_LIFO);

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
	if (th == NULL) {
		thread_local static thread_t my_th;
		th = &my_th;
	}

	// Check input
	if (size <= 0) {
		printf("my_malloc: Wrong size\n");
		return NULL;
	}
	else if (size > MAX_SIZE_SMALL_OBJ) {
		// I'll return a 16B alligned memory
		void *obj = memory_alloc(size+16);
		*(size_t*)obj = size;
		obj = (void*)((unsigned long)obj + 16);

		void *ptr = atomic_pop(&large_obj_table.freed_LIFO);
		if (ptr != NULL) {
			// Get obj from freed_LIFO
			*(void**)ptr = obj;
		}
		else {
			// Get obj from unallocated
			void *old_ptr, *new_ptr;
			do {
				old_ptr = (void*)large_obj_table.unallocated_ptr;

				if (old_ptr > (void*)((long)large_obj_table.array +
					LARGE_OBJ_TABLE_SIZE)) {
					printf("Run out of memory\n");
					exit(1);
				}
				new_ptr = (void*)((long)large_obj_table.unallocated_ptr + 8);
			} while(compare_and_swap_ptr(&large_obj_table.unallocated_ptr, old_ptr, new_ptr) == 0);
			*(void**)old_ptr = obj;
		}
		return obj;
	}

	int memory_class = get_memory_class(size);

	// Get a pg_block
	pg_block_header_t *pg_block_header = get_pg_block(memory_class);
	// Get an object
	void *obj = obj_alloc(pg_block_header);

	#ifdef MEMORYLIB_DEBUG
		printf("EVENT, my_malloc: alocated %p\n", obj);
		print_less_heap();
		//print_heap();
	#endif
	return obj;
}

extern "C" void my_free(void *ptr) {
	if (th == NULL) {
		thread_local static thread_t my_th;
		th = &my_th;
	}


	for (void *tmp = large_obj_table.array; tmp < large_obj_table.unallocated_ptr;
		tmp = (void*)((long)tmp + 8)) {
		if (*(void**)tmp == ptr) {
			ptr = (void*)((long)ptr - 16);
			memory_dealloc(ptr, *(size_t*)ptr);
			*(void**)tmp = NULL;
			atomic_push(&large_obj_table.freed_LIFO, tmp);
			return;
		}
	}

	// Then it is a small obj
	pg_block_header_t *pg_block_header = get_pg_block_header(ptr);
	int memory_class = get_memory_class(pg_block_header->object_size);

	// Rearange remotely_freed_LIFO
	if (pg_block_header->id != th->id) {
		void *old_ptr;
		while (1) {
			old_ptr = (void*)pg_block_header->remotely_freed_LIFO;
			if (memory_class == 0) {
				*(int*)ptr = ptr_to_pseudo_ptr(old_ptr);
			}
			else {
				*(void**)ptr = old_ptr;
			}

			if (old_ptr == (void*)1) {
				// Found orphaned block - Try to adopt it
				// change id, insert to list free ptr
				// Check if someone else adopted it before me
				if (compare_and_swap_ptr(&pg_block_header->remotely_freed_LIFO, old_ptr,
					 NULL) != 0) {
					 pg_block_header->id = th->id;
					 list_insert_front(&th->heap[memory_class], pg_block_header);
					 //print_heap();
				}
				my_free(ptr);
				return;
			}

			if (compare_and_swap_ptr(&pg_block_header->remotely_freed_LIFO,
				old_ptr, ptr) == 0) {
					#ifdef MEMORYLIB_DEBUG
					printf("my_free: cmp&swap failed, retry\n");
					#endif
				}
				else {
					break;
				}
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
		print_less_heap();
		//print_heap();
	#endif

	if (pg_block_is_empty(pg_block_header)) {
		// If the pg_block is empty, free it
		list_remove(&th->heap[memory_class], pg_block_header);
		return_pg_block(pg_block_header);
	}
	else if (list_get_front(&th->heap[memory_class]) != pg_block_header) {
		// Move the pg_block to the beginning of the list
		list_remove(&th->heap[memory_class], pg_block_header);
		list_insert_front(&th->heap[memory_class], pg_block_header);
	}
}

extern "C" void *my_realloc(void *ptr, size_t size) {
	if (th == NULL) {
		thread_local static thread_t my_th;
		th = &my_th;
	}

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

	int new_memory_class = get_memory_class(size);
	pg_block_header_t *pg_block_header = get_pg_block_header(ptr);
	int old_memory_class = get_memory_class(pg_block_header->object_size);

	if (new_memory_class <= old_memory_class) {
		return ptr;
	}

	void *obj = my_malloc(size);
	memcpy(obj, ptr, pg_block_header->object_size);
	my_free(ptr);

	return obj;
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
	}

	// Assign memory_class to cache_class
	cache_classes = 0;
	unsigned int pg_block_size = 0;
	for (int i = 1; i < CLASSES; i++) {
		global_cache[i] = NULL;

		if (pg_block_size != class_info[i].pg_block_size) {
			pg_block_size = class_info[i].pg_block_size;
			cache_classes++;
		}
		class_info[i].cache_class = cache_classes-1;
	}

	#ifdef MEMORYLIB_DEBUG
	for (int i = 0; i < CLASSES; i++)
		print_memory_class(i);
	print_global_cache();
	#endif

	// Allocate the large_obj_table
	large_obj_table.array = memory_alloc(LARGE_OBJ_TABLE_SIZE);
	large_obj_table.freed_LIFO = NULL;
	large_obj_table.unallocated_ptr = large_obj_table.array;

	#ifdef MEMORYLIB_DEBUG
	printf("\n\n\n\n\n");
	#endif
}

__attribute__((destructor)) static void terminator(void) {
	#ifdef MEMORYLIB_DEBUG
	printf("!!! Library unloading !!!\n");
	#endif
}
