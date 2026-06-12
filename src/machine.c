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
    if (m->model == HC91_MODEL_128) {
        switch (addr >> 14) {
        case 0:  return ((m->port_7ffd & 0x10) ? m->rom1 : m->mem)[addr];
        case 1:  return m->ram128[5][addr & 0x3FFF];
        case 2:  return m->ram128[2][addr & 0x3FFF];
        default: return m->ram128[m->port_7ffd & 7][addr & 0x3FFF];
        }
    }
    if (m->have_boot) {                      /* HC-2000 config map */
        if (addr < 0x4000) {
            if (m->cfg_7e & 0x02)            /* D1: RAM bank 0 low */
                return m->mem_cpm[addr];
            if (m->if1_paged)
                return addr < 0x2000 ? m->rom_if1[addr]
                                     : m->if1_ram16[addr];
            return ((m->cfg_7e & 0x01) ? m->rom_boot : m->mem)[addr];
        }
        if (addr >= 0xC000 && addr < 0xE000 && m->cpm_page)
            return m->mem[addr | 0x2000];    /* CPM: A13 pulled high */
        if (addr >= 0xE000 && (m->cfg_7e & 0x02))
            return ((m->cfg_7e & 0x01) ? m->rom_boot : m->mem)
                   [0x2000 + (addr & 0x1FFF)];
        return m->mem[addr];
    }
    if (addr < 0x4000) {
        if (m->ram_paged & 2)
            return m->if1_ram16[addr];
        if (m->ram_paged & 1)
            return m->mem_cpm[addr];
        if (m->if1_paged)
            return addr < 0x2000 ? m->rom_if1[addr]
                                 : m->if1_ram16[addr];
    }
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
    if (m->model == HC91_MODEL_128) {
        uint8_t *p;
        uint32_t off = addr & 0x3FFF;
        switch (addr >> 14) {
        case 0:  return;                       /* ROM */
        case 1:  p = m->ram128[5]; break;
        case 2:  p = m->ram128[2]; break;
        default: p = m->ram128[m->port_7ffd & 7]; break;
        }
        if (m->fb_live && p == m->screen && off < 0x1B00 && p[off] != val)
            video_beam_catchup(m);
        p[off] = val;
        return;
    }
    if (m->have_boot) {                      /* HC-2000 config map */
        if (addr < 0x4000) {
            if (m->cfg_7e & 0x02)
                m->mem_cpm[addr] = val;      /* RAM bank 0 */
            else if (m->if1_paged && addr >= 0x2000)
                m->if1_ram16[addr] = val;
            return;                          /* else ROM */
        }
        if (addr >= 0xC000 && addr < 0xE000 && m->cpm_page)
            addr |= 0x2000;                  /* CPM: A13 pulled high */
        else if (addr >= 0xE000 && (m->cfg_7e & 0x02))
            return;                          /* ROM at 0xE000 */
        {
            uint32_t scr = (uint32_t)(addr - (m->screen - m->mem));
            if (m->fb_live && scr < 0x1B00 && m->mem[addr] != val)
                video_beam_catchup(m);
        }
        m->mem[addr] = val;
        return;
    }
    if (addr < 0x4000) {         /* ROM (ignore), CP/M RAM or IF1 RAM */
        if (m->ram_paged & 2)
            m->if1_ram16[addr] = val;
        else if (m->ram_paged & 1)
            m->mem_cpm[addr] = val;
        else if (m->if1_paged && addr >= 0x2000)
            m->if1_ram16[addr] = val;
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
    if ((addr & 0xC000) == 0x4000)
        return contention_delay(m);          /* bank 5 / 48K screen RAM */
    if (m->model == HC91_MODEL_128 && (addr >> 14) == 3
        && (m->port_7ffd & 1))
        return contention_delay(m);          /* odd bank paged at 0xC000 */
    return 0;
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
    /* HC-2000: the config latch reads back ((port & 0x81) == 0); the
     * ULA keyboard read stays on even ports with A7 = 1 (0xFE). */
    if (m->have_boot && (port & 0x81) == 0)
        return m->cfg_7e;
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
    /* AY register read at 0xFFFD (model 128). */
    if (m->model == HC91_MODEL_128 && (port & 0xC002) == 0xC000)
        return ay_read(&m->ay);
    /* HC disk interface (IF1): i8272 + control latch. */
    if (m->have_if1) {
        uint8_t lo = (uint8_t)(port & 0xFF);
        if (lo == 0x85)
            return fdc_status(&m->fdc);
        if (lo == 0x87)
            return fdc_data_read(&m->fdc);
        if ((lo & 0xFD) == 0x05)
            return fdc_sel_read(&m->fdc);
    }
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
                return m->screen[((y & 0xC0) << 5) | ((y & 7) << 8)
                                 | ((y & 0x38) << 2) | col];
            case 1: case 3:
                return m->screen[0x1800 + (y >> 3) * 32 + col];
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
    if (m->model == HC91_MODEL_128) {
        if ((port & 0x8002) == 0) {              /* 0x7FFD bank latch */
            if (!(m->port_7ffd & 0x20)) {        /* not locked */
                uint8_t *scr = m->ram128[(val & 0x08) ? 7 : 5];
                if (m->fb_live && scr != m->screen)
                    video_beam_catchup(m);       /* mid-frame flip */
                m->port_7ffd = val;
                m->screen = scr;
            }
        } else if ((port & 0xC002) == 0xC000) {  /* AY select */
            ay_select(&m->ay, val);
        } else if ((port & 0xC002) == 0x8000) {  /* AY data */
            beep_flush(m, m->cpu.tstates);       /* sample-accurate */
            ay_write(&m->ay, val);
        }
    }
    /* HC disk interface (IF1). */
    if (m->have_if1) {
        uint8_t lo = (uint8_t)(port & 0xFF);
        if (lo == 0x87)
            fdc_data_write(&m->fdc, val);
        else if ((lo & 0xFD) == 0x05)
            fdc_sel_write(&m->fdc, val);
    }
    if (m->have_boot) {
        /* HC-2000 config latch ((port & 0x81) == 0, canonically 0x7E)
         * and the CPM flip-flop (write to 0xC7 sets, 0xC5 clears). */
        if ((port & 0x81) == 0) {
            if (!m->cfg_locked) {
                uint8_t *scr = m->mem + ((val & 0x08) ? 0xC000 : 0x4000);
                if (m->fb_live && scr != m->screen)
                    video_beam_catchup(m);
                m->cfg_7e = val;
                m->cfg_locked = val & 0x04;
                m->screen = scr;
            }
        } else if ((port & 0xFF) == 0xC5 || (port & 0xFF) == 0xC7) {
            m->cpm_page = ((port & 0xFF) == 0xC7);
        }
    } else if (m->model == HC91_MODEL_48 && (port & 0xFF) == 0x7E) {
        /* HC-91 RAM paging at port 0x7E: bit 0 = the motherboard CP/M
         * bank (the ROM bootstrap at 0x386E does OUT (0x7E),1 and jumps
         * to 0); bit 1 = the disk interface's ext16k RAM (probed by HC
         * disk software with OUT (0x7E),2). Full low-byte decode. */
        m->ram_paged = val & (m->have_if1 ? 3 : 1);
    }
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

    m->screen = m->mem + 0x4000;

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

/* Switch to the HC-128 model: 8 banked 16K pages, second ROM (rom1_path,
 * or a duplicate of ROM 0 when NULL — only one HC-128 ROM image is
 * dumped), AY. Call right after machine_init. */
int machine_set_128(Machine *m, const char *rom1_path)
{
    m->model = HC91_MODEL_128;
    m->port_7ffd = 0;
    m->screen = m->ram128[5];
    ay_reset(&m->ay);
    if (rom1_path) {
        FILE *f = fopen(rom1_path, "rb");
        size_t n = 0;
        if (f) {
            n = fread(m->rom1, 1, 0x4000, f);
            fclose(f);
        }
        if (n != 0x4000) {
            fprintf(stderr, "error: cannot read 16K ROM '%s'\n", rom1_path);
            return -1;
        }
    } else {
        memcpy(m->rom1, m->mem, 0x4000);
    }
    return 0;
}

/* Load the HC-2000 CP/M ROM (the second half of the machine's 32K ROM
 * space, selected by config bit D0). */
int machine_set_boot(Machine *m, const char *rom_path)
{
    FILE *f = fopen(rom_path, "rb");
    size_t n;

    if (!f) {
        fprintf(stderr, "error: cannot open CP/M ROM '%s'\n", rom_path);
        return -1;
    }
    n = fread(m->rom_boot, 1, 0x4000, f);
    fclose(f);
    if (n != 0x4000) {
        fprintf(stderr, "error: CP/M ROM '%s' must be 16K\n", rom_path);
        return -1;
    }
    m->have_boot = 1;
    return 0;
}

/* Attach the HC "IF1" disk interface: a 16K shadow ROM (an 8K image is
 * mirrored into both halves) + the i8272 at ports 0x85/0x87/0x05-0x07. */
int machine_set_if1(Machine *m, const char *rom_path)
{
    FILE *f = fopen(rom_path, "rb");
    size_t n;

    if (!f) {
        fprintf(stderr, "error: cannot open IF1 ROM '%s'\n", rom_path);
        return -1;
    }
    n = fread(m->rom_if1, 1, 0x4000, f);
    fclose(f);
    if (n == 0x2000)
        memcpy(m->rom_if1 + 0x2000, m->rom_if1, 0x2000);
    else if (n != 0x4000) {
        fprintf(stderr, "error: IF1 ROM '%s' must be 8K or 16K\n",
                rom_path);
        return -1;
    }
    m->have_if1 = 1;
    m->if1_paged = 0;
    fdc_reset(&m->fdc);
    return 0;
}

/* ---- File dispatch by extension ---- */

int machine_load_file(Machine *m, const char *path)
{
    const char *dot = strrchr(path, '.');
    if (dot) {
        if (ext_eq(dot, ".tap") || ext_eq(dot, ".tzx")
            || ext_eq(dot, ".wav"))
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
            "(expected .tap/.tzx/.wav/.sna/.z80/.szx/.scr/.rzx)\n", path);
    return -1;
}

/* Warm reset (the RESET button): CPU registers and the I/O latches
 * return to power-on state; RAM contents, loaded media and the T-state
 * clock are kept (the running clock keeps audio and the frame phase
 * continuous across the reset). The FDC keeps its inserted disks. */
void machine_reset(Machine *m)
{
    uint64_t ts = m->cpu.tstates, fe = m->cpu.fetches;
    z80_reset(&m->cpu);
    m->cpu.tstates = ts;
    m->cpu.fetches = fe;

    m->ram_paged = 0;
    m->if1_paged = 0;
    m->cfg_7e = 0;
    m->cfg_locked = 0;
    m->cpm_page = 0;
    m->port_7ffd = 0;
    m->screen = (m->model == HC91_MODEL_128) ? m->ram128[5]
                                             : m->mem + 0x4000;
    ay_reset(&m->ay);
    m->border = 7;
    memset(m->keyrows, 0, sizeof(m->keyrows));
    m->kempston = 0;
    m->nkey_events = 0;
    m->njoy_events = 0;
    m->play_at_frame = -1;
    m->player.playing = 0;
    m->player.paused = 0;
    m->player.idx = 0;
    m->player.ear = 0;
    m->player.nextb = 1;
    m->player.hot = 0;
    m->player.win_reads = 0;
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
    /* (HC-2000 CP/M note: the ROM BIOS runs IM 2 with I=0xFE — the
     * vector table lives in the E000-FFFF boot-ROM overlay, all 0xFF
     * with the ISR address at its end, so the standard 0xFF bus byte
     * vectors into the ROM BIOS interrupt handler.) */
    int_taken = z80_int(&m->cpu, 0xFF);

    while (m->cpu.tstates < end) {
        uint16_t pc0 = m->cpu.pc.w;
        if (m->dbg)
            debug_step_hook(m);
        if (m->tape.attached && !m->real_tape && m->cpu.pc.w == 0x0556)
            tape_trap(m);
        if (m->save_tape && m->cpu.pc.w == 0x04C2)
            tape_save_trap(m);
        /* IF1 shadow ROM pages in on the hook addresses (only while a
         * ROM is mapped low — not in any RAM-low mode)... */
        if (m->have_if1 && !m->if1_paged
            && !(m->have_boot ? (m->cfg_7e & 0x02) : m->ram_paged)
            && (pc0 == 0x0008 || pc0 == 0x1708))
            m->if1_paged = 1;
        z80_step(&m->cpu);
        /* ...and out after executing the instruction at 0x0700 (which
         * is fetched from the IF1 ROM, like the real /ROMCS timing). */
        if (m->if1_paged && pc0 == 0x0700)
            m->if1_paged = 0;
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
