#ifndef __CACHE_H__
#define __CACHE_H__

#include "csapp.h"

typedef struct node_t {
    int size;
    struct node_t *next, *prev;
    char uri[100];
    char *data;
} node_t;

typedef struct cache_t {
    int size;       // the current size of the cache
    node_t *head;     // pointer to the first node in the list
    node_t *tail;
    sem_t mutex;    // protects accesses to the cache
} cache_t;

void cache_init(cache_t *cache);
void cache_deinit(cache_t *cache);
void cache_add(cache_t *cache, int size, char* uri, char *data);
void cache_remove(cache_t *cache);
char *cache_find(cache_t *cache, char* uri);

#endif