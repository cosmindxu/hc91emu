/* png.c — minimal PNG writer: 8-bit RGBA, zlib stream with STORED
 * (uncompressed) deflate blocks. No external libraries. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ---- CRC-32 (polynomial 0xEDB88320) ---- */
static uint32_t crc_table[256];
static int crc_table_ready = 0;

static void crc_init(void)
{
    uint32_t c;
    int n, k;
    for (n = 0; n < 256; n++) {
        c = (uint32_t)n;
        for (k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc_table[n] = c;
    }
    crc_table_ready = 1;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *buf, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++)
        crc = crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

/* ---- Adler-32 ---- */
static uint32_t adler32(const uint8_t *buf, size_t len)
{
    uint32_t a = 1, b = 0;
    size_t i;
    for (i = 0; i < len; i++) {
        a = (a + buf[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static int write_chunk(FILE *f, const char *type, const uint8_t *data,
                       uint32_t len)
{
    uint8_t hdr[8], crcb[4];
    uint32_t crc;

    put_be32(hdr, len);
    memcpy(hdr + 4, type, 4);
    crc = crc32_update(0xFFFFFFFFu, hdr + 4, 4);
    if (len)
        crc = crc32_update(crc, data, len);
    put_be32(crcb, crc ^ 0xFFFFFFFFu);

    if (fwrite(hdr, 1, 8, f) != 8)
        return -1;
    if (len && fwrite(data, 1, len, f) != len)
        return -1;
    if (fwrite(crcb, 1, 4, f) != 4)
        return -1;
    return 0;
}

int png_write(const char *path, const uint32_t *rgba, int w, int h)
{
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    FILE *f;
    uint8_t ihdr[13];
    uint8_t *raw, *idat;
    size_t rawlen, idatlen, off, pos;
    int y, rc = -1;

    if (!crc_table_ready)
        crc_init();
    if (w <= 0 || h <= 0)
        return -1;

    /* Raw scanlines: filter byte 0 + w*4 bytes per line. */
    rawlen = (size_t)h * (1 + (size_t)w * 4);
    raw = malloc(rawlen);
    if (!raw)
        return -1;
    pos = 0;
    for (y = 0; y < h; y++) {
        raw[pos++] = 0; /* filter: none */
        memcpy(raw + pos, rgba + (size_t)y * w, (size_t)w * 4);
        pos += (size_t)w * 4;
    }

    /* zlib stream: 2-byte header + stored blocks (5-byte hdr each,
     * max 65535 payload) + 4-byte adler32. */
    {
        size_t nblocks = (rawlen + 65534) / 65535;
        idatlen = 2 + rawlen + nblocks * 5 + 4;
    }
    idat = malloc(idatlen);
    if (!idat) {
        free(raw);
        return -1;
    }
    pos = 0;
    idat[pos++] = 0x78;
    idat[pos++] = 0x01;
    off = 0;
    while (off < rawlen) {
        size_t blk = rawlen - off;
        int final;
        if (blk > 65535)
            blk = 65535;
        final = (off + blk == rawlen);
        idat[pos++] = final ? 1 : 0;
        idat[pos++] = (uint8_t)(blk & 0xFF);
        idat[pos++] = (uint8_t)(blk >> 8);
        idat[pos++] = (uint8_t)(~blk & 0xFF);
        idat[pos++] = (uint8_t)((~blk >> 8) & 0xFF);
        memcpy(idat + pos, raw + off, blk);
        pos += blk;
        off += blk;
    }
    put_be32(idat + pos, adler32(raw, rawlen));
    pos += 4;

    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "error: cannot create '%s'\n", path);
        free(raw);
        free(idat);
        return -1;
    }

    put_be32(ihdr, (uint32_t)w);
    put_be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;   /* bit depth */
    ihdr[9] = 6;   /* color type: RGBA */
    ihdr[10] = 0;  /* compression */
    ihdr[11] = 0;  /* filter */
    ihdr[12] = 0;  /* interlace */

    if (fwrite(sig, 1, 8, f) == 8 &&
        write_chunk(f, "IHDR", ihdr, 13) == 0 &&
        write_chunk(f, "IDAT", idat, (uint32_t)pos) == 0 &&
        write_chunk(f, "IEND", NULL, 0) == 0)
        rc = 0;

    if (fclose(f) != 0)
        rc = -1;
    free(raw);
    free(idat);
    return rc;
}
