#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>

// memory quota module used to limit copytool memory usage
// in current code, most of memory used by small file (no more than MAX_OBJ_SIZE_LEVEL)
// we can start by only control memory usage by those files

static  size_t quota_total;
static  size_t quota_left;
static  size_t quota_waiters;
static  pthread_mutex_t  quota_mutex;
static  pthread_cond_t    quota_cond;

void quota_mem_init(size_t quota_size)
{
    quota_total = quota_size;
    quota_left = quota_size;
    quota_waiters = 0;
    pthread_mutex_init (&quota_mutex, NULL);
    pthread_cond_init (&quota_cond, NULL);
}

void quota_mem_destroy()
{
    pthread_mutex_destroy(&quota_mutex);
    pthread_cond_destroy (&quota_cond);
}

void quota_mem_free(void *mem_ptr, size_t mem_size)
{
    bool notify_waiter = false;
retry:
    pthread_mutex_lock(&quota_mutex);
    quota_left += mem_size;
    if (quota_waiters)  notify_waiter = true;
    pthread_mutex_unlock(&quota_mutex);
    free(mem_ptr);
    if (notify_waiter)
        pthread_cond_signal(&quota_cond);
}

void *quota_mem_alloc(size_t alloc_size)
{
    bool alloc_it = false;
retry:
    pthread_mutex_lock(&quota_mutex);
    if (alloc_size <= quota_left)
    {
        alloc_it = true;
        quota_left -= alloc_size;
        pthread_mutex_unlock(&quota_mutex);
        void *ptr = malloc(alloc_size);
        if (ptr == NULL)  {
            // return quota
            pthread_mutex_lock(&quota_mutex);
            quota_left += alloc_size;
            pthread_mutex_unlock(&quota_mutex);
        }
        return ptr;
    } else {
        quota_waiters++;
        int ret = pthread_cond_wait(&quota_cond, &quota_mutex);
        quota_waiters--;
        pthread_mutex_unlock(&quota_mutex);
        goto retry;
    }
}