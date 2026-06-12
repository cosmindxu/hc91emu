/* tape.c — .tap/.tzx parsing, trap-based instant LD-BYTES loading, the
 * pulse-level tape player, and the SA-BYTES SAVE trap. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "machine.h"
#include "inflate.h"

void tape_free(Machine *m)
{
    int i;
    for (i = 0; i < m->tape.nblocks; i++)
        free(m->tape.blocks[i].data);
    free(m->tape.blocks);
    memset(&m->tape, 0, sizeof(m->tape));
    free(m->player.pulses);
    free(m->player.bounds);
    free(m->player.bstop);
    memset(&m->player, 0, sizeof(m->player));
}

/* ---- Pulse stream compiler ----
 * Standard ROM timings (T-states). */
#define T_PILOT     2168
#define T_SYNC1      667
#define T_SYNC2      735
#define T_BIT0       855
#define T_BIT1      1710
#define PILOT_HDR   8063
#define PILOT_DATA  3223
#define T_PER_MS    3500

static int pulse_push(Machine *m, uint32_t dur)
{
    TapePlayer *p = &m->player;
    static size_t cap;                  /* tracked per load; reset below */
    if (p->npulses == 0)
        cap = 0;
    if (p->npulses == cap) {
        uint32_t *np;
        cap = cap ? cap * 2 : 4096;
        np = realloc(p->pulses, cap * sizeof(uint32_t));
        if (!np)
            return -1;
        p->pulses = np;
    }
    p->pulses[p->npulses++] = dur;
    return 0;
}

/* Emit one data block: pilot tone, two sync pulses, MSB-first data bits
 * (two equal pulses per bit), trailing pause. Any element can be zero to
 * skip it. */
static void bound_push(Machine *m, int stop);

static int emit_data(Machine *m, uint32_t pilot, uint32_t pilot_cnt,
                     uint32_t s1, uint32_t s2, uint32_t b0, uint32_t b1,
                     int last_bits, uint32_t pause_ms,
                     const uint8_t *data, uint32_t n)
{
    uint32_t i;
    int b;
    bound_push(m, 0);
    for (i = 0; i < pilot_cnt; i++)
        if (pulse_push(m, pilot)) return -1;
    if (s1 && pulse_push(m, s1)) return -1;
    if (s2 && pulse_push(m, s2)) return -1;
    for (i = 0; i < n; i++) {
        int nb = (i == n - 1) ? last_bits : 8;
        for (b = 0; b < nb; b++) {
            uint32_t t = (data[i] & (0x80u >> b)) ? b1 : b0;
            if (pulse_push(m, t) || pulse_push(m, t))
                return -1;
        }
    }
    if (pause_ms && pulse_push(m, pause_ms * T_PER_MS))
        return -1;
    return 0;
}

/* Append a flag+data+checksum block to the trap-loader list. */
static int trap_block_add(Machine *m, const uint8_t *data, uint16_t len,
                          int *cap)
{
    if (m->tape.nblocks == *cap) {
        TapBlock *nb;
        *cap = *cap ? *cap * 2 : 16;
        nb = realloc(m->tape.blocks, (size_t)*cap * sizeof(TapBlock));
        if (!nb)
            return -1;
        m->tape.blocks = nb;
    }
    m->tape.blocks[m->tape.nblocks].len = len;
    m->tape.blocks[m->tape.nblocks].data = malloc(len ? len : 1);
    if (!m->tape.blocks[m->tape.nblocks].data)
        return -1;
    memcpy(m->tape.blocks[m->tape.nblocks].data, data, len);
    m->tape.nblocks++;
    return 0;
}

static uint32_t rd24(const uint8_t *p)
{
    return (uint32_t)(p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16));
}

static uint32_t rd32(const uint8_t *p)
{
    return rd24(p) | ((uint32_t)p[3] << 24);
}

/* Record a block boundary at the current stream position; stop = 1 for
 * unconditional TZX stop-the-tape markers. */
static void bound_push(Machine *m, int stop)
{
    TapePlayer *p = &m->player;
    static size_t cap;
    if (p->nbounds == 0)
        cap = 0;
    if (p->nbounds && p->bounds[p->nbounds - 1] == p->npulses) {
        p->bstop[p->nbounds - 1] |= (uint8_t)stop;
        return;                       /* merge empty blocks */
    }
    if (p->nbounds == cap) {
        size_t nc = cap ? cap * 2 : 64;
        size_t *nb = realloc(p->bounds, nc * sizeof(size_t));
        uint8_t *ns = realloc(p->bstop, nc);
        if (!nb || !ns) {
            free(nb);
            return;
        }
        p->bounds = nb;
        p->bstop = ns;
        cap = nc;
    }
    p->bounds[p->nbounds] = p->npulses;
    p->bstop[p->nbounds] = (uint8_t)stop;
    p->nbounds++;
}

/* Lengthen the previous pulse (suppresses the edge a new pulse would
 * make — the generalized-data "no edge" symbol polarity). */
static void pulse_extend(Machine *m, uint32_t dur)
{
    TapePlayer *p = &m->player;
    if (p->npulses)
        p->pulses[p->npulses - 1] += dur;
    else
        pulse_push(m, dur);
}

/* Emit one TZX 0x19 symbol: def = flags byte + np u16 durations (0 ends
 * the symbol). The pulse stream toggles from level 0, so the level of
 * the next pushed pulse is npulses&1; the flags adjust the first edge:
 * 0 = toggle (natural), 1 = no edge (merge into the previous pulse),
 * 2/3 = force low/high (a zero-length pulse flips parity for free). */
static int gdb_symbol(Machine *m, const uint8_t *def, int np)
{
    int flags = def[0] & 3, k, merged = 0;

    if (flags == 1)
        merged = 1;
    else if (flags == 2 && (m->player.npulses & 1))
        pulse_push(m, 0);
    else if (flags == 3 && !(m->player.npulses & 1))
        pulse_push(m, 0);
    for (k = 0; k < np; k++) {
        uint32_t d = (uint32_t)(def[1 + k * 2] | (def[2 + k * 2] << 8));
        if (!d)
            break;
        if (merged) {
            pulse_extend(m, d);
            merged = 0;
        } else if (pulse_push(m, d)) {
            return -1;
        }
    }
    return 0;
}

/* Parse a TZX file: standard/turbo data blocks feed both the trap list
 * and the pulse stream; tone/pulse/pause/loop blocks feed pulses only;
 * info blocks are skipped. Unsupported structural blocks end the pulse
 * compile (with a warning) but keep whatever was parsed. */
static int tzx_parse(Machine *m, const uint8_t *buf, long size,
                     const char *path, int *cap)
{
    long off = 10;
    long loop_off = -1;
    uint32_t loop_left = 0;

    while (off < size) {
        uint8_t id = buf[off++];
        switch (id) {
        case 0x10: {                       /* standard speed data */
            uint32_t pause, len;
            if (off + 4 > size) return 0;
            pause = (uint32_t)(buf[off] | (buf[off+1] << 8));
            len = (uint32_t)(buf[off+2] | (buf[off+3] << 8));
            off += 4;
            if (off + (long)len > size) return 0;
            trap_block_add(m, buf + off, (uint16_t)len, cap);
            emit_data(m, T_PILOT,
                      (len && buf[off] < 128) ? PILOT_HDR : PILOT_DATA,
                      T_SYNC1, T_SYNC2, T_BIT0, T_BIT1, 8,
                      pause ? pause : 1000, buf + off, len);
            off += (long)len;
            break;
        }
        case 0x11: {                       /* turbo speed data */
            uint32_t pilot, s1, s2, b0, b1, pcnt, pause, len;
            int lastb;
            if (off + 18 > size) return 0;
            pilot = (uint32_t)(buf[off] | (buf[off+1] << 8));
            s1 = (uint32_t)(buf[off+2] | (buf[off+3] << 8));
            s2 = (uint32_t)(buf[off+4] | (buf[off+5] << 8));
            b0 = (uint32_t)(buf[off+6] | (buf[off+7] << 8));
            b1 = (uint32_t)(buf[off+8] | (buf[off+9] << 8));
            pcnt = (uint32_t)(buf[off+10] | (buf[off+11] << 8));
            lastb = buf[off+12];
            pause = (uint32_t)(buf[off+13] | (buf[off+14] << 8));
            len = rd24(buf + off + 15);
            off += 18;
            if (off + (long)len > size) return 0;
            trap_block_add(m, buf + off, (uint16_t)len, cap);
            emit_data(m, pilot, pcnt, s1, s2, b0, b1, lastb, pause,
                      buf + off, len);
            off += (long)len;
            break;
        }
        case 0x12: {                       /* pure tone */
            uint32_t t, n, i;
            if (off + 4 > size) return 0;
            t = (uint32_t)(buf[off] | (buf[off+1] << 8));
            n = (uint32_t)(buf[off+2] | (buf[off+3] << 8));
            off += 4;
            for (i = 0; i < n; i++)
                pulse_push(m, t);
            break;
        }
        case 0x13: {                       /* pulse sequence */
            uint32_t n, i;
            if (off + 1 > size) return 0;
            n = buf[off++];
            if (off + (long)n * 2 > size) return 0;
            for (i = 0; i < n; i++)
                pulse_push(m, (uint32_t)(buf[off + i*2] |
                                         (buf[off + i*2 + 1] << 8)));
            off += (long)n * 2;
            break;
        }
        case 0x14: {                       /* pure data */
            uint32_t b0, b1, pause, len;
            int lastb;
            if (off + 10 > size) return 0;
            b0 = (uint32_t)(buf[off] | (buf[off+1] << 8));
            b1 = (uint32_t)(buf[off+2] | (buf[off+3] << 8));
            lastb = buf[off+4];
            pause = (uint32_t)(buf[off+5] | (buf[off+6] << 8));
            len = rd24(buf + off + 7);
            off += 10;
            if (off + (long)len > size) return 0;
            emit_data(m, 0, 0, 0, 0, b0, b1, lastb, pause, buf + off, len);
            off += (long)len;
            break;
        }
        case 0x18: {                       /* CSW recording */
            uint32_t blen, rate, want, got = 0;
            uint16_t pause;
            uint8_t comp;
            const uint8_t *s;
            uint8_t *un = NULL;
            size_t sn, i;
            if (off + 4 > size) return 0;
            blen = rd32(buf + off);
            off += 4;
            if (blen < 10 || off + (long)blen > size) {
                fprintf(stderr, "warning: '%s': truncated CSW block\n",
                        path);
                return 0;
            }
            pause = (uint16_t)(buf[off] | (buf[off + 1] << 8));
            rate = rd24(buf + off + 2);
            comp = buf[off + 5];
            want = rd32(buf + off + 6);
            if (!rate) rate = 44100;
            bound_push(m, 0);
            if (comp == 2) {               /* Z-RLE: zlib stream */
                un = zlib_inflate_alloc(buf + off + 10, blen - 10, &sn);
                if (!un) {
                    fprintf(stderr, "warning: '%s': bad Z-RLE CSW data\n",
                            path);
                    return 0;
                }
                s = un;
            } else if (comp == 1) {        /* plain RLE */
                s = buf + off + 10;
                sn = blen - 10;
            } else {
                fprintf(stderr, "warning: '%s': CSW compression %u "
                        "unsupported\n", path, comp);
                return 0;
            }
            off += blen;
            for (i = 0; i < sn; ) {
                uint32_t samp = s[i++];
                if (samp == 0) {
                    if (i + 4 > sn) break;
                    samp = rd32(s + i);
                    i += 4;
                }
                pulse_push(m, (uint32_t)(((uint64_t)samp * 3500000
                                          + rate / 2) / rate));
                got++;
            }
            free(un);
            if (got != want)
                fprintf(stderr, "warning: '%s': CSW pulse count %u != "
                        "header %u\n", path, got, want);
            if (pause)
                pulse_push(m, (uint32_t)pause * T_PER_MS);
            break;
        }
        case 0x19: {                       /* generalized data */
            uint32_t blen, totp, totd, e;
            uint16_t pause;
            int npp, nasp, npd, nasd, nb;
            long base, sp, pr, sd, ds;
            if (off + 4 > size) return 0;
            blen = rd32(buf + off);
            off += 4;
            if (blen < 14 || off + (long)blen > size) {
                fprintf(stderr, "warning: '%s': truncated generalized "
                        "data block\n", path);
                return 0;
            }
            base = off;
            off += blen;
            pause = (uint16_t)(buf[base] | (buf[base + 1] << 8));
            totp = rd32(buf + base + 2);
            npp = buf[base + 6];
            nasp = buf[base + 7] ? buf[base + 7] : 256;
            totd = rd32(buf + base + 8);
            npd = buf[base + 12];
            nasd = buf[base + 13] ? buf[base + 13] : 256;
            bound_push(m, 0);
            sp = base + 14;                          /* pilot symbols */
            pr = sp + (totp ? (long)nasp * (1 + 2 * npp) : 0);
            sd = pr + (totp ? (long)totp * 3 : 0);   /* data symbols */
            ds = sd + (totd ? (long)nasd * (1 + 2 * npd) : 0);
            nb = 0;
            while ((1 << nb) < nasd)
                nb++;
            if (ds + (totd ? ((long)totd * nb + 7) / 8 : 0) > base + (long)blen) {
                fprintf(stderr, "warning: '%s': generalized data block "
                        "overruns\n", path);
                return 0;
            }
            for (e = 0; e < totp; e++) {             /* pilot: PRLE */
                uint8_t sym = buf[pr + e * 3];
                uint32_t rep = (uint32_t)(buf[pr + e * 3 + 1]
                                          | (buf[pr + e * 3 + 2] << 8));
                if (sym >= nasp) continue;
                if (!rep) rep = 1;
                while (rep--)
                    gdb_symbol(m, buf + sp + sym * (1 + 2 * npp), npp);
            }
            for (e = 0; e < totd; e++) {             /* data: nb-bit syms */
                uint32_t bit = e * (uint32_t)nb, sym = 0;
                int k;
                for (k = 0; k < nb; k++, bit++)
                    sym = (sym << 1)
                        | ((buf[ds + (bit >> 3)] >> (7 - (bit & 7))) & 1);
                if (sym < (uint32_t)nasd)
                    gdb_symbol(m, buf + sd + sym * (1 + 2 * npd), npd);
            }
            if (pause)
                pulse_push(m, (uint32_t)pause * T_PER_MS);
            break;
        }
        case 0x20: {                       /* pause / stop */
            uint32_t ms;
            if (off + 2 > size) return 0;
            ms = (uint32_t)(buf[off] | (buf[off+1] << 8));
            off += 2;
            if (ms == 0)                   /* "stop the tape" */
                bound_push(m, 1);
            else
                pulse_push(m, ms * T_PER_MS);
            break;
        }
        case 0x21:                         /* group start (name skipped) */
            if (off + 1 > size) return 0;
            off += 1 + buf[off];
            break;
        case 0x22:                         /* group end */
            break;
        case 0x24:                         /* loop start */
            if (off + 2 > size) return 0;
            loop_left = (uint32_t)(buf[off] | (buf[off+1] << 8));
            off += 2;
            loop_off = off;
            if (loop_left)
                loop_left--;               /* first pass plays now */
            break;
        case 0x25:                         /* loop end */
            if (loop_left && loop_off >= 0) {
                loop_left--;
                off = loop_off;
            }
            break;
        case 0x2A:                         /* stop if 48K: we are one */
            if (off + 4 > size) return 0;
            off += 4 + (long)rd32(buf + off);
            bound_push(m, 1);
            break;
        case 0x30:                         /* text description */
            if (off + 1 > size) return 0;
            off += 1 + buf[off];
            break;
        case 0x31:                         /* message */
            if (off + 2 > size) return 0;
            off += 2 + buf[off + 1];
            break;
        case 0x32:                         /* archive info */
            if (off + 2 > size) return 0;
            off += 2 + (long)(buf[off] | (buf[off+1] << 8));
            break;
        case 0x33:                         /* hardware type */
            if (off + 1 > size) return 0;
            off += 1 + 3 * (long)buf[off];
            break;
        case 0x35:                         /* custom info */
            if (off + 14 > size) return 0;
            off += 14 + (long)(buf[off+10] | (buf[off+11] << 8) |
                               ((uint32_t)buf[off+12] << 16) |
                               ((uint32_t)buf[off+13] << 24));
            break;
        case 0x5A:                         /* glue */
            off += 9;
            break;
        default:
            fprintf(stderr, "warning: '%s': unsupported TZX block 0x%02X; "
                    "tape ends here\n", path, id);
            return 0;
        }
    }
    return 0;
}

int tape_load(Machine *m, const char *path)
{
    FILE *f;
    long size;
    uint8_t *buf;
    long off;
    int cap = 0;

    m->tape_cur = path;
    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: cannot open tape file '%s'\n", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fprintf(stderr, "error: tape file '%s' is empty\n", path);
        fclose(f);
        return -1;
    }
    buf = malloc((size_t)size);
    if (!buf) {
        fclose(f);
        return -1;
    }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fprintf(stderr, "error: cannot read tape file '%s'\n", path);
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);

    tape_free(m);

    if (size > 10 && memcmp(buf, "ZXTape!\x1A", 8) == 0) {
        tzx_parse(m, buf, size, path, &cap);
    } else {
        off = 0;
        while (off + 2 <= size) {
            uint16_t len = (uint16_t)(buf[off] | (buf[off + 1] << 8));
            off += 2;
            if (off + len > size) {
                fprintf(stderr,
                        "warning: tape '%s': truncated block %d, ignored\n",
                        path, m->tape.nblocks);
                break;
            }
            if (trap_block_add(m, buf + off, len, &cap)) {
                free(buf);
                tape_free(m);
                return -1;
            }
            emit_data(m, T_PILOT,
                      (len && buf[off] < 128) ? PILOT_HDR : PILOT_DATA,
                      T_SYNC1, T_SYNC2, T_BIT0, T_BIT1, 8, 1000,
                      buf + off, len);
            off += len;
        }
    }
    free(buf);

    if (m->tape.nblocks == 0 && m->player.npulses == 0) {
        fprintf(stderr, "error: tape '%s' contains no blocks\n", path);
        return -1;
    }
    m->tape.pos = 0;
    m->tape.attached = 1;
    return 0;
}

/* Swap cassettes: replace the attached tape with another file (the
 * other side of a multi-load game; repeated calls toggle the sides).
 * The new tape starts rewound and paused at block 0; the next hot
 * loader poll sets it rolling. */
int tape_swap(Machine *m, const char *path)
{
    const char *old = m->tape_cur;

    tape_free(m);
    if (tape_load(m, path) != 0)
        return -1;
    m->tape_next = old;
    tape_play_start(m);
    if (m->player.playing) {
        m->player.paused = 1;
        fprintf(stderr, "tape: inserted '%s' (paused at the start)\n",
                path);
    }
    return 0;
}

/* ---- Pulse player ---- */

void tape_play_start(Machine *m)
{
    TapePlayer *p = &m->player;
    if (!p->npulses)
        return;
    p->idx = 0;
    p->ear = 0;
    p->edge_ts = m->cpu.tstates + p->pulses[0];
    p->playing = 1;
    p->paused = 0;
    p->nextb = 0;
    while (p->nextb < p->nbounds && p->bounds[p->nextb] == 0)
        p->nextb++;                       /* never pause before block 0 */
    p->win_start = m->cpu.tstates;
    p->win_reads = 0;
    p->hot = 0;
}

/* The EAR-poll rate decides pause/resume at block boundaries: a loader
 * reads the port every ~50-200 T (hundreds per window). Reads issued
 * from the ROM keyboard scanner (KEY-SCAN, 0x028E-0x02BE) don't count —
 * a "press any key" wait calls it in a tight loop and would otherwise
 * look exactly like a loader; real loaders read from LD-EDGE (0x05E7)
 * or from RAM. */
#define TAPE_HOT_WINDOW 10000             /* T-states */
#define TAPE_HOT_READS  32

int tape_player_ear(Machine *m)
{
    TapePlayer *p = &m->player;
    uint64_t now = m->cpu.tstates;
    uint16_t pc = m->cpu.pc.w;

    if (now - p->win_start > TAPE_HOT_WINDOW) {
        p->hot = (p->win_reads >= TAPE_HOT_READS);
        p->win_start = now;
        p->win_reads = 0;
    }
    if (!(pc >= 0x028E && pc <= 0x02BE))  /* not the ROM key scanner */
        p->win_reads++;

    if (p->paused) {
        if (p->hot) {                     /* loader is back: roll on */
            p->paused = 0;
            p->edge_ts = now + p->pulses[p->idx];
            fprintf(stderr, "tape: resumed (block %zu/%zu)\n",
                    p->nextb, p->nbounds);
        }
        return p->ear;
    }

    while (p->playing && now >= p->edge_ts) {
        p->ear ^= 1;
        p->idx++;
        if (p->idx >= p->npulses) {
            if (p->hot) {
                /* a loader is still searching (e.g. "rewind tape" for
                 * an earlier level): wind back to the start and pause
                 * at block 0 — the next hot read resumes from there */
                p->idx = 0;
                p->ear = 0;
                p->nextb = 1;
                p->paused = 1;
                fprintf(stderr, "tape: end of tape - rewound\n");
            } else {
                p->playing = 0;
                p->ear = 0;
            }
            break;
        }
        if (p->nextb < p->nbounds && p->idx == p->bounds[p->nextb]) {
            int stop = p->bstop[p->nextb];
            p->nextb++;
            if (stop || !p->hot) {        /* nobody is listening */
                p->paused = 1;
                fprintf(stderr, "tape: paused at block %zu/%zu "
                        "(waiting for the loader%s)\n", p->nextb,
                        p->nbounds, stop ? "; stop-the-tape marker" : "");
                break;
            }
            if (getenv("HC91_TAPE_DEBUG"))
                fprintf(stderr, "tape: block %zu/%zu\n",
                        p->nextb, p->nbounds);
        }
        p->edge_ts += p->pulses[p->idx];
    }
    return p->ear;
}

/* Simulate RET. */
static void trap_ret(Machine *m)
{
    Z80 *z = &m->cpu;
    uint16_t lo = z->mem_read(z->ctx, z->sp.w);
    uint16_t hi = z->mem_read(z->ctx, (uint16_t)(z->sp.w + 1));
    z->pc.w = (uint16_t)(lo | (hi << 8));
    z->sp.w = (uint16_t)(z->sp.w + 2);
    z->memptr.w = z->pc.w;
}

/* ROM SA-BYTES trap at PC=0x04C2: append the block to the --save-tape
 * .tap file. Entry: A = flag byte, IX = start, DE = length. */
void tape_save_trap(Machine *m)
{
    Z80 *z = &m->cpu;
    uint16_t n = z->de.w;
    uint8_t flag = z->af.b.h, cs;
    uint16_t i;
    FILE *f = fopen(m->save_tape, "ab");

    if (!f) {
        fprintf(stderr, "error: cannot append to '%s'\n", m->save_tape);
        z->af.b.l &= (uint8_t)~ZF_C;
        trap_ret(m);
        return;
    }
    fputc((n + 2) & 0xFF, f);
    fputc(((n + 2) >> 8) & 0xFF, f);
    fputc(flag, f);
    cs = flag;
    for (i = 0; i < n; i++) {
        uint8_t b = z->mem_read(z->ctx, (uint16_t)(z->ix.w + i));
        fputc(b, f);
        cs ^= b;
    }
    fputc(cs, f);
    fclose(f);

    z->ix.w = (uint16_t)(z->ix.w + n);
    z->de.w = 0;
    z->af.b.l |= ZF_C;                 /* success */
    trap_ret(m);
}

/* ROM LD-BYTES trap at PC=0x0556.
 * Entry: A = expected flag byte, IX = destination, DE = byte count,
 * carry set = LOAD (verify treated identically). */
void tape_trap(Machine *m)
{
    Z80 *z = &m->cpu;
    TapBlock *blk;
    uint8_t want = z->af.b.h;
    uint16_t requested = z->de.w;

    if (m->tape.pos >= m->tape.nblocks) {
        z->af.b.l &= (uint8_t)~ZF_C;   /* no more data: error */
        trap_ret(m);
        return;
    }

    blk = &m->tape.blocks[m->tape.pos++];

    if (blk->len < 1 || blk->data[0] != want) {
        z->af.b.l &= (uint8_t)~ZF_C;   /* flag mismatch */
        trap_ret(m);
        return;
    }

    {
        uint32_t avail = (blk->len >= 2) ? (uint32_t)(blk->len - 2) : 0;
        uint32_t n = requested < avail ? requested : avail;
        uint32_t i;
        for (i = 0; i < n; i++)
            z->mem_write(z->ctx, (uint16_t)(z->ix.w + i), blk->data[1 + i]);
        z->ix.w = (uint16_t)(z->ix.w + n);
        z->de.w = (uint16_t)(z->de.w - n);
        if (n == requested)
            z->af.b.l |= ZF_C;          /* success */
        else
            z->af.b.l &= (uint8_t)~ZF_C;
        z->af.b.h = 0;
    }
    trap_ret(m);
}
