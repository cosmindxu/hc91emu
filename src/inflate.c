/* inflate.c — compact DEFLATE/zlib decompressor (stored, fixed and
 * dynamic Huffman blocks), in the style of Mark Adler's puff: canonical
 * Huffman codes are decoded directly from per-length symbol counts, no
 * lookup-table construction. Adler-32 trailer is not verified (the SZX
 * and RZX consumers validate sizes and content themselves).
 */
#include <stdlib.h>
#include <string.h>
#include "inflate.h"

#define MAXLBITS 15                 /* max code length in bits */
#define NLITSYMS 288
#define NDSTSYMS 30

typedef struct {
    const uint8_t *src;
    size_t len, pos;
    uint32_t bitbuf;
    int bitcnt;
    uint8_t *out;                   /* output buffer (may be growable) */
    size_t outcap, outpos;
    int grow;                       /* 1 = realloc out on demand */
    int err;
} Inf;

typedef struct {
    uint16_t count[MAXLBITS + 1];   /* codes per bit length */
    uint16_t symbol[NLITSYMS];      /* symbols ordered by code */
} Huff;

static int bits(Inf *s, int n)
{
    while (s->bitcnt < n) {
        if (s->pos >= s->len) { s->err = 1; return 0; }
        s->bitbuf |= (uint32_t)s->src[s->pos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    {
        int v = (int)(s->bitbuf & ((1u << n) - 1));
        s->bitbuf >>= n;
        s->bitcnt -= n;
        return v;
    }
}

static int putbyte(Inf *s, uint8_t b)
{
    if (s->outpos >= s->outcap) {
        if (!s->grow) { s->err = 1; return -1; }
        {
            size_t nc = s->outcap ? s->outcap * 2 : 4096;
            uint8_t *nb = realloc(s->out, nc);
            if (!nb) { s->err = 1; return -1; }
            s->out = nb;
            s->outcap = nc;
        }
    }
    s->out[s->outpos++] = b;
    return 0;
}

/* Build canonical-code tables from a code-length list; 0 = unused. */
static int huff_build(Huff *h, const uint8_t *lengths, int n)
{
    int sym, len, left;
    uint16_t offs[MAXLBITS + 1];

    memset(h->count, 0, sizeof h->count);
    for (sym = 0; sym < n; sym++)
        h->count[lengths[sym]]++;
    if (h->count[0] == n)
        return -1;                  /* no codes at all */

    left = 1;                       /* over-subscription check */
    for (len = 1; len <= MAXLBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0)
            return -1;
    }

    offs[1] = 0;
    for (len = 1; len < MAXLBITS; len++)
        offs[len + 1] = (uint16_t)(offs[len] + h->count[len]);
    for (sym = 0; sym < n; sym++)
        if (lengths[sym])
            h->symbol[offs[lengths[sym]]++] = (uint16_t)sym;
    return 0;
}

static int huff_decode(Inf *s, const Huff *h)
{
    int len, code = 0, first = 0, index = 0;
    for (len = 1; len <= MAXLBITS; len++) {
        int count;
        code |= bits(s, 1);
        if (s->err) return -1;
        count = h->count[len];
        if (code - first < count)
            return h->symbol[index + (code - first)];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    s->err = 1;
    return -1;
}

static const uint16_t lbase[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t lext[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const uint16_t dbase[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577
};
static const uint8_t dext[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static void block_codes(Inf *s, const Huff *lit, const Huff *dist)
{
    for (;;) {
        int sym = huff_decode(s, lit);
        if (s->err) return;
        if (sym < 256) {
            if (putbyte(s, (uint8_t)sym)) return;
        } else if (sym == 256) {
            return;                 /* end of block */
        } else {
            size_t length, distv, i;
            sym -= 257;
            if (sym >= 29) { s->err = 1; return; }
            length = lbase[sym] + (size_t)bits(s, lext[sym]);
            sym = huff_decode(s, dist);
            if (s->err) return;
            if (sym >= 30) { s->err = 1; return; }
            distv = dbase[sym] + (size_t)bits(s, dext[sym]);
            if (s->err) return;
            if (distv > s->outpos) { s->err = 1; return; }
            for (i = 0; i < length; i++) {
                uint8_t b = s->out[s->outpos - distv];
                if (putbyte(s, b)) return;
            }
        }
    }
}

static void block_fixed(Inf *s)
{
    static Huff lit, dist;
    static int ready = 0;
    if (!ready) {
        uint8_t lengths[NLITSYMS];
        int i;
        for (i = 0; i < 144; i++) lengths[i] = 8;
        for (; i < 256; i++) lengths[i] = 9;
        for (; i < 280; i++) lengths[i] = 7;
        for (; i < NLITSYMS; i++) lengths[i] = 8;
        huff_build(&lit, lengths, NLITSYMS);
        for (i = 0; i < NDSTSYMS; i++) lengths[i] = 5;
        huff_build(&dist, lengths, NDSTSYMS);
        ready = 1;
    }
    block_codes(s, &lit, &dist);
}

static void block_dynamic(Inf *s)
{
    static const uint8_t order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };
    uint8_t lengths[NLITSYMS + NDSTSYMS + 2];
    Huff lenc, lit, dist;
    int nlit, ndist, ncode, i, n;

    nlit = 257 + bits(s, 5);
    ndist = 1 + bits(s, 5);
    ncode = 4 + bits(s, 4);
    if (s->err || nlit > 286 || ndist > 30) { s->err = 1; return; }

    memset(lengths, 0, sizeof lengths);
    for (i = 0; i < ncode; i++)
        lengths[order[i]] = (uint8_t)bits(s, 3);
    for (; i < 19; i++)
        lengths[order[i]] = 0;
    if (huff_build(&lenc, lengths, 19)) { s->err = 1; return; }

    n = 0;
    while (n < nlit + ndist) {
        int sym = huff_decode(s, &lenc);
        if (s->err) return;
        if (sym < 16) {
            lengths[n++] = (uint8_t)sym;
        } else {
            int rep, val = 0;
            if (sym == 16) {
                if (n == 0) { s->err = 1; return; }
                val = lengths[n - 1];
                rep = 3 + bits(s, 2);
            } else if (sym == 17) {
                rep = 3 + bits(s, 3);
            } else {
                rep = 11 + bits(s, 7);
            }
            if (s->err || n + rep > nlit + ndist) { s->err = 1; return; }
            while (rep--)
                lengths[n++] = (uint8_t)val;
        }
    }
    if (lengths[256] == 0) { s->err = 1; return; }   /* need end code */
    if (huff_build(&lit, lengths, nlit)) { s->err = 1; return; }
    if (huff_build(&dist, lengths + nlit, ndist) &&
        ndist > 1) { s->err = 1; return; }
    block_codes(s, &lit, &dist);
}

static void block_stored(Inf *s)
{
    unsigned len, nlen;
    s->bitbuf = 0;
    s->bitcnt = 0;                  /* byte-align */
    if (s->pos + 4 > s->len) { s->err = 1; return; }
    len = s->src[s->pos] | ((unsigned)s->src[s->pos + 1] << 8);
    nlen = s->src[s->pos + 2] | ((unsigned)s->src[s->pos + 3] << 8);
    s->pos += 4;
    if ((len ^ 0xFFFF) != nlen || s->pos + len > s->len) {
        s->err = 1;
        return;
    }
    while (len--)
        if (putbyte(s, s->src[s->pos++]))
            return;
}

static int inflate_stream(Inf *s)
{
    int final;
    /* zlib header: CM=8, no preset dictionary */
    if (s->len < 2 || (s->src[0] & 0x0F) != 8 || (s->src[1] & 0x20))
        return -1;
    s->pos = 2;
    do {
        int type;
        final = bits(s, 1);
        type = bits(s, 2);
        if (s->err) return -1;
        if (type == 0)      block_stored(s);
        else if (type == 1) block_fixed(s);
        else if (type == 2) block_dynamic(s);
        else                s->err = 1;
        if (s->err) return -1;
    } while (!final);
    return 0;
}

long zlib_inflate(const uint8_t *src, size_t srclen,
                  uint8_t *dst, size_t dstlen)
{
    Inf s;
    memset(&s, 0, sizeof s);
    s.src = src;
    s.len = srclen;
    s.out = dst;
    s.outcap = dstlen;
    if (inflate_stream(&s) != 0)
        return -1;
    return (long)s.outpos;
}

uint8_t *zlib_inflate_alloc(const uint8_t *src, size_t srclen,
                            size_t *outlen)
{
    Inf s;
    memset(&s, 0, sizeof s);
    s.src = src;
    s.len = srclen;
    s.grow = 1;
    if (inflate_stream(&s) != 0) {
        free(s.out);
        return NULL;
    }
    if (!s.out)                     /* empty stream: still success */
        s.out = malloc(1);
    *outlen = s.outpos;
    return s.out;
}
