#ifndef _THREAD_POOL_H_
#define _THREAD_POOL_H_
#include <pthread.h>

typedef struct task
{
    void (*function)(void *args);
    void *arg;             
    struct task *next;    
}task_t;

typedef struct threadpool
{
    pthread_mutex_t p_mutex;
    pthread_cond_t p_cond;
    task_t *first;      // queue head
    task_t *last;       // queue tail
    int counter;         
    int idle;            
    int max_threads;   // threads_num
}threadpool_t;

void threadpool_init(threadpool_t *pool, int threads);

void threadpool_add_task(threadpool_t *pool, void (*function)(void *arg), void *arg);

void threadpool_destroy(threadpool_t *pool);

#endif
