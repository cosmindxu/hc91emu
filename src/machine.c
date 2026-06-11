/* machine.c — HC-91 machine core: memory/IO bus, frame loop, file dispatch. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "machine.h"
#include "debug.h"
#include "rzx.h"

static int ext_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++; b++;
    }
    return *a == *b;
}

/* ---- Bus callbacks ---- */

uint8_t machine_peek(const Machine *m, uint16_t addr)
{
    if (addr < 0x4000 && m->ram_paged)
        return m->mem_cpm[addr];
    return m->mem[addr];
}

static uint8_t bus_mem_read(void *ctx, uint16_t addr)
{
    Machine *m = (Machine *)ctx;
    uint8_t v = machine_peek(m, addr);
    if (m->dbg)
        debug_note_mem_read(m, addr, v);
    return v;
}

static void bus_mem_write(void *ctx, uint16_t addr, uint8_t val)
{
    Machine *m = (Machine *)ctx;
    if (m->dbg)
        debug_note_mem_write(m, addr, val);
    if (addr < 0x4000) {         /* ROM (ignore) or paged CP/M RAM */
        if (m->ram_paged)
            m->mem_cpm[addr] = val;
        return;
    }
    if (m->fb_live && addr < 0x5B00 && m->mem[addr] != val)
        video_beam_catchup(m);   /* paint up to the beam before changing */
    m->mem[addr] = val;
}

/* ULA memory contention: while fetching display data the ULA stalls the
 * CPU per the canonical 6,5,4,3,2,1,0,0 pattern, starting at T=14335
 * after the frame interrupt, for the first 128 T of each of the 192
 * display lines. */
static int contention_delay(const Machine *m)
{
    int64_t t = (int64_t)(m->cpu.tstates - m->frame_start_ts) - 14335;
    int lt;
    if (t < 0 || t >= 192 * 224) return 0;
    lt = (int)(t % 224);
    if (lt >= 128) return 0;
    switch (lt & 7) {
    case 0: return 6;
    case 1: return 5;
    case 2: return 4;
    case 3: return 3;
    case 4: return 2;
    case 5: return 1;
    default: return 0;
    }
}

static int bus_mem_contend(void *ctx, uint16_t addr)
{
    Machine *m = (Machine *)ctx;
    return ((addr & 0xC000) == 0x4000) ? contention_delay(m) : 0;
}

/* I/O contention per the documented table (high byte 0x40-0x7F is a
 * "contended port"; low bit selects the ULA):
 *   hi no,  ULA:  N:1, C:3
 *   hi no,  ext:  N:4
 *   hi yes, ULA:  C:1, C:3
 *   hi yes, ext:  C:1, C:1, C:1, C:1
 */
static int bus_io_contend_early(void *ctx, uint16_t port)
{
    Machine *m = (Machine *)ctx;
    return ((port & 0xC000) == 0x4000) ? contention_delay(m) : 0;
}

static int bus_io_contend_late(void *ctx, uint16_t port)
{
    Machine *m = (Machine *)ctx;
    if ((port & 1) == 0)
        return contention_delay(m);              /* ULA: C:3 */
    if ((port & 0xC000) == 0x4000) {
        /* contended non-ULA: three more individually contended 1T slots;
         * evaluate each at its simulated time */
        uint64_t save = m->cpu.tstates;
        int d2, d3, d4;
        d2 = contention_delay(m); m->cpu.tstates += (uint64_t)(d2 + 1);
        d3 = contention_delay(m); m->cpu.tstates += (uint64_t)(d3 + 1);
        d4 = contention_delay(m);
        m->cpu.tstates = save;
        return d2 + d3 + d4;
    }
    return 0;
}

static uint8_t bus_io_read_raw(Machine *m, uint16_t port)
{
    if ((port & 1) == 0) {
        /* ULA: bits 0-4 keyboard (active low), bit 6 EAR. While the tape
         * player runs, EAR carries the tape signal; otherwise it follows
         * the last OUT's speaker bit (Issue-3 behavior; z80test expects
         * 0xBF after OUT xx,0). Bits 5 and 7 read as 1. */
        uint8_t hi = (uint8_t)(port >> 8);
        uint8_t keys = 0x1F;
        uint8_t ear = m->player.playing
                          ? (tape_player_ear(m) ? 0x40 : 0x00)
                          : (m->beeper ? 0x40 : 0x00);
        int r;
        for (r = 0; r < 8; r++)
            if (!(hi & (1u << r)))
                keys &= (uint8_t)(~m->keyrows[r]);
        return (uint8_t)(0xA0 | ear | (keys & 0x1F));
    }
    /* Kempston interface (when attached) decodes A7-A5 = 0. */
    if (m->kempston_enabled && (port & 0x00E0) == 0)
        return m->kempston;
    /* Unattached port: floating bus. While the ULA fetches display data
     * it leaves the byte on the bus; idle/border periods read 0xFF.
     * Frame layout: 224 T per line, lines 64-255 visible; within a line
     * the first 128 T fetch the 32 column pairs in 8-T blocks as
     * pixel,attr,pixel+1,attr+1,idle×4 (used by e.g. Arkanoid for beam
     * synchronization). */
    if (m->floating_bus) {
        uint64_t t = m->cpu.tstates - m->frame_start_ts;
        uint32_t line = (uint32_t)(t / 224), lt = (uint32_t)(t % 224);
        if (line >= 64 && line < 256 && lt < 128) {
            uint32_t y = line - 64;
            uint32_t col = (lt >> 3) * 2 + ((lt & 7) >= 2 ? 1 : 0);
            switch (lt & 7) {
            case 0: case 2:
                return m->mem[0x4000 | ((y & 0xC0) << 5) | ((y & 7) << 8)
                              | ((y & 0x38) << 2) | col];
            case 1: case 3:
                return m->mem[0x5800 + (y >> 3) * 32 + col];
            default:
                break;
            }
        }
    }
    return 0xFF;
}

static uint8_t bus_io_read(void *ctx, uint16_t port)
{
    Machine *m = (Machine *)ctx;
    uint8_t v;
    if (m->rzx && rzx_playing(m))
        v = rzx_in(m);              /* playback: feed the recording */
    else
        v = bus_io_read_raw(m, port);
    if (m->rzx)
        rzx_log_in(m, v);           /* no-op unless recording */
    if (m->dbg)
        debug_note_io(m, port, v, 0);
    return v;
}

static void bus_io_write(void *ctx, uint16_t port, uint8_t val)
{
    Machine *m = (Machine *)ctx;
    if (m->dbg)
        debug_note_io(m, port, val, 1);
    /* HC-91 CP/M paging: the ROM bootstrap at 0x386E does OUT (0x7E),1
     * and jumps to 0 expecting RAM there (64K machine). Full low-byte
     * decode; bit 0 = RAM over ROM. The ULA also sees this even port. */
    if ((port & 0xFF) == 0x7E)
        m->ram_paged = val & 1;
    if ((port & 1) == 0) {
        if (m->fb_live && ((val ^ m->border) & 7))
            video_beam_catchup(m);
        m->border = val & 7;
        beep_edge(m, m->cpu.tstates, (val >> 4) & 1);
        m->beeper = (val >> 4) & 1;
    }
}

/* ---- Init ---- */

int machine_init(Machine *m, const char *rom_path)
{
    FILE *f;
    size_t n;

    memset(m, 0, sizeof(*m));
    m->floating_bus = 1;
    m->play_at_frame = -1;
    memset(m->mem_cpm, 0x76, sizeof(m->mem_cpm));   /* HALTs (see .h) */

    f = fopen(rom_path, "rb");
    if (!f) {
        fprintf(stderr, "error: cannot open ROM file '%s'\n", rom_path);
        return -1;
    }
    n = fread(m->mem, 1, 0x4000, f);
    fclose(f);
    if (n != 0x4000) {
        fprintf(stderr, "error: ROM '%s' too short (%lu bytes, need 16384)\n",
                rom_path, (unsigned long)n);
        return -1;
    }

    m->cpu.ctx = m;
    m->cpu.mem_read = bus_mem_read;
    m->cpu.mem_write = bus_mem_write;
    m->cpu.io_read = bus_io_read;
    m->cpu.io_write = bus_io_write;
    m->cpu.mem_contend = bus_mem_contend;
    m->cpu.io_contend_early = bus_io_contend_early;
    m->cpu.io_contend_late = bus_io_contend_late;
    z80_reset(&m->cpu);
    m->border = 7;
    return 0;
}

/* ---- File dispatch by extension ---- */

int machine_load_file(Machine *m, const char *path)
{
    const char *dot = strrchr(path, '.');
    if (dot) {
        if (ext_eq(dot, ".tap") || ext_eq(dot, ".tzx"))
            return tape_load(m, path);
        if (ext_eq(dot, ".sna"))
            return snapshot_load_sna(m, path);
        if (ext_eq(dot, ".z80"))
            return snapshot_load_z80(m, path);
        if (ext_eq(dot, ".szx"))
            return snapshot_load_szx(m, path);
        if (ext_eq(dot, ".scr"))
            return screen_load_scr(m, path);
        if (ext_eq(dot, ".rzx"))
            return rzx_load(m, path);
    }
    fprintf(stderr, "error: '%s': unknown file type "
            "(expected .tap/.tzx/.sna/.z80/.szx/.scr/.rzx)\n", path);
    return -1;
}

/* ---- Frame loop ---- */

void machine_run_frame(Machine *m)
{
    /* Drift-free: frame boundaries are exactly HC91_FRAME_TSTATES apart;
     * instruction overshoot carries into the next frame (this keeps the
     * contention/floating-bus phase locked to the interrupt). */
    uint64_t end = m->frame_start_ts + HC91_FRAME_TSTATES;
    int int_taken;

    if (m->rzx && rzx_playing(m)) {      /* fetch-count bounded replay */
        rzx_play_frame(m);
        return;
    }

    m->render_pos = 0;                   /* beam to top of frame */
    if (m->rzx)
        rzx_frame_begin(m);
    if (m->play_at_frame >= 0 && (int)m->frame_counter == m->play_at_frame)
        tape_play_start(m);
    int_taken = z80_int(&m->cpu, 0xFF);

    while (m->cpu.tstates < end) {
        if (m->dbg)
            debug_step_hook(m);
        if (m->tape.attached && !m->real_tape && m->cpu.pc.w == 0x0556)
            tape_trap(m);
        if (m->save_tape && m->cpu.pc.w == 0x04C2)
            tape_save_trap(m);
        z80_step(&m->cpu);
        if (!int_taken && m->cpu.tstates - m->frame_start_ts < HC91_INT_WINDOW)
            int_taken = z80_int(&m->cpu, 0xFF);
    }
    if (m->rzx)
        rzx_frame_end(m);
    if (m->fb_live)
        video_beam_finish(m);
    m->frame_start_ts = end;
    m->frame_counter++;
}
