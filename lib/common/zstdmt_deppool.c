/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#include <stddef.h>
#include "debug.h"
#include "zstd_internal.h"
#include "zstdmt_deppool.h"

#if defined(_MSC_VER)
#  pragma warning(disable : 4204)
#endif

#ifdef ZSTD_MULTITHREAD

#include "threading.h"

/* ZSTDMT_LinkedList
 * -----------------
 * This is a doubly linked list that the thread pool will use to hold jobs
 * I'm using this because I need O(1) remove complexity after a job has
 * finished running */

typedef struct ZSTDMT_LinkedListNode_s ZSTDMT_LinkedListNode;

struct ZSTDMT_LinkedListNode_s {ZSTDMT_LinkedListNode* next; ZSTDMT_LinkedListNode* prev; void* data;};

typedef struct {ZSTDMT_LinkedListNode* head;} ZSTDMT_LinkedList;

static ZSTDMT_LinkedList* ZSTDMT_LinkedList_create(void)
{
    ZSTDMT_LinkedList* list = (ZSTDMT_LinkedList*)ZSTD_malloc(sizeof(ZSTDMT_LinkedList), ZSTD_defaultCMem);
    return list;
}

static void ZSTDMT_LinkedList_free(ZSTDMT_LinkedList* list)
{
    ZSTDMT_LinkedListNode* node = list->head;
    ZSTDMT_LinkedListNode* nextNode;
    while (node) {nextNode = node->next; ZSTD_free(node, ZSTD_defaultCMem); node = nextNode;}
    ZSTD_free(list, ZSTD_defaultCMem);
}

static ZSTDMT_LinkedListNode* ZSTDMT_LinkedList_add(ZSTDMT_LinkedList* list, void* data)
{
    ZSTDMT_LinkedListNode* node = (ZSTDMT_LinkedListNode*)ZSTD_malloc(sizeof(ZSTDMT_LinkedListNode), ZSTD_defaultCMem);
    node->data = data; node->next = NULL; node->prev = NULL;
    if (!list->head) list->head = node;
    else {node->next = list->head; list->head->prev = node; list->head = node;}
    return node;
}

static void ZSTDMT_LinkedList_del(ZSTDMT_LinkedList* list, ZSTDMT_LinkedListNode* node)
{
    if (list->head == node) list->head = node->next;
    if (node->prev) node->prev->next = node->next;
    if (node->next) node->next->prev = node->prev;
    ZSTD_free(node, ZSTD_defaultCMem);
}

/* ZSTDMT_JobStatus
 * ----------------
 * I'm using this enum to indicate the result of ZSTDMT_DepPool_nextReadyJob() */

typedef enum {ZSTDMT_JobReadyInThreadPool, ZSTDMT_NoReadyJobsInThreadPool, ZSTDMT_AllJobsStartedInThreadPool} ZSTDMT_JobStatus;

/* ZSTDMT_Job
 * ----------
 * A job is just a callback function and a void* (for arguments). Each job requires
 * the jobs correponding to the ids in dependencyJobIds (indexed into jobsArray)
 * to be finished first. It also has a reference to the linked list 'node'
 * that holds it in jobsList so that it can remove it from the list in O(1)
 * when it is done running */

#define MAX_NB_JOB_DEPENDENCIES 10
typedef struct {
    void (*function)(void*);
    void* functionData;
    size_t dependencyJobIds[MAX_NB_JOB_DEPENDENCIES];
    size_t nbDependencies;
    int finished;
    ZSTDMT_LinkedListNode* node;
    size_t jobId;
} ZSTDMT_Job;

/* ZSTDMT_DepPool context
 * -------------------------
 * Has a cond for knowing when jobs are ready to be run. 'allJobsSupplied'
 * is set when the user of the ThreadPool has finished adding all jobs */

#define MAX_NB_THREADS 64
#define MAX_NB_JOBS 1024 * 10000
struct ZSTDMT_DepPool_s {
    ZSTD_pthread_mutex_t mutex;
    ZSTD_pthread_cond_t cond;
    ZSTD_pthread_t threads[MAX_NB_THREADS];
    size_t nbThreads;
    ZSTDMT_Job* jobsArray[MAX_NB_JOBS];
    ZSTDMT_LinkedList* jobsList;
    size_t nbJobs;
    int allJobsSupplied;
};

static ZSTDMT_JobStatus ZSTDMT_DepPool_nextReadyJob(ZSTDMT_Job** nextJob, ZSTDMT_DepPool* pool)
{
    int jobsListEmpty = 1;
    ZSTDMT_LinkedListNode* node;
    for (node = pool->jobsList->head; node; node = node->next) {
        ZSTDMT_Job* job = (ZSTDMT_Job*)(node->data);
        jobsListEmpty = 0;

        /* Loop through the dependent jobs and check to see if all
         * of them have finished running. If they have, set nextJob
         * to the current job */

        { int dependenciesSatisfied = 1; size_t i;
          for (i = 0; i < job->nbDependencies; i++)
            dependenciesSatisfied &= pool->jobsArray[job->dependencyJobIds[i]]->finished;
          if (dependenciesSatisfied) {*nextJob = job; return ZSTDMT_JobReadyInThreadPool;}}
    }

    /* If we entered the loop, indicate that we don't have a ready job yet.
     * Otherwise, all jobs have at least already been started */

    return !jobsListEmpty ? ZSTDMT_NoReadyJobsInThreadPool : ZSTDMT_AllJobsStartedInThreadPool;
}

static void* ZSTDMT_DepPool_threadRoutine(void* data)
{
    ZSTDMT_DepPool* pool = (ZSTDMT_DepPool*)data;

    /* Lock the pool mutex to start because we're going to use it
     * inside ZSTDMT_DepPool_nextReadyJob() */

    ZSTD_pthread_mutex_lock(&pool->mutex);
    while (1) {
        ZSTDMT_Job* job;
        ZSTDMT_JobStatus jobStatus = ZSTDMT_DepPool_nextReadyJob(&job, pool);

        /* If all the jobs enqueued so far have at least started and the
         * user has told us that they are done adding jobs, we can break
         * and begin waiting for the running threads to join the main one */

        if (pool->allJobsSupplied && jobStatus == ZSTDMT_AllJobsStartedInThreadPool) break;

        /* If we didn't get a job back because there are jobs whose
         * dependent jobs havne't finished running, just wait */

        if (jobStatus != ZSTDMT_JobReadyInThreadPool) {
            ZSTD_pthread_cond_wait(&pool->cond, &pool->mutex);
            continue;
        }

        /* We remove the node corresponding to the job so that
         * we don't iterate through it anymore when we call
         * ZSTDMT_DepPool_nextReadyJob(). The job isn't freed as
         * its dependents might still need access to the finished flag */

        ZSTDMT_LinkedList_del(pool->jobsList, job->node);

        /* We don't need to hold the mutex when actually running the job
         * and pthread_cond_wait re-locks the mutex so we unlock it here */

        ZSTD_pthread_mutex_unlock(&pool->mutex);

        /* Run the job */

        job->function(job->functionData);

        /* Re-lock the mutex because we will need to set the finished
         * flag for the job and remove the corresonding node from the
         * linkedlist */

        ZSTD_pthread_mutex_lock(&pool->mutex);

        job->finished = 1;

        /* Wake up at least one thread */

        ZSTD_pthread_cond_signal(&pool->cond);
    }

    /* Wake up all threads once we're sure that all the jobs queued
     * have been at least started */

    ZSTD_pthread_cond_broadcast(&pool->cond);
    ZSTD_pthread_mutex_unlock(&pool->mutex);
    return NULL;
}

/* Thread Pool api
 * --------------- */

ZSTDMT_DepPool* ZSTDMT_DepPool_create(size_t nbThreads)
{
    ZSTDMT_DepPool* pool = (ZSTDMT_DepPool*)ZSTD_malloc(sizeof(ZSTDMT_DepPool), ZSTD_defaultCMem);

    int error = 0;

    pool->jobsList = ZSTDMT_LinkedList_create();
    pool->nbThreads = nbThreads; pool->nbJobs = 0; pool->allJobsSupplied = 0;
    { error |= ZSTD_pthread_mutex_init(&pool->mutex, NULL);
      error |= ZSTD_pthread_cond_init(&pool->cond, NULL);}
    { size_t i;
      for (i = 0; i < nbThreads; i++)
        error |= ZSTD_pthread_create(&pool->threads[i], NULL, ZSTDMT_DepPool_threadRoutine, (void*)pool);}

    /* Bail if there is something wrong when initializing the mutex,
     * cond or pthreads above */

    if (error) {ZSTDMT_LinkedList_free(pool->jobsList); ZSTD_free(pool, ZSTD_defaultCMem); return NULL;}
    return pool;
}

void ZSTDMT_DepPool_wait(ZSTDMT_DepPool* pool)
{
    ZSTD_pthread_mutex_lock(&pool->mutex);

    /* We assume the user is done loading jobs to the thread pool
     * when they call this function and thus set the
     * 'allJobsSupplied' flag */

    pool->allJobsSupplied = 1;
    ZSTD_pthread_cond_broadcast(&pool->cond);
    ZSTD_pthread_mutex_unlock(&pool->mutex);

    /* Join all the threads back into main */

    { size_t i;
      for (i = 0; i < pool->nbThreads; i++)
        ZSTD_pthread_join(pool->threads[i], NULL);}
}

void ZSTDMT_DepPool_free(ZSTDMT_DepPool* pool)
{
    ZSTDMT_LinkedList_free(pool->jobsList);

    /* Free the jobs here and not after they finish running */

    { size_t i;
      for (i = 0; i < pool->nbJobs; i++)
        ZSTD_free(pool->jobsArray[i], ZSTD_defaultCMem);}
    ZSTD_free(pool, ZSTD_defaultCMem);
}

size_t ZSTDMT_DepPool_add(ZSTDMT_DepPool* pool, void (*function)(void*), void* functionData,
                             size_t* dependencyJobIds, size_t nbDependencies)
{
    size_t jobId;
    ZSTDMT_Job* job = (ZSTDMT_Job*)ZSTD_malloc(sizeof(ZSTDMT_Job), ZSTD_defaultCMem);
    job->function = function; job->functionData = functionData; job->nbDependencies = nbDependencies;

    /* We memcpy the dependencyJobIds so that changes to the array that
     * the user might make on their end don't cause undefined behavior */

    memcpy(job->dependencyJobIds, dependencyJobIds, nbDependencies * sizeof(size_t));
    ZSTD_pthread_mutex_lock(&pool->mutex);

    /* The 'jobId' is just the index that the job gets put into in 'jobsArray' */

    jobId = pool->nbJobs;
    job->jobId = jobId;

    /* Set the node reference so that we can remove it once the job
     * finished in O(1) */

    { ZSTDMT_LinkedListNode* node = ZSTDMT_LinkedList_add(pool->jobsList, (void*)job);
      job->node = node;}
    pool->jobsArray[pool->nbJobs++] = job;

    /* Wake up at least one thread */

    ZSTD_pthread_cond_signal(&pool->cond);
    ZSTD_pthread_mutex_unlock(&pool->mutex);
    return jobId;
}

#endif  /* ZSTD_MULTITHREAD */
