#include "cache.h"

#define MAX_CACHE_SIZE 1049000

void cache_init(cache_t *cache) {
    cache->size = 0;
    cache->head = NULL;
    cache->tail = NULL;
    Sem_init(&cache->mutex, 0, 1);
}

void cache_deinit(cache_t *cache) {
    if (cache->head != NULL && cache->head != cache->tail) {
        node_t *node = cache->head;
        while (node->next != NULL) {
            node = node->next;
            free(node->data);
            free(node->prev);
        }
        free(cache->tail);
    }
    else if (cache->head != NULL) {
        free(cache->head);
    }
}

void cache_add(cache_t *cache, int size, char* uri, char *data) {
    // Create the new node
    node_t *node = malloc(sizeof(struct node_t));
    node->size = size;
    memcpy(node->uri, uri, sizeof(node->uri));
    node->data = data;
    node->prev = NULL;
    printf("Adding new object to cache of size %d and request key '%s'\n",
           node->size, node->uri);
    
    // Add to the cache
    P(&cache->mutex);
    while (cache->size + size > MAX_CACHE_SIZE) {
        cache_remove(cache);
    }
    node->next = cache->head;
    if (cache->head != NULL)
        cache->head->prev = node;
    cache->head = node;
    cache->size += size;
    V(&cache->mutex);
}

void cache_remove(cache_t *cache) {
    node_t *to_remove = cache->tail;
    to_remove->prev->next = NULL;
    cache->size -= to_remove->size;
    free(to_remove->data);
    free(to_remove);
}

char *cache_find(cache_t *cache, char* uri) {
    if (cache->head == NULL) {
        return NULL;
    }
    
    node_t *node = cache->head;
    while (node != NULL) {
        if (strcmp(node->uri, uri) == 0) {
            return node->data;
        }
        else {
            node = node->next;
        }
    }
    return NULL;
}