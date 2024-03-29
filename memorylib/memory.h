#ifndef __MEMORY_H__
#define __MEMORY_H__

void *my_malloc(size_t size);
void my_free(void *ptr);
void *my_realloc(void *ptr, size_t size);
void print_less_heap();
void print_heap();
void print_local_cache();
void print_global_cache();
void print_large_obj_table();

#endif
