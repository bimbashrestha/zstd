/*
 * Copyright (c) 2015-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "zstdmt_deppool.h"

/* Sequence jobs: testing that order is respected
 * 1 - 2 - 3 - 4 - 5
 */

void sequenceJob1(void* data) {printf("1 ");}
void sequenceJob2(void* data) {printf("2 ");}
void sequenceJob3(void* data) {printf("3 ");}
void sequenceJob4(void* data) {printf("4 ");}
void sequenceJob5(void* data) {printf("5 ");}

/* Crew jobs: testing that multiple dependencies are handled
 *     |-- 2 --|
 *     |-- 3 --|
 * 1 --|-- 4 --|--- 7
 *     |-- 5 --|
 *     |-- 6 --|
 */

void crewJob1(void* data) {printf("1 ");}
void crewJob2(void* data) {printf("2 ");}
void crewJob3(void* data) {printf("3 ");}
void crewJob4(void* data) {printf("4 ");}
void crewJob5(void* data) {printf("5 ");}
void crewJob6(void* data) {printf("6 ");}
void crewJob7(void* data) {printf("7 ");}

/* Shifted jobs: testing multiple starting and ending jobs instead of just 1
 * 1 -- 2 -- 3 -- 4
 *       \    \
 *   5 -- 6 -- 7 -- 8
 *         \    \
 *     9 -- 10 - 11
 */

void shiftedJob1(void* data) {printf("1 ");}
void shiftedJob2(void* data) {printf("2 ");}
void shiftedJob3(void* data) {printf("3 ");}
void shiftedJob4(void* data) {printf("4 ");}
void shiftedJob5(void* data) {printf("5 ");}
void shiftedJob6(void* data) {printf("6 ");}
void shiftedJob7(void* data) {printf("7 ");}
void shiftedJob8(void* data) {printf("8 ");}
void shiftedJob9(void* data) {printf("9 ");}
void shiftedJob10(void* data) {printf("10 ");}
void shiftedJob11(void* data) {printf("11 ");}

int main(int argc, char** argv)
{
    if (argc != 3) return 0;
    int nbThreads = atoi(argv[argc-2]);
    if (!strcmp(argv[argc-1], "-sequential")) {
        ZSTDMT_DepPool* threadPool = ZSTDMT_DepPool_create(nbThreads);
        size_t job1Id = ZSTDMT_DepPool_add(threadPool, sequenceJob1, NULL, NULL, 0);
        size_t job2Id = ZSTDMT_DepPool_add(threadPool, sequenceJob2, NULL, &job1Id, 1);
        size_t job3Id = ZSTDMT_DepPool_add(threadPool, sequenceJob3, NULL, &job2Id, 1);
        size_t job4Id = ZSTDMT_DepPool_add(threadPool, sequenceJob4, NULL, &job3Id, 1);
        ZSTDMT_DepPool_add(threadPool, sequenceJob5, NULL, &job4Id, 1);
        ZSTDMT_DepPool_wait(threadPool);
        ZSTDMT_DepPool_free(threadPool);
    }
    if (!strcmp(argv[argc-1], "-crew")) {
        ZSTDMT_DepPool* threadPool = ZSTDMT_DepPool_create(nbThreads);
        size_t job1Id = ZSTDMT_DepPool_add(threadPool, crewJob1, NULL, NULL, 0);
        size_t job2Id = ZSTDMT_DepPool_add(threadPool, crewJob2, NULL, &job1Id, 1);
        size_t job3Id = ZSTDMT_DepPool_add(threadPool, crewJob3, NULL, &job1Id, 1);
        size_t job4Id = ZSTDMT_DepPool_add(threadPool, crewJob4, NULL, &job1Id, 1);
        size_t job5Id = ZSTDMT_DepPool_add(threadPool, crewJob5, NULL, &job1Id, 1);
        size_t job6Id = ZSTDMT_DepPool_add(threadPool, crewJob6, NULL, &job1Id, 1);
        size_t jobDepIds[] = {job2Id, job3Id, job4Id, job5Id, job6Id};
        ZSTDMT_DepPool_add(threadPool, crewJob7, NULL, jobDepIds, 5);
        ZSTDMT_DepPool_wait(threadPool);
        ZSTDMT_DepPool_free(threadPool);
    }
    if (!strcmp(argv[argc-1], "-shifted")) {
        ZSTDMT_DepPool* threadPool = ZSTDMT_DepPool_create(nbThreads);
        size_t job1Id = ZSTDMT_DepPool_add(threadPool, shiftedJob1, NULL, NULL, 0);
        size_t job5Id = ZSTDMT_DepPool_add(threadPool, shiftedJob5, NULL, NULL, 0);
        size_t job9Id = ZSTDMT_DepPool_add(threadPool, shiftedJob9, NULL, NULL, 0);

        size_t job2Id = ZSTDMT_DepPool_add(threadPool, shiftedJob2, NULL, &job1Id, 1);
        size_t job3Id = ZSTDMT_DepPool_add(threadPool, shiftedJob3, NULL, &job2Id, 1);
        ZSTDMT_DepPool_add(threadPool, shiftedJob4, NULL, &job3Id, 1);

        size_t job6DepIds[] = {job2Id, job5Id};
        size_t job6Id = ZSTDMT_DepPool_add(threadPool, shiftedJob6, NULL, job6DepIds, 2);
        size_t job7DepIds[] = {job3Id, job6Id};
        size_t job7Id = ZSTDMT_DepPool_add(threadPool, shiftedJob7, NULL, job7DepIds, 2);
        ZSTDMT_DepPool_add(threadPool, shiftedJob8, NULL, &job7Id, 1);

        size_t job10DepIds[] = {job6Id, job9Id};
        size_t job10Id = ZSTDMT_DepPool_add(threadPool, shiftedJob10, NULL, job10DepIds, 2);
        size_t job11DepIds[] = {job7Id, job10Id};
        ZSTDMT_DepPool_add(threadPool, shiftedJob11, NULL, job11DepIds, 2);
        ZSTDMT_DepPool_wait(threadPool);
        ZSTDMT_DepPool_free(threadPool);
    }
    printf("\n");
    return 0;
}
