#ifndef ZSTDMT_THREADPOOL_H
#define ZSTDMT_THREADPOOL_H

#include <stddef.h>

#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"

typedef struct ZSTDMT_ThreadPool_s ZSTDMT_ThreadPool;

/*! ZSTDMT_ThreadPool_create()
 * Create a thread pool. The maximum number of threads is 64.
 * Note: Will return NULL if there is an error */

ZSTDMT_ThreadPool* ZSTDMT_ThreadPool_create(size_t nbThreads);

/*! ZSTDMT_ThreadPool_wait()
 * Call this function once you're done adding all jobs to the
 * thread pool. Will wait for all already added jobs to finish
 * running before back control to main thread */

void ZSTDMT_ThreadPool_wait(ZSTDMT_ThreadPool* threadPool);

void ZSTDMT_ThreadPool_free(ZSTDMT_ThreadPool* threadPool);

/*! ZSTDMT_ThreadPool_add()
 * Adds a new job to the thread pool. The function will return
 * the job id of the job you just added. Specify up to 10 jobs
 * using their job ids that must finish before the newly added
 * job starts running */

size_t ZSTDMT_ThreadPool_add(ZSTDMT_ThreadPool* threadPool, void (*function)(void*),
    void* functionData, size_t* dependencyJobIds, size_t dependencyJobIdsSize);

#endif /* ZSTDMT_THREADPOOL_H */
