/* rzx.c — RZX input-recording: record (--rzx-record) and playback (an
 * .rzx input file).
 *
 * An RZX frame spans interrupt to interrupt and stores how many M1/R
 * fetches the CPU performed and the byte returned by every port read,
 * in order. Playback therefore runs each frame until the recorded fetch
 * count is consumed (instead of a T-state budget) and feeds port reads
 * from the recording — given the same starting snapshot this reproduces
 * the run exactly, which is what makes recordings usable as whole-game
 * regression tests.
 *
 * Recording writes: creator block, an embedded uncompressed .z80 v2
 * snapshot of the state at recording start, and one uncompressed input
 * block. Playback additionally accepts zlib-compressed snapshot and
 * input blocks (the normal case for files from other emulators) via the
 * built-in inflater; "protected" (encrypted) recordings are rejected.
 */
#define _POSIX_C_SOURCE 200809L   /* mkstemp, fdopen under -std=c99 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "machine.h"
#include "debug.h"
#include "rzx.h"
#include "inflate.h"

static void wr16b(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wr32b(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint16_t rd16b(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32b(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Portable temp-file template (honors TMPDIR; TEMP/TMP on Windows). */
static void tmp_template(char *buf, size_t sz)
{
    const char *d = getenv("TMPDIR");
#ifdef _WIN32
    if (!d) d = getenv("TEMP");
    if (!d) d = getenv("TMP");
    if (!d) d = ".";
#else
    if (!d) d = "/tmp";
#endif
    snprintf(buf, sz, "%s/hc91rzxXXXXXX", d);
}

static int grow(uint8_t **buf, size_t *cap, size_t need)
{
    if (need <= *cap)
        return 0;
    {
        size_t nc = *cap ? *cap : 4096;
        uint8_t *nb;
        while (nc < need)
            nc *= 2;
        nb = realloc(*buf, nc);
        if (!nb)
            return -1;
        *buf = nb;
        *cap = nc;
    }
    return 0;
}

int rzx_playing(const Machine *m)
{
    return m->rzx && m->rzx->playing;
}

/* ---- recording ---- */

int rzx_record_start(Machine *m, const char *path)
{
    Rzx *r = m->rzx;
    char tmp[512];
    int fd;
    long n;
    FILE *f;

    if (m->tape.attached && !m->real_tape)
        fprintf(stderr, "warning: rzx: instant tape traps mutate state "
                "outside the recorded inputs; playback will desync "
                "(use --real-tape)\n");

    /* snapshot of the state at recording start, embedded later */
    tmp_template(tmp, sizeof tmp);
    fd = mkstemp(tmp);
    if (fd < 0) {
        fprintf(stderr, "error: rzx: cannot create temp snapshot\n");
        return -1;
    }
    close(fd);
    if (snapshot_save_z80(m, tmp) != 0) {
        unlink(tmp);
        return -1;
    }
    f = fopen(tmp, "rb");
    if (!f) {
        unlink(tmp);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    r->snap = malloc((size_t)n);
    if (!r->snap || (long)fread(r->snap, 1, (size_t)n, f) != n) {
        fclose(f);
        unlink(tmp);
        return -1;
    }
    fclose(f);
    unlink(tmp);
    r->snap_len = (size_t)n;

    r->rec_path = path;
    r->recording = 1;
    return 0;
}

void rzx_frame_begin(Machine *m)
{
    Rzx *r = m->rzx;
    if (!r->recording)
        return;
    r->fetch_base = m->cpu.fetches;
    r->rin_n = 0;
}

void rzx_log_in(Machine *m, uint8_t v)
{
    Rzx *r = m->rzx;
    if (!r->recording)
        return;
    if (grow(&r->rin, &r->rin_cap, r->rin_n + 1) == 0)
        r->rin[r->rin_n++] = v;
}

void rzx_frame_end(Machine *m)
{
    Rzx *r = m->rzx;
    uint64_t fc;
    if (!r->recording)
        return;
    fc = m->cpu.fetches - r->fetch_base;
    if (fc > 0xFFFF || r->rin_n > 0xFFFE) {
        fprintf(stderr, "error: rzx: frame too large, recording stopped\n");
        r->recording = 0;
        return;
    }
    if (grow(&r->fdata, &r->fdata_cap, r->fdata_n + 4 + r->rin_n) != 0)
        return;
    wr16b(r->fdata + r->fdata_n, (uint16_t)fc);
    wr16b(r->fdata + r->fdata_n + 2, (uint16_t)r->rin_n);
    if (r->rin_n)                   /* rin may be NULL while empty */
        memcpy(r->fdata + r->fdata_n + 4, r->rin, r->rin_n);
    r->fdata_n += 4 + r->rin_n;
    r->rec_frames++;
}

int rzx_record_finish(Machine *m)
{
    Rzx *r = m->rzx;
    FILE *f;
    uint8_t hdr[10], blk[32];

    if (!r->rec_path)
        return 0;
    f = fopen(r->rec_path, "wb");
    if (!f) {
        fprintf(stderr, "error: cannot write '%s'\n", r->rec_path);
        return -1;
    }
    memcpy(hdr, "RZX!", 4);
    hdr[4] = 0;                                  /* version 0.13 */
    hdr[5] = 13;
    wr32b(hdr + 6, 0);
    fwrite(hdr, 1, 10, f);

    blk[0] = 0x10;                               /* creator */
    wr32b(blk + 1, 29);
    memset(blk + 5, 0, 24);
    strcpy((char *)blk + 5, "HC-91 emulator");
    wr16b(blk + 25, 1);
    wr16b(blk + 27, 0);
    fwrite(blk, 1, 29, f);

    blk[0] = 0x30;                               /* snapshot, .z80 */
    wr32b(blk + 1, (uint32_t)(17 + r->snap_len));
    wr32b(blk + 5, 0);                           /* embedded, raw */
    memcpy(blk + 9, "z80\0", 4);
    wr32b(blk + 13, (uint32_t)r->snap_len);
    fwrite(blk, 1, 17, f);
    fwrite(r->snap, 1, r->snap_len, f);

    blk[0] = 0x80;                               /* input recording */
    wr32b(blk + 1, (uint32_t)(18 + r->fdata_n));
    wr32b(blk + 5, r->rec_frames);
    blk[9] = 0;
    wr32b(blk + 10, 0);                          /* tstates at start */
    wr32b(blk + 14, 0);                          /* uncompressed */
    fwrite(blk, 1, 18, f);
    fwrite(r->fdata, 1, r->fdata_n, f);

    if (fclose(f) != 0) {
        fprintf(stderr, "error: cannot write '%s'\n", r->rec_path);
        return -1;
    }
    fprintf(stderr, "rzx: wrote %u frames to %s\n", r->rec_frames,
            r->rec_path);
    return 0;
}

/* ---- playback ---- */

/* Parse the frame stream into the frames[] index (expanding 0xFFFF
 * "repeat previous" counts). data is kept as the value pool. */
static int parse_frames(Machine *m, Rzx *r, uint8_t *data, size_t n,
                        uint32_t nframes)
{
    size_t off = 0;
    uint32_t i;

    r->pool = data;
    r->frames = calloc(nframes, sizeof *r->frames);
    if (!r->frames)
        return -1;
    for (i = 0; i < nframes; i++) {
        uint16_t fc, nin;
        if (off + 4 > n)
            return -1;
        fc = rd16b(data + off);
        nin = rd16b(data + off + 2);
        off += 4;
        r->frames[i].fetches = fc;
        if (nin == 0xFFFF) {                     /* repeat previous */
            if (i == 0)
                return -1;
            r->frames[i].nin = r->frames[i - 1].nin;
            r->frames[i].in_off = r->frames[i - 1].in_off;
        } else {
            if (off + nin > n)
                return -1;
            r->frames[i].nin = nin;
            r->frames[i].in_off = (uint32_t)off;
            off += nin;
        }
    }
    r->nframes = nframes;
    (void)m;
    return 0;
}

/* Write an embedded snapshot to a temp file and load it by extension. */
static int load_embedded_snap(Machine *m, const char ext[4],
                              const uint8_t *data, size_t n)
{
    char tmp[512];
    int fd;
    tmp_template(tmp, sizeof tmp);
    fd = mkstemp(tmp);
    FILE *f;
    int rc;

    if (fd < 0)
        return -1;
    f = fdopen(fd, "wb");
    if (!f || fwrite(data, 1, n, f) != n) {
        if (f) fclose(f);
        unlink(tmp);
        return -1;
    }
    fclose(f);
    if (!strncmp(ext, "z80", 3))
        rc = snapshot_load_z80(m, tmp);
    else if (!strncmp(ext, "szx", 3))
        rc = snapshot_load_szx(m, tmp);
    else if (!strncmp(ext, "sna", 3))
        rc = snapshot_load_sna(m, tmp);
    else {
        fprintf(stderr, "error: rzx: unsupported snapshot type '%.3s'\n",
                ext);
        rc = -1;
    }
    unlink(tmp);
    return rc;
}

int rzx_load(Machine *m, const char *path)
{
    FILE *f = fopen(path, "rb");
    long size;
    uint8_t *buf;
    long off = 10;
    int have_snap = 0, have_input = 0;
    Rzx *r = m->rzx;

    if (!f) {
        fprintf(stderr, "error: cannot open '%s'\n", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)size);
    if (!buf || (long)fread(buf, 1, (size_t)size, f) != size) {
        fprintf(stderr, "error: cannot read '%s'\n", path);
        fclose(f);
        free(buf);
        return -1;
    }
    fclose(f);

    if (size < 10 || memcmp(buf, "RZX!", 4) != 0) {
        fprintf(stderr, "error: '%s' is not an RZX file\n", path);
        free(buf);
        return -1;
    }

    while (off + 5 <= size) {
        uint8_t id = buf[off];
        uint32_t blen = rd32b(buf + off + 1);
        const uint8_t *pay = buf + off + 5;
        long paylen = (long)blen - 5;
        if (blen < 5 || (long)blen > size - off) {
            fprintf(stderr, "error: '%s': truncated RZX block\n", path);
            free(buf);
            return -1;
        }
        if (id == 0x30 && paylen >= 12) {        /* snapshot */
            uint32_t flags = rd32b(pay);
            const char *ext = (const char *)pay + 4;
            uint32_t usize = rd32b(pay + 8);
            if (flags & 1) {
                fprintf(stderr, "error: '%s': external snapshot refs not "
                        "supported\n", path);
                free(buf);
                return -1;
            }
            if (flags & 2) {                     /* zlib-compressed */
                uint8_t *un = malloc(usize ? usize : 1);
                if (!un || zlib_inflate(pay + 12, (size_t)(paylen - 12),
                                        un, usize) != (long)usize) {
                    fprintf(stderr, "error: '%s': bad compressed "
                            "snapshot\n", path);
                    free(un);
                    free(buf);
                    return -1;
                }
                have_snap = load_embedded_snap(m, ext, un, usize) == 0;
                free(un);
            } else {
                have_snap = load_embedded_snap(m, ext, pay + 12,
                                               (size_t)(paylen - 12)) == 0;
            }
            if (!have_snap) {
                free(buf);
                return -1;
            }
        } else if (id == 0x80 && paylen >= 13) { /* input recording */
            uint32_t nframes = rd32b(pay);
            uint32_t flags = rd32b(pay + 9);
            uint8_t *fdata;
            size_t fn;
            if (flags & 1) {
                fprintf(stderr, "error: '%s': protected (encrypted) RZX "
                        "not supported\n", path);
                free(buf);
                return -1;
            }
            if (flags & 2) {
                fdata = zlib_inflate_alloc(pay + 13,
                                           (size_t)(paylen - 13), &fn);
                if (!fdata) {
                    fprintf(stderr, "error: '%s': bad compressed input "
                            "block\n", path);
                    free(buf);
                    return -1;
                }
            } else {
                fn = (size_t)(paylen - 13);
                fdata = malloc(fn ? fn : 1);
                if (!fdata) {
                    free(buf);
                    return -1;
                }
                memcpy(fdata, pay + 13, fn);
            }
            if (parse_frames(m, r, fdata, fn, nframes) != 0) {
                fprintf(stderr, "error: '%s': malformed input frames\n",
                        path);
                free(buf);
                return -1;
            }
            have_input = 1;
        }
        off += (long)blen;
    }
    free(buf);

    if (!have_snap || !have_input) {
        fprintf(stderr, "error: '%s': missing %s block\n", path,
                have_snap ? "input" : "snapshot");
        return -1;
    }
    r->playing = 1;
    r->cur = 0;
    fprintf(stderr, "rzx: playing back %u frames from %s\n",
            r->nframes, path);
    return 0;
}

uint8_t rzx_in(Machine *m)
{
    Rzx *r = m->rzx;
    const RzxFrame *fr = &r->frames[r->cur];
    if (r->in_pos < fr->nin)
        return r->pool[fr->in_off + r->in_pos++];
    if (!r->desync) {
        r->desync = 1;
        fprintf(stderr, "warning: rzx: port-read underrun in frame %u "
                "(playback desynced)\n", r->cur);
    }
    return 0xFF;
}

/* One playback frame: fetch-count bounded instead of T-state bounded;
 * everything else mirrors machine_run_frame so contention, beam state
 * and interrupt acceptance evolve exactly as they did when recorded. */
void rzx_play_frame(Machine *m)
{
    Rzx *r = m->rzx;
    const RzxFrame *fr = &r->frames[r->cur];
    uint64_t base = m->cpu.fetches;
    int int_taken;

    m->render_pos = 0;
    r->in_pos = 0;
    int_taken = z80_int(&m->cpu, 0xFF);

    while (m->cpu.fetches - base < fr->fetches) {
        if (m->dbg)
            debug_step_hook(m);
        z80_step(&m->cpu);
        if (!int_taken &&
            m->cpu.tstates - m->frame_start_ts < HC91_INT_WINDOW)
            int_taken = z80_int(&m->cpu, 0xFF);
    }
    if (r->in_pos < fr->nin && !r->desync) {
        r->desync = 1;
        fprintf(stderr, "warning: rzx: %u unused port reads in frame %u "
                "(playback desynced)\n", fr->nin - r->in_pos, r->cur);
    }
    if (m->fb_live)
        video_beam_finish(m);
    m->frame_start_ts += HC91_FRAME_TSTATES;
    m->frame_counter++;

    if (++r->cur >= r->nframes) {
        r->playing = 0;
        fprintf(stderr, "rzx: playback finished (%u frames)\n",
                r->nframes);
    }
}

void rzx_free(Machine *m)
{
    Rzx *r = m->rzx;
    if (!r)
        return;
    free(r->frames);
    free(r->pool);
    free(r->rin);
    free(r->fdata);
    free(r->snap);
    memset(r, 0, sizeof *r);
}
