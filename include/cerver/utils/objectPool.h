#ifndef _CERVER_POOL_H_
#define _CERVER_POOL_H_

#include <stdint.h>

typedef struct PoolMember {

    void *data;
    struct PoolMember *next;

} PoolMember;

// The pool is just a custom stack implementation
typedef struct Pool {

    uint32_t size;
    PoolMember *top;
    void (*destroy)(void *data);

} Pool;


#define POOL_SIZE(pool) ((pool)->size)
#define POOL_TOP(pool) ((pool)->top)
#define POOL_DATA(member) ((member)->data)

extern Pool *pool_init (void (*destroy)(void *data));
extern void pool_push (Pool *, void *data);
extern void *pool_pop (Pool *);
extern void pool_clear (Pool *);

#endif