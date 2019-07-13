source ../../../emsdk/emsdk_env.sh
emcc -s ERROR_ON_UNDEFINED_SYMBOLS=0 -s SIDE_MODULE=1 -O1 --profiling-funcs -I ./zlib/ -I ../../source/ ./zlib/compress.c ./zlib/deflate.c ./zlib/crc32.c ./zlib/adler32.c \
	./zlib/trees.c ./zlib/zutil.c  test.zlib.c -o zlib.wasm
