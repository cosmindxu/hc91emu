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

/* model-aware memory placement helpers (defined with the .z80 code) */
static void scatter48(Machine *m, const uint8_t *img);
static const uint8_t *view_ptr(const Machine *m, int region);

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

    scatter48(m, buf + 27);

    /* PC = pop from stack */
    z->pc.w = (uint16_t)(machine_peek(m, z->sp.w)
                         | (machine_peek(m, (uint16_t)(z->sp.w + 1)) << 8));
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
    if (m->model == HC91_MODEL_128)
        fprintf(stderr, "warning: '%s': .sna stores the current 48K view "
                "only (bank latch %02X not representable); prefer "
                "--save-z80/--save-szx\n", path, m->port_7ffd);
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
    memcpy(out + 27, view_ptr(m, 1), 16384);
    memcpy(out + 27 + 16384, view_ptr(m, 2), 16384);
    memcpy(out + 27 + 32768, view_ptr(m, 3), 16384);
    out[27 + (sp - 0x4000)] = (uint8_t)(pc & 0xFF);
    out[27 + (sp + 1 - 0x4000)] = (uint8_t)(pc >> 8);
    return write_file(path, out, sizeof out);
}

/* ---- .z80 ---- */

/* RLE decompress 'src[0..srclen)' into mem starting at dst_base, writing at
 * most dstmax bytes. Scheme: ED ED count value. Returns bytes consumed. */
static long z80_unrle(const uint8_t *src, long srclen,
                      uint8_t *dst, uint32_t dstmax, int allow_term)
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
            for (k = 0; k < count && out < dstmax; k++)
                dst[out++] = val;
        } else {
            dst[out++] = src[i++];
        }
    }
    return i;
}

/* Place a flat 48K image (0x4000-0xFFFF) into the machine. On the
 * HC-128 it lands in banks 5/2/0 with the bank latch locked to the
 * normal 48K view (USR0-style), so 48K snapshots run there too. */
static void scatter48(Machine *m, const uint8_t *img)
{
    if (m->model == HC91_MODEL_128) {
        memcpy(m->ram128[5], img, 16384);
        memcpy(m->ram128[2], img + 16384, 16384);
        memcpy(m->ram128[0], img + 32768, 16384);
        m->port_7ffd = 0x30;            /* bank 0, screen 5, locked */
        m->screen = m->ram128[5];
    } else {
        memcpy(m->mem + 0x4000, img, 49152);
    }
}

/* Current CPU-visible 16K region (1..3) regardless of model. */
static const uint8_t *view_ptr(const Machine *m, int region)
{
    if (m->model == HC91_MODEL_128) {
        if (region == 1) return m->ram128[5];
        if (region == 2) return m->ram128[2];
        return m->ram128[m->port_7ffd & 7];
    }
    return m->mem + region * 0x4000;
}

/* Destination for a .z80 page number (NULL = skip). file128 selects the
 * 128K page numbering (3-10 = banks 0-7) vs the 48K one. */
static uint8_t *z80_page_ptr(Machine *m, uint8_t page, int file128)
{
    if (m->model == HC91_MODEL_128) {
        if (file128)
            return (page >= 3 && page <= 10) ? m->ram128[page - 3] : NULL;
        switch (page) {                 /* 48K file -> USR0-style banks */
        case 8: return m->ram128[5];
        case 4: return m->ram128[2];
        case 5: return m->ram128[0];
        }
        return NULL;
    }
    switch (page) {
    case 8: return m->mem + 0x4000;
    case 4: return m->mem + 0x8000;
    case 5: return m->mem + 0xC000;
    }
    return NULL;
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
        static uint8_t img[49152];
        long n = size - 30;
        memset(img, 0, sizeof img);
        if (b12 & 0x20)
            z80_unrle(buf + 30, n, img, 49152, 1);
        else
            memcpy(img, buf + 30, (size_t)(n > 49152 ? 49152 : n));
        scatter48(m, img);
    } else {
        /* v2/v3 */
        uint16_t ehlen;
        long off;
        uint8_t hwmode;
        int v3, file128;

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
        file128 = v3 ? (hwmode >= 4 && hwmode <= 6)
                     : (hwmode == 3 || hwmode == 4);
        if (file128 && m->model != HC91_MODEL_128) {
            fprintf(stderr, "error: '%s' is a 128K snapshot - run with "
                    "--machine hc128\n", path);
            free(buf);
            return -1;
        }
        if (!file128 && hwmode > 1)
            fprintf(stderr, "warning: '%s': hardware mode %u; treating "
                    "as 48K\n", path, hwmode);

        if (file128) {
            int k;
            m->port_7ffd = buf[35];
            m->screen = m->ram128[(buf[35] & 0x08) ? 7 : 5];
            if (ehlen >= 23) {
                for (k = 0; k < 16; k++) {
                    m->ay.sel = (uint8_t)k;
                    ay_write(&m->ay, buf[39 + k]);
                }
                ay_select(&m->ay, buf[38]);
            }
        } else if (m->model == HC91_MODEL_128) {
            m->port_7ffd = 0x30;             /* USR0-style 48K view */
            m->screen = m->ram128[5];
        }

        off = 30 + 2 + (long)ehlen;
        while (off + 3 <= size) {
            uint16_t complen = rd16(buf + off);
            uint8_t page = buf[off + 2];
            uint8_t *dst;
            long avail;

            off += 3;
            dst = z80_page_ptr(m, page, file128);
            if (!dst)
                fprintf(stderr, "warning: '%s': skipping page %u\n",
                        path, page);
            avail = size - off;
            if (complen == 0xFFFF) {
                long n = avail < 16384 ? avail : 16384;
                if (dst)
                    memcpy(dst, buf + off, (size_t)n);
                off += 16384;
            } else {
                if ((long)complen > avail) {
                    fprintf(stderr, "warning: '%s': truncated page data\n",
                            path);
                    complen = (uint16_t)avail;
                }
                if (dst)
                    z80_unrle(buf + off, complen, dst, 16384, 0);
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
     * the real PC + hardware mode (0 = 48K, 3 = 128K incl. port 0x7FFD
     * and the AY register file), then uncompressed 16K pages (length
     * marker 0xFFFF): three for 48K, all eight banks for the HC-128. */
    const Z80 *z = &m->cpu;
    static uint8_t out[30 + 2 + 23 + 8 * (3 + 16384)];
    static const uint8_t pages48[3] = { 8, 4, 5 };
    int is128 = (m->model == HC91_MODEL_128);
    uint8_t *p = out;
    int i, npages = is128 ? 8 : 3;

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
    if (is128) {
        p[34] = 3;                   /* v2 hardware mode: 128K */
        p[35] = m->port_7ffd;
        p[38] = m->ay.sel;
        for (i = 0; i < 16; i++)
            p[39 + i] = m->ay.reg[i];
    }
    p += 30 + 2 + 23;

    for (i = 0; i < npages; i++) {
        wr16(p, 0xFFFF);             /* uncompressed page */
        if (is128) {
            p[2] = (uint8_t)(i + 3); /* pages 3-10 = banks 0-7 */
            memcpy(p + 3, m->ram128[i], 16384);
        } else {
            p[2] = pages48[i];
            memcpy(p + 3, view_ptr(m, i + 1), 16384);
        }
        p += 3 + 16384;
    }
    return write_file(path, out, (size_t)(p - out));
}

/* ---- .scr (raw 6912-byte screen) ---- */

int screen_save_scr(const Machine *m, const char *path)
{
    return write_file(path, m->screen, 6912);
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
    memcpy(m->screen, buf, 6912);
    free(buf);
    /* park the CPU (DI + HALT) so the ROM never wipes the screen */
    m->cpu.iff1 = m->cpu.iff2 = 0;
    m->cpu.halted = 1;
    return 0;
}

/* ---- .szx (zx-state v1.4, 48K machine) ----
 *
 * Container: 8-byte header then { u32 fourcc, u32 size, payload } blocks.
 * We write CRTR + Z80R + SPCR + three uncompressed RAMP pages (5/2/0);
 * on load, zlib-compressed RAMP pages (the common case in files written
 * by other emulators) are handled by the built-in inflater. Unknown
 * blocks are skipped. SZX has a real HALTED flag, so PC is stored as-is
 * (no snap_pc rewind). dwCyclesStart is written but ignored on load (we
 * always resume at a frame boundary, like the .sna/.z80 loaders).
 */
#include "inflate.h"

#define ZXSTZF_EILAST 1
#define ZXSTZF_HALTED 2
#define ZXSTRF_COMPRESSED 1

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint8_t *szx_block(uint8_t *p, const char id[4], uint32_t size)
{
    memcpy(p, id, 4);
    wr32(p + 4, size);
    return p + 8;
}

int snapshot_save_szx(const Machine *m, const char *path)
{
    /* hdr + CRTR + Z80R + SPCR [+ AY] + up to 8 RAMP pages */
    static uint8_t buf[8 + 44 + 45 + 16 + 26 + 8 * 16395];
    uint8_t *p = buf;
    const Z80 *z = &m->cpu;
    static const uint8_t pageno[3] = { 5, 2, 0 };
    static const uint16_t pagebase[3] = { 0x4000, 0x8000, 0xC000 };
    int is128 = (m->model == HC91_MODEL_128);
    int i;

    warn_if_paged(m, path);

    memcpy(p, "ZXST", 4);
    p[4] = 1;                       /* major */
    p[5] = 4;                       /* minor */
    p[6] = (uint8_t)(is128 ? 2 : 1);   /* machine: 128K / 48K */
    p[7] = 0;
    p += 8;

    p = szx_block(p, "CRTR", 36);
    memset(p, 0, 36);
    strcpy((char *)p, "HC-91 emulator");
    wr16(p + 32, 1);
    wr16(p + 34, 0);
    p += 36;

    p = szx_block(p, "Z80R", 37);
    wr16(p + 0, z->af.w);
    wr16(p + 2, z->bc.w);
    wr16(p + 4, z->de.w);
    wr16(p + 6, z->hl.w);
    wr16(p + 8, z->af_.w);
    wr16(p + 10, z->bc_.w);
    wr16(p + 12, z->de_.w);
    wr16(p + 14, z->hl_.w);
    wr16(p + 16, z->ix.w);
    wr16(p + 18, z->iy.w);
    wr16(p + 20, z->sp.w);
    wr16(p + 22, z->pc.w);
    p[24] = z->i;
    p[25] = z->r;
    p[26] = z->iff1;
    p[27] = z->iff2;
    p[28] = z->im;
    wr32(p + 29, (uint32_t)(z->tstates - m->frame_start_ts));
    p[33] = 0;                      /* chHoldIntReqCycles */
    p[34] = (uint8_t)((z->ei_pending ? ZXSTZF_EILAST : 0)
                      | (z->halted ? ZXSTZF_HALTED : 0));
    wr16(p + 35, z->memptr.w);
    p += 37;

    p = szx_block(p, "SPCR", 8);
    memset(p, 0, 8);
    p[0] = m->border;
    p[1] = m->port_7ffd;            /* ch7ffd (0 on 48K models) */
    p[3] = (uint8_t)(m->border | (m->beeper << 4));   /* last FE OUT */
    p += 8;

    if (is128) {
        p = szx_block(p, "AY\0\0", 18);
        p[0] = 0;                   /* flags */
        p[1] = m->ay.sel;
        memcpy(p + 2, m->ay.reg, 16);
        p += 18;
        for (i = 0; i < 8; i++) {
            p = szx_block(p, "RAMP", 3 + 16384);
            wr16(p, 0);             /* uncompressed */
            p[2] = (uint8_t)i;
            memcpy(p + 3, m->ram128[i], 16384);
            p += 3 + 16384;
        }
    } else {
        for (i = 0; i < 3; i++) {
            p = szx_block(p, "RAMP", 3 + 16384);
            wr16(p, 0);             /* uncompressed */
            p[2] = pageno[i];
            memcpy(p + 3, m->mem + pagebase[i], 16384);
            p += 3 + 16384;
        }
    }

    return write_file(path, buf, (size_t)(p - buf));
}

int snapshot_load_szx(Machine *m, const char *path)
{
    long size;
    uint8_t *buf = read_whole_file(path, &size);
    long off = 8;
    int got_ram = 0, file128, want_ram;

    if (!buf)
        return -1;
    if (size < 8 || memcmp(buf, "ZXST", 4) != 0) {
        fprintf(stderr, "error: '%s' is not an SZX (zx-state) file\n", path);
        free(buf);
        return -1;
    }
    if (buf[6] > 2) {
        fprintf(stderr, "error: '%s': SZX machine id %d not supported "
                "(only 16K/48K/128K)\n", path, buf[6]);
        free(buf);
        return -1;
    }
    file128 = (buf[6] == 2);
    want_ram = file128 ? 8 : 3;
    if (file128 && m->model != HC91_MODEL_128) {
        fprintf(stderr, "error: '%s' is a 128K snapshot - run with "
                "--machine hc128\n", path);
        free(buf);
        return -1;
    }
    if (m->model == HC91_MODEL_128 && !file128) {
        m->port_7ffd = 0x30;                 /* USR0-style 48K view */
        m->screen = m->ram128[5];
    }

    while (off + 8 <= size) {
        const uint8_t *blk = buf + off + 8;
        uint32_t bsz = rd32(buf + off + 4);
        if ((long)bsz > size - off - 8) {
            fprintf(stderr, "error: '%s': truncated SZX block\n", path);
            free(buf);
            return -1;
        }
        if (!memcmp(buf + off, "Z80R", 4) && bsz >= 29) {
            Z80 *z = &m->cpu;
            z->af.w = rd16(blk + 0);
            z->bc.w = rd16(blk + 2);
            z->de.w = rd16(blk + 4);
            z->hl.w = rd16(blk + 6);
            z->af_.w = rd16(blk + 8);
            z->bc_.w = rd16(blk + 10);
            z->de_.w = rd16(blk + 12);
            z->hl_.w = rd16(blk + 14);
            z->ix.w = rd16(blk + 16);
            z->iy.w = rd16(blk + 18);
            z->sp.w = rd16(blk + 20);
            z->pc.w = rd16(blk + 22);
            z->i = blk[24];
            z->r = blk[25];
            z->iff1 = (uint8_t)(blk[26] != 0);
            z->iff2 = (uint8_t)(blk[27] != 0);
            z->im = (uint8_t)(blk[28] & 3);
            z->ei_pending = 0;
            z->halted = 0;
            if (bsz >= 35) {
                z->ei_pending = (uint8_t)((blk[34] & ZXSTZF_EILAST) != 0);
                z->halted = (uint8_t)((blk[34] & ZXSTZF_HALTED) != 0);
            }
            z->memptr.w = (bsz >= 37) ? rd16(blk + 35) : 0;
            z->q = 0;               /* not represented in SZX */
        } else if (!memcmp(buf + off, "SPCR", 4) && bsz >= 4) {
            m->border = (uint8_t)(blk[0] & 7);
            m->beeper = (uint8_t)((blk[3] >> 4) & 1);
            if (file128) {
                m->port_7ffd = blk[1];
                m->screen = m->ram128[(blk[1] & 0x08) ? 7 : 5];
            }
        } else if (!memcmp(buf + off, "AY\0\0", 4) && bsz >= 18
                   && m->model == HC91_MODEL_128) {
            int k;
            for (k = 0; k < 16; k++) {
                m->ay.sel = (uint8_t)k;
                ay_write(&m->ay, blk[2 + k]);
            }
            ay_select(&m->ay, blk[1]);
        } else if (!memcmp(buf + off, "RAMP", 4) && bsz >= 3) {
            uint16_t flags = rd16(blk);
            uint8_t page = blk[2];
            uint8_t *dst = NULL;
            if (m->model == HC91_MODEL_128) {
                if (file128)
                    dst = page < 8 ? m->ram128[page] : NULL;
                else
                    dst = page == 5 ? m->ram128[5]
                        : page == 2 ? m->ram128[2]
                        : page == 0 ? m->ram128[0] : NULL;
            } else {
                dst = page == 5 ? m->mem + 0x4000
                    : page == 2 ? m->mem + 0x8000
                    : page == 0 ? m->mem + 0xC000 : NULL;
            }
            if (dst) {
                if (flags & ZXSTRF_COMPRESSED) {
                    if (zlib_inflate(blk + 3, bsz - 3,
                                     dst, 16384) != 16384) {
                        fprintf(stderr, "error: '%s': bad compressed RAM "
                                "page %d\n", path, page);
                        free(buf);
                        return -1;
                    }
                } else if (bsz - 3 >= 16384) {
                    memcpy(dst, blk + 3, 16384);
                } else {
                    fprintf(stderr, "error: '%s': short RAM page %d\n",
                            path, page);
                    free(buf);
                    return -1;
                }
                got_ram++;
            }
        }
        off += 8 + (long)bsz;
    }
    free(buf);
    if (got_ram < want_ram)
        fprintf(stderr, "warning: '%s': only %d of %d RAM pages present\n",
                path, got_ram, want_ram);
    return 0;
}
