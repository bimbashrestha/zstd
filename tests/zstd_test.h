/* This is a super simple unit test framework for ZSTD
 * Example usage:
 *    #define ZSTD_TEST_MAIN
 *    #include "zstd_test.h"
 *    ZSTD_TEST(suiteName1, testName1) { ASSERT_TRUE(1 == 1); }
 *    ZSTD_TEST(suiteName1, testName2) { ASSERT_TRUE(1 == 2); }
 *    ZSTD_TEST(suiteName2, testName1) { ASSERT_TRUE(1 == 3); }
 *    int main(void) { return ZSTD_TEST_main(); } */
#ifndef ZSTD_TEST_H
#define ZSTD_TEST_H

#include <setjmp.h>
#include <stdio.h>

/* Using this when an assert fails to keep processing subsequent tests and
 * not just abort like fuzzer.c. We abort at the end when there is a assert
 * failure. */
jmp_buf jmpBuf;

/* Jump back to loop in ZSTD_TEST_main to keep processing more tests */
#define ASSERT_TRUE(cond) (cond) ? (void)0 : longjmp(jmpBuf, 1)

/* Struct for unit test. Just has a callback function.
 * TODO: Add data to this so that function can be passed arguments */
struct ZSTD_Test {
  const char *suite;
  const char *test;
  void (*testFn)(void);
  unsigned int sentinal;
};

/*-************************************
 *  ZSTD_TEST macros
 **************************************/

#define ZSTD_TEST_NAME(name) ZSTD_TEST_##name
#define ZSTD_TEST_FNAME(suiteName, testName)                                   \
  ZSTD_TEST_NAME(suiteName##_##testName##_testFn)
#define ZSTD_TEST_TNAME(suiteName, testName)                                   \
  ZSTD_TEST_NAME(suiteName##_##testName)

/* Magic value we use to iterate through the tests */
#define ZSTD_TEST_SENTINAL 11111111

#if defined(__APPLE__)
#define ZSTD_TEST_SECTION                                                      \
  __attribute__((used, section("__DATA, .ZSTD_Test"), aligned(1)))
#elif defined(_MSC_VER)
#pragma data_seg(push)
#pragma data_seg(".ZSTD_Test$u")
#pragma data_seg(pop)
#define CTEST_IMPL_SECTION                                                     \
  __declspec(allocate(".ZSTD_Test$u")) __declspec(align(1))
#else
#define ZSTD_TEST_SECTION                                                      \
  __attribute__((used, section(".ZSTD_Test"), aligned(1)))
#endif

#define ZSTD_TEST_STRUCT(suiteName, testName)                                  \
  static struct ZSTD_Test ZSTD_TEST_SECTION ZSTD_TEST_TNAME(                   \
      suiteName, testName) = {.suite = #suiteName,                             \
                              .test = #testName,                               \
                              .testFn = ZSTD_TEST_FNAME(suiteName, testName),  \
                              .sentinal = ZSTD_TEST_SENTINAL}

/* ZSTD_TEST
 * @arg suiteName: the name of the test suite (ie. test category)
 * @arg testName: the name of the test */
#define ZSTD_TEST(suiteName, testName)                                         \
  static void ZSTD_TEST_FNAME(suiteName, testName)(void);                      \
  ZSTD_TEST_STRUCT(suiteName, testName);                                       \
  static void ZSTD_TEST_FNAME(suiteName, testName)(void)

/* Note: This needs to be defined for the unit test framework to work! */
#ifdef ZSTD_TEST_MAIN

/* Dummy test so that we have a function ptr to start iterating from */
ZSTD_TEST(dummySuiteName, dummyTestName) {}

/* Call this method from main() in the file where tests are declared */
static int ZSTD_TEST_main(void) {
  /* These are static because otherwise setjmp() might clobber */
  static struct ZSTD_Test *test;
  static struct ZSTD_Test *start =
      &ZSTD_TEST_TNAME(dummySuiteName, dummyTestName);
  static struct ZSTD_Test *end =
      &ZSTD_TEST_TNAME(dummySuiteName, dummyTestName);
  static size_t idx = 1;
  static size_t total = 0;
  static size_t nbPassed = 0;

  /* Find the starting test */
  while (1) {
    struct ZSTD_Test *t = start - 1;
    if (t->sentinal != ZSTD_TEST_SENTINAL)
      break;
    start--;
  }

  /* Find the ending test */
  while (1) {
    struct ZSTD_Test *t = end + 1;
    if (t->sentinal != ZSTD_TEST_SENTINAL)
      break;
    end++;
  }
  end++;

  /* Count the total number of tests */
  for (test = start; test != end; test++)
    total++;
  total--;

  /* Run each of the tests */
  for (test = start; test != end; test++) {
    if (test == &ZSTD_TEST_TNAME(dummySuiteName, dummyTestName))
      continue;
    printf("[TEST %lu/%lu %s:%s] ", idx, total, test->suite, test->test);
    if (!setjmp(jmpBuf)) {
      /* Test passed */
      test->testFn();
      printf("\xE2\x9C\x93\n");
      nbPassed++;
    } else {
      /* Test failed */
      printf("x\n");
    }
    idx++;
  }
  printf("%lu PASSED %lu FAILED\n", nbPassed, total - nbPassed);

  /* Abort at the end when there is at least one assert failure */
  if (nbPassed != total)
    abort();
  return 0;
}

#endif /* ZSTD_TEST_MAIN */

#endif /* ZSTD_TEST_H */
