/* inflate.h — minimal zlib-stream (RFC1950/1951) decompressor, used for
 * SZX RAMP pages and RZX input blocks written by other emulators. */
#ifndef HC91_INFLATE_H
#define HC91_INFLATE_H

#include <stdint.h>
#include <stddef.h>

/* Inflate a zlib stream into dst (exactly dstlen expected). Returns the
 * number of bytes produced (== dstlen on success) or -1 on error. */
long zlib_inflate(const uint8_t *src, size_t srclen,
                  uint8_t *dst, size_t dstlen);

/* Same, but allocates the output (geometric growth; caller free()s).
 * Returns NULL on error; *outlen receives the produced size. */
uint8_t *zlib_inflate_alloc(const uint8_t *src, size_t srclen,
                            size_t *outlen);

#endif
