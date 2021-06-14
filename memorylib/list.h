#ifndef __LIST_H__
#define __LIST_H__
#include <stdio.h>
#include <stdlib.h>

struct list {
	unsigned int size;
	void *head;
	void *tail;
};

typedef struct list list_t;

static inline void list_set_next(void* node, void* next) {
	*(void**)node = next;
	return;
}
static inline void list_set_prev(void* node, void* prev) {
	node = (char*)node + sizeof(void*);
	*(void**)node = prev;
	return;
}
static inline void* list_get_next(void* node) {
	return *(void**)node;
}
static inline void* list_get_prev(void* node) {
	return *(void**)((char*)node + sizeof(void*));
}

void list_init(list_t *list);
int list_is_empty(list_t *list);
void list_insert_front(list_t *list, void *node);
void list_insert_back(list_t *list, void *node);
void list_remove(list_t *list, void *node);
void *list_remove_front(list_t *list);
void *list_remove_back(list_t *list);
void *list_get_front(list_t *list);
void *list_get_back(list_t *list);

#endif
