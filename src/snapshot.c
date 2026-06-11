/* snapshot.c — .sna (48K) and .z80 (v1/v2/v3, 48K) snapshot load + save,
 * .scr screen import/export. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "machine.h"

static uint8_t *read_whole_file(const char *path, long *out_size)
{
    FILE *f = fopen(path, "rb");
    long size;
    uint8_t *buf;

    if (!f) {
        fprintf(stderr, "error: cannot open snapshot '%s'\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fprintf(stderr, "error: snapshot '%s' is empty\n", path);
        fclose(f);
        return NULL;
    }
    buf = malloc((size_t)size);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fprintf(stderr, "error: cannot read snapshot '%s'\n", path);
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = size;
    return buf;
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

/* PC to store in a snapshot: if the CPU is inside HALT, point back at the
 * HALT opcode so the wait resumes on load (neither format has a halted
 * flag). */
static uint16_t snap_pc(const Z80 *z)
{
    return z->halted ? (uint16_t)(z->pc.w - 1) : z->pc.w;
}

/* .sna/.z80 cannot represent the HC-91 CP/M paging state. */
static void warn_if_paged(const Machine *m, const char *path)
{
    if (m->ram_paged)
        fprintf(stderr, "warning: '%s': CP/M RAM is paged in; snapshot "
                "stores the 48K view only\n", path);
}

static int write_file(const char *path, const uint8_t *buf, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(buf, 1, n, f) != n) {
        fprintf(stderr, "error: cannot write '%s'\n", path);
        if (f) fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

/* ---- .sna 48K ---- */

int snapshot_load_sna(Machine *m, const char *path)
{
    long size;
    uint8_t *buf = read_whole_file(path, &size);
    Z80 *z = &m->cpu;
    uint8_t iff;

    if (!buf)
        return -1;
    if (size != 49179) {
        fprintf(stderr, "error: '%s': bad .sna size %ld (48K .sna is 49179)\n",
                path, size);
        free(buf);
        return -1;
    }

    z80_reset(z);

    z->i = buf[0];
    z->hl_.w = rd16(buf + 1);
    z->de_.w = rd16(buf + 3);
    z->bc_.w = rd16(buf + 5);
    z->af_.w = rd16(buf + 7);
    z->hl.w = rd16(buf + 9);
    z->de.w = rd16(buf + 11);
    z->bc.w = rd16(buf + 13);
    z->iy.w = rd16(buf + 15);
    z->ix.w = rd16(buf + 17);
    iff = (buf[19] >> 2) & 1;
    z->iff1 = z->iff2 = iff;
    z->r = buf[20];
    z->af.w = rd16(buf + 21);
    z->sp.w = rd16(buf + 23);
    z->im = buf[25] & 3;
    m->border = buf[26] & 7;

    memcpy(m->mem + 0x4000, buf + 27, 49152);

    /* PC = pop from stack */
    z->pc.w = (uint16_t)(m->mem[z->sp.w] | (m->mem[(uint16_t)(z->sp.w + 1)] << 8));
    z->sp.w = (uint16_t)(z->sp.w + 2);
    z->memptr.w = z->pc.w;
    z->halted = 0;
    z->ei_pending = 0;

    free(buf);
    return 0;
}

int snapshot_save_sna(const Machine *m, const char *path)
{
    const Z80 *z = &m->cpu;
    static uint8_t out[27 + 49152];
    uint16_t pc = snap_pc(z);
    uint16_t sp = (uint16_t)(z->sp.w - 2);

    warn_if_paged(m, path);
    if (sp < 0x4000 || sp == 0xFFFF) {
        fprintf(stderr, "error: cannot save .sna: pushing PC at SP=%04X "
                "would hit ROM\n", z->sp.w);
        return -1;
    }
    out[0] = z->i;
    wr16(out + 1, z->hl_.w);
    wr16(out + 3, z->de_.w);
    wr16(out + 5, z->bc_.w);
    wr16(out + 7, z->af_.w);
    wr16(out + 9, z->hl.w);
    wr16(out + 11, z->de.w);
    wr16(out + 13, z->bc.w);
    wr16(out + 15, z->iy.w);
    wr16(out + 17, z->ix.w);
    out[19] = z->iff2 ? 4 : 0;
    out[20] = z->r;
    wr16(out + 21, z->af.w);
    wr16(out + 23, sp);
    out[25] = z->im;
    out[26] = m->border & 7;
    memcpy(out + 27, m->mem + 0x4000, 49152);
    out[27 + (sp - 0x4000)] = (uint8_t)(pc & 0xFF);
    out[27 + (sp + 1 - 0x4000)] = (uint8_t)(pc >> 8);
    return write_file(path, out, sizeof out);
}

/* ---- .z80 ---- */

/* RLE decompress 'src[0..srclen)' into mem starting at dst_base, writing at
 * most dstmax bytes. Scheme: ED ED count value. Returns bytes consumed. */
static long z80_unrle(Machine *m, const uint8_t *src, long srclen,
                      uint32_t dst_base, uint32_t dstmax, int allow_term)
{
    long i = 0;
    uint32_t out = 0;

    while (i < srclen && out < dstmax) {
        /* optional v1 terminator 00 ED ED 00 */
        if (allow_term && i + 3 < srclen && src[i] == 0x00 &&
            src[i + 1] == 0xED && src[i + 2] == 0xED && src[i + 3] == 0x00) {
            i += 4;
            break;
        }
        if (i + 3 < srclen && src[i] == 0xED && src[i + 1] == 0xED) {
            uint32_t count = src[i + 2];
            uint8_t val = src[i + 3];
            uint32_t k;
            i += 4;
            for (k = 0; k < count && out < dstmax; k++) {
                uint32_t a = dst_base + out++;
                if (a >= 0x4000 && a <= 0xFFFF)
                    m->mem[a] = val;
            }
        } else {
            uint32_t a = dst_base + out++;
            if (a >= 0x4000 && a <= 0xFFFF)
                m->mem[a] = src[i];
            i++;
        }
    }
    return i;
}

int snapshot_load_z80(Machine *m, const char *path)
{
    long size;
    uint8_t *buf = read_whole_file(path, &size);
    Z80 *z = &m->cpu;
    uint8_t b12;
    uint16_t pc;

    if (!buf)
        return -1;
    if (size < 30) {
        fprintf(stderr, "error: '%s': too short for a .z80 file\n", path);
        free(buf);
        return -1;
    }

    z80_reset(z);

    z->af.b.h = buf[0];
    z->af.b.l = buf[1];
    z->bc.w = rd16(buf + 2);
    z->hl.w = rd16(buf + 4);
    pc = rd16(buf + 6);
    z->sp.w = rd16(buf + 8);
    z->i = buf[10];
    b12 = buf[12];
    if (b12 == 0xFF)
        b12 = 1;
    z->r = (uint8_t)((buf[11] & 0x7F) | ((b12 & 1) << 7));
    m->border = (b12 >> 1) & 7;
    z->de.w = rd16(buf + 13);
    z->bc_.w = rd16(buf + 15);
    z->de_.w = rd16(buf + 17);
    z->hl_.w = rd16(buf + 19);
    z->af_.b.h = buf[21];
    z->af_.b.l = buf[22];
    z->iy.w = rd16(buf + 23);
    z->ix.w = rd16(buf + 25);
    z->iff1 = buf[27] ? 1 : 0;
    z->iff2 = buf[28] ? 1 : 0;
    z->im = buf[29] & 3;

    if (pc != 0) {
        /* v1: 48K image at 0x4000, optionally RLE-compressed (b12 bit 5) */
        if (b12 & 0x20)
            z80_unrle(m, buf + 30, size - 30, 0x4000, 49152, 1);
        else {
            long n = size - 30;
            if (n > 49152)
                n = 49152;
            memcpy(m->mem + 0x4000, buf + 30, (size_t)n);
        }
    } else {
        /* v2/v3 */
        uint16_t ehlen;
        long off;
        uint8_t hwmode;
        int v3;

        if (size < 32 + 2) {
            fprintf(stderr, "error: '%s': truncated .z80 v2+ header\n", path);
            free(buf);
            return -1;
        }
        ehlen = rd16(buf + 30);
        v3 = (ehlen == 54 || ehlen == 55);
        if (ehlen != 23 && !v3)
            fprintf(stderr, "warning: '%s': unusual .z80 extra header length %u\n",
                    path, ehlen);
        if (size < 30 + 2 + (long)ehlen) {
            fprintf(stderr, "error: '%s': truncated .z80 v2+ header\n", path);
            free(buf);
            return -1;
        }
        pc = rd16(buf + 32);
        hwmode = buf[34];
        if (!((hwmode == 0 || hwmode == 1) || (v3 && hwmode == 3)))
            fprintf(stderr, "warning: '%s': hardware mode %u is not 48K; "
                    "loading anyway\n", path, hwmode);

        off = 30 + 2 + (long)ehlen;
        while (off + 3 <= size) {
            uint16_t complen = rd16(buf + off);
            uint8_t page = buf[off + 2];
            uint32_t base;
            long avail;

            off += 3;
            switch (page) {
            case 8: base = 0x4000; break;
            case 4: base = 0x8000; break;
            case 5: base = 0xC000; break;
            default:
                base = 0;
                fprintf(stderr, "warning: '%s': skipping page %u (not 48K)\n",
                        path, page);
                break;
            }
            avail = size - off;
            if (complen == 0xFFFF) {
                long n = avail < 16384 ? avail : 16384;
                if (base)
                    memcpy(m->mem + base, buf + off, (size_t)n);
                off += 16384;
            } else {
                if ((long)complen > avail) {
                    fprintf(stderr, "warning: '%s': truncated page data\n",
                            path);
                    complen = (uint16_t)avail;
                }
                if (base)
                    z80_unrle(m, buf + off, complen, base, 16384, 0);
                off += complen;
            }
        }
    }

    z->pc.w = pc;
    z->memptr.w = pc;
    z->halted = 0;
    z->ei_pending = 0;

    free(buf);
    return 0;
}

int snapshot_save_z80(const Machine *m, const char *path)
{
    /* v2 format: 30-byte v1 header with PC=0, 23-byte extension carrying
     * the real PC + hardware mode 0 (48K), then three uncompressed 16K
     * pages (length marker 0xFFFF). */
    const Z80 *z = &m->cpu;
    static uint8_t out[30 + 2 + 23 + 3 * (3 + 16384)];
    static const struct { uint8_t page; uint32_t base; } pages[] = {
        { 8, 0x4000 }, { 4, 0x8000 }, { 5, 0xC000 }
    };
    uint8_t *p = out;
    int i;

    warn_if_paged(m, path);
    memset(out, 0, 30 + 2 + 23);
    p[0] = z->af.b.h;
    p[1] = z->af.b.l;
    wr16(p + 2, z->bc.w);
    wr16(p + 4, z->hl.w);
    /* p[6..7] = 0 marks v2+ */
    wr16(p + 8, z->sp.w);
    p[10] = z->i;
    p[11] = (uint8_t)(z->r & 0x7F);
    p[12] = (uint8_t)((z->r >> 7) | ((m->border & 7) << 1));
    wr16(p + 13, z->de.w);
    wr16(p + 15, z->bc_.w);
    wr16(p + 17, z->de_.w);
    wr16(p + 19, z->hl_.w);
    p[21] = z->af_.b.h;
    p[22] = z->af_.b.l;
    wr16(p + 23, z->iy.w);
    wr16(p + 25, z->ix.w);
    p[27] = z->iff1;
    p[28] = z->iff2;
    p[29] = z->im & 3;
    wr16(p + 30, 23);                /* extension length -> v2 */
    wr16(p + 32, snap_pc(z));
    p += 30 + 2 + 23;

    for (i = 0; i < 3; i++) {
        wr16(p, 0xFFFF);             /* uncompressed page */
        p[2] = pages[i].page;
        memcpy(p + 3, m->mem + pages[i].base, 16384);
        p += 3 + 16384;
    }
    return write_file(path, out, sizeof out);
}

/* ---- .scr (raw 6912-byte screen) ---- */

int screen_save_scr(const Machine *m, const char *path)
{
    return write_file(path, m->mem + 0x4000, 6912);
}

int screen_load_scr(Machine *m, const char *path)
{
    long size;
    uint8_t *buf = read_whole_file(path, &size);

    if (!buf)
        return -1;
    if (size != 6912) {
        fprintf(stderr, "error: '%s': bad .scr size %ld (expected 6912)\n",
                path, size);
        free(buf);
        return -1;
    }
    memcpy(m->mem + 0x4000, buf, 6912);
    free(buf);
    /* park the CPU (DI + HALT) so the ROM never wipes the screen */
    m->cpu.iff1 = m->cpu.iff2 = 0;
    m->cpu.halted = 1;
    return 0;
}
