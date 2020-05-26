CC = cc

ZSTDLIBDIR = ../../lib
PROGRAMDIR = ../../programs
COMMONDIR = ../../lib/common
COMPRESSDIR = ../../lib/compress
DECOMPRESSDIR = ../../lib/decompress

CFLAGS += -I$(ZSTDLIBDIR) -I$(PROGRAMDIR) -I$(COMMONDIR) -I$(COMPRESSDIR) -I$(DECOMPRESSDIR)
CFLAGS += -O3
CFLAGS += $(MOREFLAGS)
CFLAGS += -Wall -Wextra
OBJECTS = fse_compress.o fse_decompress.o huf_compress.o huf_decompress.o zstd_compress.o      \
	xxhash.o zstd_fast.o zstd_double_fast.o zstd_lazy.o zstd_opt.o zstd_ldm.o entropy_common.o \
	zstd_compress_sequences.o zstd_compress_literals.o zstdmt_compress.o error_private.o util.o\
	pool.o zstd_compress_superblock.o zstd_common.o debug.o hist.o zstdpatch.o

zstdpatch: main.c $(OBJECTS)
	$(CC) $(CFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $^ -o $@

%.o: $(COMMONDIR)/%.c
	$(CC) $(CFLAGS) -c $^ -o $@

%.o: $(COMPRESSDIR)/%.c
	$(CC) $(CFLAGS) -c $^ -o $@

%.o: $(PROGRAMDIR)/%.c
	$(CC) $(CFLAGS) -c $^ -o $@

%.o: $(DECOMPRESSDIR)/%.c
	$(CC) $(CFLAGS) -c $^ -o $@

clean:
	rm -rf *.o zstdpatch
