#ifndef ZSTDMT_DEPPOOL_H
#define ZSTDMT_DEPPOOL_H

#include <stddef.h>

#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"

typedef struct ZSTDMT_DepPool_s ZSTDMT_DepPool;

/*! ZSTDMT_DepPool_create()
 * Create a thread pool. The maximum number of threads is 64.
 * Note: Will return NULL if there is an error */

ZSTDMT_DepPool* ZSTDMT_DepPool_create(size_t nbThreads);

/*! ZSTDMT_DepPool_wait()
 * Call this function once you're done adding all jobs to the
 * thread pool. Will wait for all already added jobs to finish
 * running before back control to main thread */

void ZSTDMT_DepPool_wait(ZSTDMT_DepPool* threadPool);

void ZSTDMT_DepPool_free(ZSTDMT_DepPool* threadPool);

/*! ZSTDMT_DepPool_add()
 * Adds a new job to the thread pool. The function will return
 * the job id of the job you just added. Specify up to 10 jobs
 * using their job ids that must finish before the newly added
 * job starts running */

size_t ZSTDMT_DepPool_add(ZSTDMT_DepPool* threadPool, void (*function)(void*),
    void* functionData, size_t* dependencyJobIds, size_t dependencyJobIdsSize);

#endif /* ZSTDMT_DEPPOOL_H */
