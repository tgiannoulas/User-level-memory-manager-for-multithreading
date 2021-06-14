#include "list.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//#define LISTLIB_DEBUG

// Creates a new list_t type list
// Returns the pointer to the list if created succesfully
// If there was an error, the function exits
void list_init(list_t *list) {
	list->size = 0;
	list->head = NULL;
	list->tail = NULL;
	return;
}

// Returns 1 if list is empty, 0 if it's not empty
int list_is_empty(list_t *list) {
	if (list->size == 0) {
		return 1;
	}
	return 0;
}

// Inserts node in front of the list, as the head of the list
void list_insert_front(list_t *list, void *node) {
	if (list->size == 0) {
		list_set_next(node, node);
		list_set_prev(node, node);
		list->head = node;
		list->tail = node;
	}
	else {
		list_set_next(node, list->head);
		list_set_prev(node, list->tail);
		list_set_next(list->tail, node);
		list_set_prev(list->head, node);
		list->head = node;
	}
	list->size++;

	#ifdef LISTLIB_DEBUG
	putchar('\n');
	printf("File: %s, Line: %d: list_insert_front, node: %p\n",
		__FILE__, __LINE__, node);
	printf("File: %s, Line: %d: list_insert_front, head: %p\n",
		__FILE__, __LINE__, list->head);
	printf("File: %s, Line: %d: list_insert_front, tail: %p\n",
		__FILE__, __LINE__, list->tail);
	printf("File: %s, Line: %d: list_insert_front, node-next: %p\n",
		__FILE__, __LINE__, list_get_next(node));
	printf("File: %s, Line: %d: list_insert_front, node-prev: %p\n",
		__FILE__, __LINE__, list_get_prev(node));
	putchar('\n');
	#endif
	return;
}

// Inserts node at the end of the list, as the tail of the list
void list_insert_back(list_t *list, void *node) {
	if (list->size == 0) {
		list_set_next(node, node);
		list_set_prev(node, node);
		list->head = node;
		list->tail = node;
	}
	else {
		list_set_next(node, list->head);
		list_set_prev(node, list->tail);
		list_set_next(list->tail, node);
		list_set_prev(list->head, node);
		list->tail = node;
	}
	list->size++;

	#ifdef LISTLIB_DEBUG
	putchar('\n');
	printf("File: %s, Line: %d: list_insert_back, node: %p\n",
		__FILE__, __LINE__, node);
	printf("File: %s, Line: %d: list_insert_back, head: %p\n",
		__FILE__, __LINE__, list->head);
	printf("File: %s, Line: %d: list_insert_back, tail: %p\n",
		__FILE__, __LINE__, list->tail);
	printf("File: %s, Line: %d: list_insert_back, node-next: %p\n",
		__FILE__, __LINE__, list_get_next(node));
	printf("File: %s, Line: %d: list_insert_back, node-prev: %p\n",
		__FILE__, __LINE__, list_get_prev(node));
	putchar('\n');
	#endif
	return;
}

// Removes node from the list
void list_remove(list_t *list, void *node) {
	if (list->size == 0) {
		return;
	}

	// Check to see if node is part of the list
	void *cur = list->head;
	while (cur != node) {
		if (cur == list->tail) {
			//reached the end
			return;
		}
		cur = list_get_next(cur);
	}

	if (list->size == 1) {
		list->head = NULL;
		list->tail = NULL;
	}
	else {
		void *prev = list_get_prev(node);
		void *next = list_get_next(node);
		if (node == list->head) {
			list->head = next;
		}
		if (node == list->tail) {
			list->tail = prev;
		}
		list_set_next(prev, next);
		list_set_prev(next, prev);
		list_set_next(node, NULL);
		list_set_prev(node, NULL);
	}
	list->size--;

	#ifdef LISTLIB_DEBUG
	putchar('\n');
	printf("File: %s, Line: %d: list_remove, node: %p\n",
		__FILE__, __LINE__, node);
	printf("File: %s, Line: %d: list_remove, head: %p\n",
		__FILE__, __LINE__, list->head);
	printf("File: %s, Line: %d: list_remove, tail: %p\n",
		__FILE__, __LINE__, list->tail);
	printf("File: %s, Line: %d: list_insert_back, node-next: %p\n",
		__FILE__, __LINE__, list_get_next(node));
	printf("File: %s, Line: %d: list_insert_back, node-prev: %p\n",
		__FILE__, __LINE__, list_get_prev(node));
	#endif
	return;
}

// Removes the head of the list
void *list_remove_front(list_t *list) {
	if (list->size == 0) {
		return NULL;
	}

	void* node = list->head;
	if (list->size == 1) {
		list->head = NULL;
		list->tail = NULL;
	}
	else {
		list->head = list_get_next(node);
		list_set_next(list->tail, list->head);
		list_set_prev(list->head, list->tail);
		list_set_next(node, NULL);
		list_set_prev(node, NULL);
	}
	list->size--;

	#ifdef LISTLIB_DEBUG
	putchar('\n');
	printf("File: %s, Line: %d: list_remove_front, node: %p\n",
		__FILE__, __LINE__, node);
	printf("File: %s, Line: %d: list_remove_front, head: %p\n",
		__FILE__, __LINE__, list->head);
	printf("File: %s, Line: %d: list_remove_front, tail: %p\n",
		__FILE__, __LINE__, list->tail);
	printf("File: %s, Line: %d: list_insert_back, node-next: %p\n",
		__FILE__, __LINE__, list_get_next(node));
	printf("File: %s, Line: %d: list_insert_back, node-prev: %p\n",
		__FILE__, __LINE__, list_get_prev(node));
	putchar('\n');
	#endif
	return node;
}

// Removes the tail of the list
void *list_remove_back(list_t *list) {
	if (list->size == 0) {
		return NULL;
	}

	void* node = list->tail;
	if (list->size == 1) {
		list->head = NULL;
		list->tail = NULL;
	}
	else {
		list->tail = list_get_prev(node);
		list_set_next(list->tail, list->head);
		list_set_prev(list->head, list->tail);
		list_set_next(node, NULL);
		list_set_prev(node, NULL);
	}
	list->size--;

	#ifdef LISTLIB_DEBUG
	putchar('\n');
	printf("File: %s, Line: %d: list_remove_end, node: %p\n",
		__FILE__, __LINE__, node);
	printf("File: %s, Line: %d: list_remove_end, head: %p\n",
		__FILE__, __LINE__, list->head);
	printf("File: %s, Line: %d: list_remove_end, tail: %p\n",
		__FILE__, __LINE__, list->tail);
	printf("File: %s, Line: %d: list_insert_back, node-next: %p\n",
		__FILE__, __LINE__, list_get_next(node));
	printf("File: %s, Line: %d: list_insert_back, node-prev: %p\n",
		__FILE__, __LINE__, list_get_prev(node));
	putchar('\n');
	#endif
	return node;
}

// Returns the head
void *list_get_front(list_t *list) {
	return (list->head);
}

// Returns the tail
void *list_get_back(list_t *list) {
	return (list->tail);
}
