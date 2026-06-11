/* tap2tzx — convert a .tap to a .tzx that exercises the TZX block zoo:
 * text/archive-info blocks (skipped), a loop around a pure tone, a pulse
 * sequence, a group, and every data block in one of three encodings —
 * all with standard ROM timings, so the ROM loader still reads them:
 *   (default)  TURBO 0x11 blocks
 *   gdb        generalized data 0x19 blocks (symbol tables + bit stream;
 *              the sync symbol uses the force-high polarity flag)
 *   csw        CSW recording 0x18 blocks at 3.5 MHz sample rate
 *              (alternating RLE and Z-RLE/zlib compression)
 *
 * usage: tap2tzx IN.tap OUT.tzx [gdb|csw]
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static void w8(FILE *f, unsigned v)  { fputc((int)(v & 0xFF), f); }
static void w16(FILE *f, unsigned v) { w8(f, v); w8(f, v >> 8); }
static void w24(FILE *f, unsigned v) { w8(f, v); w8(f, v >> 8); w8(f, v >> 16); }
static void w32(FILE *f, unsigned v) { w16(f, v); w16(f, v >> 16); }

/* ---- CSW helpers ---- */

/* Standard-timing pulse list for one tap block (durations in T-states,
 * which is also the CSW sample count at a 3.5 MHz sample rate). */
static size_t block_pulses(const uint8_t *data, unsigned len,
                           uint32_t *out, size_t max)
{
    size_t n = 0;
    unsigned i, pcnt = (len && data[0] < 128) ? 8063 : 3223;
    int b;
    for (i = 0; i < pcnt && n < max; i++) out[n++] = 2168;
    if (n + 2 <= max) { out[n++] = 667; out[n++] = 735; }
    for (i = 0; i < len; i++)
        for (b = 0; b < 8 && n + 2 <= max; b++) {
            uint32_t t = (data[i] & (0x80u >> b)) ? 1710 : 855;
            out[n++] = t;
            out[n++] = t;
        }
    return n;
}

/* CSW RLE: byte = samples, 0 = u32 follows. */
static size_t csw_rle(const uint32_t *p, size_t n, uint8_t *out)
{
    size_t i, o = 0;
    for (i = 0; i < n; i++) {
        if (p[i] && p[i] < 256) {
            out[o++] = (uint8_t)p[i];
        } else {
            out[o++] = 0;
            out[o++] = (uint8_t)p[i];
            out[o++] = (uint8_t)(p[i] >> 8);
            out[o++] = (uint8_t)(p[i] >> 16);
            out[o++] = (uint8_t)(p[i] >> 24);
        }
    }
    return o;
}

/* Wrap a buffer in a zlib stream of stored (uncompressed) DEFLATE
 * blocks + adler32 — enough to exercise a real Z-RLE decode path. */
static size_t zlib_store(const uint8_t *src, size_t n, uint8_t *out)
{
    size_t o = 0, i = 0;
    uint32_t a = 1, b = 0;
    out[o++] = 0x78;
    out[o++] = 0x01;
    do {
        size_t chunk = n - i > 65535 ? 65535 : n - i;
        out[o++] = (i + chunk == n) ? 1 : 0;     /* BFINAL + stored */
        out[o++] = (uint8_t)chunk;
        out[o++] = (uint8_t)(chunk >> 8);
        out[o++] = (uint8_t)~chunk;
        out[o++] = (uint8_t)(~chunk >> 8);
        memcpy(out + o, src + i, chunk);
        o += chunk;
        i += chunk;
    } while (i < n);
    for (i = 0; i < n; i++) {                    /* adler32 */
        a = (a + src[i]) % 65521;
        b = (b + a) % 65521;
    }
    out[o++] = (uint8_t)(b >> 8);
    out[o++] = (uint8_t)b;
    out[o++] = (uint8_t)(a >> 8);
    out[o++] = (uint8_t)a;
    return o;
}

int main(int argc, char **argv)
{
    static const char text[] = "hc91emu TZX parser test";
    static uint8_t buf[65536];
    static uint32_t pulses[600000];
    static uint8_t rle[3000000], zl[3000100];
    FILE *in, *out;
    size_t size;
    long off = 0;
    int mode = 0, blkno = 0;                 /* 0=turbo 1=gdb 2=csw */

    if (argc == 4 && !strcmp(argv[3], "gdb")) mode = 1;
    else if (argc == 4 && !strcmp(argv[3], "csw")) mode = 2;
    else if (argc != 3) {
        fprintf(stderr, "usage: %s IN.tap OUT.tzx [gdb|csw]\n", argv[0]);
        return 1;
    }
    in = fopen(argv[1], "rb");
    if (!in) {
        fprintf(stderr, "error: cannot open '%s'\n", argv[1]);
        return 1;
    }
    size = fread(buf, 1, sizeof buf, in);
    fclose(in);
    out = fopen(argv[2], "wb");
    if (!out) {
        fprintf(stderr, "error: cannot write '%s'\n", argv[2]);
        return 1;
    }

    fwrite("ZXTape!\x1A", 1, 8, out);
    w8(out, 1); w8(out, 20);                  /* rev 1.20 */

    w8(out, 0x30);                            /* text description */
    w8(out, (unsigned)strlen(text));
    fwrite(text, 1, strlen(text), out);

    w8(out, 0x32);                            /* archive info (skipped) */
    w16(out, 5);
    w8(out, 1);                               /* 1 string */
    w8(out, 0x00); w8(out, 2);                /* title, 2 chars */
    fwrite("ok", 1, 2, out);

    w8(out, 0x24); w16(out, 2);               /* loop x2 ... */
    w8(out, 0x12); w16(out, 600); w16(out, 50);   /* pure tone */
    w8(out, 0x25);                            /* ... end loop */

    w8(out, 0x13); w8(out, 2);                /* pulse sequence */
    w16(out, 400); w16(out, 400);

    w8(out, 0x21); w8(out, 4);                /* group "data" */
    fwrite("data", 1, 4, out);

    while (off + 2 <= (long)size) {
        unsigned len = (unsigned)(buf[off] | (buf[off + 1] << 8));
        off += 2;
        if (off + (long)len > (long)size)
            break;
        if (mode == 1) {                      /* generalized data 0x19 */
            unsigned pcnt = (len && buf[off] < 128) ? 8063 : 3223;
            w8(out, 0x19);
            w32(out, 14 + 2 * 5 + 2 * 3 + 2 * 5 + len);
            w16(out, 1000);                   /* pause ms */
            w32(out, 2);                      /* TOTP: 2 PRLE entries */
            w8(out, 2);                       /* NPP */
            w8(out, 2);                       /* ASP */
            w32(out, len * 8);                /* TOTD */
            w8(out, 2);                       /* NPD */
            w8(out, 2);                       /* ASD */
            /* pilot alphabet: sym0 = pilot pulse, sym1 = the two syncs
             * with the force-high polarity flag (exercises flag 3) */
            w8(out, 0); w16(out, 2168); w16(out, 0);
            w8(out, 3); w16(out, 667);  w16(out, 735);
            w8(out, 0); w16(out, pcnt);       /* PRLE: sym0 x pcnt */
            w8(out, 1); w16(out, 1);          /*       sym1 x 1 */
            /* data alphabet: bit0/bit1 = two equal pulses */
            w8(out, 0); w16(out, 855);  w16(out, 855);
            w8(out, 0); w16(out, 1710); w16(out, 1710);
            fwrite(buf + off, 1, len, out);   /* 1-bit syms = the bytes */
        } else if (mode == 2) {               /* CSW recording 0x18 */
            size_t np = block_pulses(buf + off, len, pulses,
                                     sizeof pulses / sizeof pulses[0]);
            size_t rn = csw_rle(pulses, np, rle);
            int zrle = (blkno & 1);           /* alternate RLE / Z-RLE */
            size_t dn = zrle ? zlib_store(rle, rn, zl) : rn;
            const uint8_t *d = zrle ? zl : rle;
            w8(out, 0x18);
            w32(out, (unsigned)(10 + dn));
            w16(out, 1000);                   /* pause ms */
            w24(out, 3500000);                /* sample rate = T rate */
            w8(out, zrle ? 2 : 1);            /* compression */
            w32(out, (unsigned)np);
            fwrite(d, 1, dn, out);
        } else {
            w8(out, 0x11);                    /* turbo data block */
            w16(out, 2168);                   /* pilot */
            w16(out, 667); w16(out, 735);     /* sync */
            w16(out, 855); w16(out, 1710);    /* bit 0 / bit 1 */
            w16(out, (len && buf[off] < 128) ? 8063 : 3223);
            w8(out, 8);                       /* used bits in last byte */
            w16(out, 1000);                   /* pause ms */
            w24(out, len);
            fwrite(buf + off, 1, len, out);
        }
        blkno++;
        off += len;
    }

    w8(out, 0x22);                            /* group end */
    fclose(out);
    return 0;
}
