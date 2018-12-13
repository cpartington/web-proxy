#ifndef __CBUF_H__
#define __CBUF_H__

#include "csapp.h"

/* $begin cbuft */
typedef struct {
    char **buf;          /* Buffer array */         
    int n;             /* Maximum number of slots */
    int front;         /* buf[(front+1)%n] is first item */
    int rear;          /* buf[rear%n] is last item */
    sem_t mutex;       /* Protects accesses to buf */
    sem_t slots;       /* Counts available slots */
    sem_t items;       /* Counts available items */
} cbuf_t;
/* $end cbuft */

void cbuf_init(cbuf_t *sp, int n);
void cbuf_deinit(cbuf_t *sp);
void cbuf_insert(cbuf_t *sp, char* item);
char* cbuf_remove(cbuf_t *sp);

#endif /* __cbuf_H__ */
