/* video.c — HC-91/Spectrum screen renderer + ROM-font OCR. */
#include <string.h>
#include "machine.h"

#define BORDER_X 32
#define BORDER_Y 24
#define LEVEL_NORMAL 0xD7
#define LEVEL_BRIGHT 0xFF

/* Spectrum color index: bit0=blue, bit1=red, bit2=green.
 * Pixel format: bytes R,G,B,A in memory (little-endian 0xAABBGGRR). */
static uint32_t spec_color(int idx, int bright)
{
    uint32_t lev = bright ? LEVEL_BRIGHT : LEVEL_NORMAL;
    uint32_t r = (idx & 2) ? lev : 0;
    uint32_t g = (idx & 4) ? lev : 0;
    uint32_t b = (idx & 1) ? lev : 0;
    return r | (g << 8) | (b << 16) | 0xFF000000u;
}

/* Paint one 8x1-px character cell column at display line y. */
static void paint_cell(const Machine *m, uint32_t *dst, int y, int col)
{
    uint16_t paddr = (uint16_t)(0x4000 | ((y & 0xC0) << 5) |
                                ((y & 7) << 8) | ((y & 0x38) << 2) | col);
    uint8_t bits = m->mem[paddr];
    uint8_t attr = m->mem[0x5800 + (y >> 3) * 32 + col];
    int ink = attr & 7;
    int paper = (attr >> 3) & 7;
    int bright = (attr >> 6) & 1;
    uint32_t cink, cpaper;
    int b;

    if ((attr & 0x80) && (m->frame_counter & 16)) {
        int t = ink; ink = paper; paper = t;
    }
    cink = spec_color(ink, bright);
    cpaper = spec_color(paper, bright);
    for (b = 0; b < 8; b++)
        dst[b] = (bits & (0x80 >> b)) ? cink : cpaper;
}

void video_render(const Machine *m, uint32_t *fb)
{
    uint32_t border;
    int x, y;

    if (m->fb_valid) {                 /* beam-painted frame available */
        memcpy(fb, m->fb, sizeof m->fb);
        return;
    }

    /* fallback: render whole frame from current memory at once */
    border = spec_color(m->border & 7, 0);
    for (y = 0; y < HC91_FB_H; y++)
        for (x = 0; x < HC91_FB_W; x++)
            fb[y * HC91_FB_W + x] = border;

    for (y = 0; y < 192; y++) {
        uint32_t *row = fb + (y + BORDER_Y) * HC91_FB_W + BORDER_X;
        int col;
        for (col = 0; col < 32; col++)
            paint_cell(m, row + col * 8, y, col);
    }
}

/* ---- Beam-accurate incremental painter ----
 *
 * Frame geometry (T-states): 312 lines x 224 T; lines 40..279 map to fb
 * rows 0..239 (display = lines 64..255). Within a line:
 *   lt   0..127  display fetch / centre 256 px  (x = 32 + 2*lt)
 *   lt 128..143  right border 32 px             (x = 32 + 2*lt)
 *   lt 144..207  blanking/retrace (nothing visible)
 *   lt 208..223  left border 32 px of the NEXT row (x = 2*(lt-208))
 * Display bytes are sampled at the ULA fetch slot: each 8-T block k
 * (lt = 8k) latches pixel+attr for cells 2k and 2k+1 -> paint 16 px at
 * once at block start. CPU writes to contended RAM can only complete in
 * the idle slots (6,7 mod 8), so "paint blocks whose start was crossed"
 * gives exact write-race semantics. The border color is constant within
 * a catch-up span because border OUTs trigger a catch-up first. */
static void paint_span(Machine *m, uint32_t from, uint32_t to)
{
    uint32_t border = spec_color(m->border & 7, 0);

    while (from < to) {
        uint32_t line = from / 224;
        uint32_t lt = from % 224;
        uint32_t seg = (line + 1) * 224;
        uint32_t end_lt, i;
        int row, disp;

        if (seg > to) seg = to;
        end_lt = lt + (seg - from);
        from = seg;

        if (line < 39 || line >= 280)
            continue;
        row = (int)line - 40;
        disp = (line >= 64 && line < 256);

        if (row >= 0) {
            if (disp) {
                /* blocks whose fetch slot 8k lies in [lt, end_lt) */
                uint32_t k0 = (lt + 7) >> 3, k1 = (end_lt + 7) >> 3;
                uint32_t *dst = m->fb + row * HC91_FB_W + BORDER_X;
                if (k1 > 16) k1 = 16;
                for (i = k0; i < k1; i++) {
                    paint_cell(m, dst + i * 16,     (int)line - 64, (int)i * 2);
                    paint_cell(m, dst + i * 16 + 8, (int)line - 64, (int)i * 2 + 1);
                }
                i = (lt < 128) ? 128 : lt;       /* right border */
            } else {
                i = lt;                          /* whole strip is border */
            }
            for (; i < end_lt && i < 144; i++) {
                m->fb[row * HC91_FB_W + 32 + 2 * i] = border;
                m->fb[row * HC91_FB_W + 33 + 2 * i] = border;
            }
        }
        if (row + 1 < 240) {                     /* left border of next row */
            for (i = (lt < 208) ? 208 : lt; i < end_lt; i++) {
                m->fb[(row + 1) * HC91_FB_W + 2 * (i - 208)] = border;
                m->fb[(row + 1) * HC91_FB_W + 2 * (i - 208) + 1] = border;
            }
        }
    }
}

void video_beam_catchup(Machine *m)
{
    uint64_t t = m->cpu.tstates - m->frame_start_ts;
    uint32_t target = (t > HC91_FRAME_TSTATES) ? HC91_FRAME_TSTATES
                                               : (uint32_t)t;
    if (target > m->render_pos) {
        paint_span(m, m->render_pos, target);
        m->render_pos = target;
    }
}

void video_beam_finish(Machine *m)
{
    if (m->render_pos < HC91_FRAME_TSTATES)
        paint_span(m, m->render_pos, HC91_FRAME_TSTATES);
    m->render_pos = HC91_FRAME_TSTATES;
    m->fb_valid = 1;
}

/* OCR: match each 8x8 cell against the ROM font (chars 32..127 at ROM
 * offset 0x3D00), direct or inverted. */
void video_screen_text(const Machine *m, char out[24][33])
{
    int row, col;
    for (row = 0; row < 24; row++) {
        for (col = 0; col < 32; col++) {
            uint8_t cell[8];
            int line, c, allzero = 1;
            char ch = '?';

            for (line = 0; line < 8; line++) {
                uint16_t paddr = (uint16_t)(0x4000 |
                                            ((row & 0x18) << 8) |
                                            (line << 8) |
                                            ((row & 7) << 5) | col);
                cell[line] = m->mem[paddr];
                if (cell[line])
                    allzero = 0;
            }
            if (allzero) {
                out[row][col] = ' ';
                continue;
            }
            for (c = 32; c < 128 && ch == '?'; c++) {
                const uint8_t *g = &m->mem[0x3D00 + (c - 32) * 8];
                int direct = 1, inverted = 1;
                for (line = 0; line < 8; line++) {
                    if (cell[line] != g[line])
                        direct = 0;
                    if (cell[line] != (uint8_t)(g[line] ^ 0xFF))
                        inverted = 0;
                }
                if (direct || inverted)
                    ch = (char)c;
            }
            out[row][col] = ch;
        }
        out[row][32] = '\0';
    }
}
