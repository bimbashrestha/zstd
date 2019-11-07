#include "datagen.h"
#include "zstd.h"
#include <stdlib.h>
#include <string.h>

#define ZSTD_TEST_MAIN
#include "zstd_test.h"

ZSTD_TEST(correctnessTests, roundTrip) {
  size_t srcSize = 5000;
  size_t dstSize = ZSTD_compressBound(srcSize);
  void *src = (void *)malloc(srcSize);
  char *dst = (void *)malloc(dstSize);
  void *reg = (void *)malloc(dstSize);
  size_t cSize;
  size_t dSize;

  RDG_genBuffer(src, srcSize, 0.5, 0., 0);

  cSize = ZSTD_compress(dst, dstSize, src, srcSize, 3);
  ASSERT_TRUE(!ZSTD_isError(cSize));

  dSize = ZSTD_decompress(reg, dstSize, dst, cSize);
  ASSERT_TRUE(!ZSTD_isError(dSize));

  ASSERT_TRUE(dSize == srcSize);
  ASSERT_TRUE(!memcmp(reg, src, dSize));

  free(src);
  free(dst);
  free(reg);
}

int main(void) { return ZSTD_TEST_main(); }
