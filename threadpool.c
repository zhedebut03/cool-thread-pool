#include "threadpool.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>


void *thread_routine(void *arg)
{
    struct timespec abstime;
    int timeout;
    threadpool_t *pool = (threadpool_t *)arg;
    while(1){
        timeout = 0;
        if(pthread_mutex_lock(&pool->p_mutex)!=0){
            perror("pthread_mutex_lock");
        }
        pool->idle++;
        while(!pool->first){  
            clock_gettime(CLOCK_REALTIME, &abstime);  
            abstime.tv_sec += 2;
            int status;
            status = pthread_cond_timedwait(&pool->p_cond, &pool->p_mutex, &abstime);
            if(status == ETIMEDOUT){
                timeout = 1;
                break;
            }
        }
        pool->idle--;

        if(pool->first != NULL){
            task_t *t = pool->first;
            pool->first = t->next;   // get work

            if(pthread_mutex_unlock(&pool->p_mutex)!=0){
                perror("pthread_mutex_unlock");
            }

            t->function(t->arg);
            free(t);

            if(pthread_mutex_lock(&pool->p_mutex)!=0){
                perror("pthread_mutex_lock");
            }
        }

        if(timeout == 1){
            pool->counter--;   // Number of threads currently working -1
            if(pthread_mutex_unlock(&pool->p_mutex)!=0){
                perror("pthread_mutex_unlock");
            }
            break;
        }
        if(pthread_mutex_unlock(&pool->p_mutex)!=0){
            perror("pthread_mutex_unlock");
        }
    }
    return NULL;
}

void threadpool_init(threadpool_t *pool, int threads)
{
    if(pthread_mutex_init(&pool->p_mutex, NULL)!=0){
        perror("pthread_mutex_init");
    }
    if(pthread_cond_init(&pool->p_cond, NULL)!=0){
        perror("pthread_cond_init");
    }
    pool->first = NULL;
    pool->last = NULL;
    pool->counter = 0;
    pool->idle = 0;
    pool->max_threads = threads;
    pthread_t tid;
    for (int i = 0; i < threads; i++){        // create threads
        if(pthread_create(&tid, NULL, thread_routine, pool)!=0){
            perror("pthread_create");
        }
    }
}

void threadpool_add_task(threadpool_t *pool, void (*function)(void *arg), void *arg)
{
    task_t *newtask = (task_t *)malloc(sizeof(task_t));
    newtask->function = function;
    newtask->arg = arg;
    newtask->next = NULL;  // The newly added task is placed at the end of the queue
    
    if(pthread_mutex_lock(&pool->p_mutex)!=0){
        perror("pthread_mutex_lock");
    }
    if(pool->first == NULL){   
        pool->first = newtask;
    }else{
        pool->last->next = newtask;
    }
    pool->last = newtask; 
    
    if(pool->idle > 0){    //notify the pool that new task arrived!
        if(pthread_cond_signal(&pool->p_cond)!=0){
            perror("pthread_cond_signal");
        }
    }else if(pool->counter < pool->max_threads){
        pool->counter++;
    }
    if(pthread_mutex_unlock(&pool->p_mutex)!=0){
        perror("pthread_mutex_unlock");
    }
}
