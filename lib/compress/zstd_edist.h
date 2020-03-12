#ifndef ZSTD_EDIST_H
#define ZSTD_EDIST_H

#include <stddef.h>
#include "zstd_internal.h"

size_t ZSTD_eDist_genSequences(ZSTD_Sequence* sequences, 
                        const void* dict, size_t dictSize,
                        const void* src, size_t srcSize);

#endif
