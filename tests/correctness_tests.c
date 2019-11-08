#include "datagen.h"
#include "zstd.h"
#include "zstd_test.h"
#include <stdlib.h>
#include <string.h>

static size_t srcSize;
static size_t dstSize;
static void *src;
static void *dst;
static void *reg;

void test_setup(void) {}
void test_teardown(void) {}

MU_TEST(roundTrip) {
  size_t srcSize = 5000;
  size_t dstSize = ZSTD_compressBound(srcSize);
  void *src = (void *)malloc(srcSize);
  char *dst = (void *)malloc(dstSize);
  void *reg = (void *)malloc(dstSize);
  size_t cSize;
  size_t dSize;

  RDG_genBuffer(src, srcSize, 0.5, 0., 0);

  cSize = ZSTD_compress(dst, dstSize, src, srcSize, 3);
  mu_assert(!ZSTD_isError(cSize), "Compression failed");

  dSize = ZSTD_decompress(reg, dstSize, dst, cSize);
  mu_assert(!ZSTD_isError(dSize), "Decompression failed");

  mu_assert(dSize == srcSize, "decompressed size doesn't match original");
  mu_assert(!memcmp(reg, src, dSize), "decompressed doesn't match original");

  free(src);
  free(dst);
  free(reg);
}

MU_TEST_SUITE(compressionTests) {
  MU_SUITE_CONFIGURE(&test_setup, &test_teardown);
  MU_RUN_TEST(roundTrip);
}

int main(void) {
  MU_RUN_SUITE(compressionTests);
  MU_REPORT();
}
