/* $begin cbufc */
#include "csapp.h"
#include "cbuf.h"


/* Create an empty, bounded, shared FIFO buffer with n slots */
void cbuf_init(cbuf_t *sp, int n)
{
    sp->buf = Calloc(n, sizeof(char *)); 
    sp->n = n;                       /* Buffer holds max of n items */
    sp->front = sp->rear = 0;        /* Empty buffer iff front == rear */
    Sem_init(&sp->mutex, 0, 1);      /* Binary semaphore for locking */
    Sem_init(&sp->slots, 0, n);      /* Initially, buf has n empty slots */
    Sem_init(&sp->items, 0, 0);      /* Initially, buf has zero data items */
}

/* Clean up buffer sp */
void cbuf_deinit(cbuf_t *sp)
{
    Free(sp->buf);
}

/* Insert item onto the rear of shared buffer sp */
void cbuf_insert(cbuf_t *sp, char* item)
{
    P(&sp->slots);                          /* Wait for available slot */
    P(&sp->mutex);                          /* Lock the buffer */
    sp->rear = (sp->rear + 1) % sp->n;      /* Update rear */
    sp->buf[sp->rear] = item;               /* Insert the item */
    V(&sp->mutex);                          /* Unlock the buffer */
    V(&sp->items);                          /* Announce available item */
}

/* Remove and return the first item from buffer sp */
char* cbuf_remove(cbuf_t *sp)
{
    char *item;
    P(&sp->items);                          /* Wait for available item */
    P(&sp->mutex);                          /* Lock the buffer */
    sp->front = (sp->front + 1) % sp->n;    /* Update front */
    item = sp->buf[sp->front];              /* Remove the item */
    V(&sp->mutex);                          /* Unlock the buffer */
    V(&sp->slots);                          /* Announce available slot */
    return item;
}
/* $end cbufc */

