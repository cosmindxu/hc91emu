/* machine.h — ICE Felix HC-91 machine layer (48K ZX Spectrum clone).
 *
 * Z80A @ 3.5 MHz, 16K ROM at 0x0000, 48K RAM, ULA-compatible I/O,
 * trap-based instant tape loading, .sna/.z80 snapshots.
 */
#ifndef HC91_MACHINE_H
#define HC91_MACHINE_H

#include <stdint.h>
#include "z80.h"
#include "fdc.h"

#define HC91_FRAME_TSTATES 69888   /* 3.5 MHz / 50 Hz */
#define HC91_INT_WINDOW    32      /* ULA INT pulse length (T-states) */

/* Machine models: the 48K-class HC-85/90/91 differ only in ROM; the
 * HC-128 adds 128K RAM banked via port 0x7FFD and an AY-3-8912 at
 * 0xFFFD/0xBFFD (both confirmed by code in its ROM), keeping the
 * HC-91-derived 48K BASIC and ULA timing. */
enum { HC91_MODEL_48 = 0, HC91_MODEL_128 = 1 };

/* Framebuffer geometry: 256x192 paper + 32px L/R, 24px T/B border. */
#define HC91_FB_W 320
#define HC91_FB_H 240

/* ---- Tape (.tap) ---- */
typedef struct TapBlock {
    uint16_t len;        /* bytes incl. flag + checksum */
    uint8_t *data;
} TapBlock;

typedef struct Tape {
    TapBlock *blocks;
    int nblocks;
    int pos;             /* next block index */
    int attached;        /* 1 if a .tap is loaded */
} Tape;

/* Pulse-level tape player: the whole tape is compiled into a flat list
 * of pulse durations (T-states); EAR toggles at each boundary.
 *
 * Multi-load support: block starts are recorded as boundaries. At a
 * boundary the player pauses unless the CPU is actively polling the
 * EAR port (a loader reads it every ~50-200 T; keyboard scanning stays
 * far below the hot threshold), and resumes when hot polling returns —
 * so "stop the tape / press a key" schemes work. TZX stop markers
 * (0x20 len 0, 0x2A) pause unconditionally. */
typedef struct TapePlayer {
    uint32_t *pulses;
    size_t npulses;
    size_t idx;
    uint64_t edge_ts;    /* T-state of the next EAR toggle */
    int ear;             /* current level 0/1 */
    int playing;
    size_t *bounds;      /* pulse indices where blocks start */
    uint8_t *bstop;      /* 1 = unconditional stop marker */
    size_t nbounds, nextb;
    int paused;
    uint64_t win_start;  /* EAR-poll rate window */
    int win_reads, hot;
} TapePlayer;

/* ---- Scheduled key events ---- */
#define HC91_MAX_KEY_EVENTS 4096
#define HC91_MAX_JOY_EVENTS 64

typedef struct KeyEvent {
    int frame_start;     /* inclusive */
    int frame_end;       /* exclusive */
    uint8_t row;         /* 0..7 */
    uint8_t mask;        /* bits to press in that row */
} KeyEvent;

/* Kempston state held for a frame range (bit0=R 1=L 2=D 3=U 4=fire). */
typedef struct JoyEvent {
    int frame_start, frame_end;
    uint8_t mask;
} JoyEvent;

enum { JOY_KEMPSTON = 0, JOY_SINCLAIR = 1, JOY_CURSOR = 2 };

/* ---- AY-3-8912 (HC-128; ay.c) ---- */
typedef struct Ay {
    uint8_t reg[16];
    uint8_t sel;             /* selected register */
    /* synthesis state (stepped at chip-clock/16 from wav.c) */
    double frac;             /* fractional chip ticks per output sample */
    uint32_t tone_cnt[3];
    int tone_out[3];
    uint32_t noise_cnt;
    uint32_t lfsr;
    int noise_out;
    uint32_t env_cnt;
    int env_pos;             /* 0..63 position in the shape pattern */
    uint8_t env_vol;
} Ay;

/* ---- Beeper audio capture (wav.c) ---- */
typedef struct Beeper {
    int enabled;
    int level;               /* current speaker level 0/1 */
    uint64_t start_ts;       /* tstates when recording began */
    double last_td;          /* synthesis advanced up to here (fractional T) */
    double pos;              /* fill position within current sample (0..TPS) */
    double acc;              /* level integral within current sample */
    int16_t *buf;
    size_t n, cap;
} Beeper;

/* ---- Machine ---- */
struct Debugger;                 /* debug.h (optional monitor) */
struct Rzx;                      /* rzx.h (input record/playback) */

typedef struct Machine {
    Z80 cpu;
    uint8_t mem[65536];      /* model 48: full map, 16K ROM write-prot. */
    uint8_t mem_cpm[0x4000]; /* low 16K RAM, paged over ROM by port 0x7E
                                (HC-91 CP/M mode); filled with HALT so a
                                bare boot into it parks deterministically */
    int ram_paged;           /* port 0x7E bits: 1 = mem_cpm mapped at
                                0x0000-0x3FFF (HC-91 CP/M bank); 2 = the
                                IF1 interface's 16K RAM (HC-2000 ext16k,
                                only with have_if1) */

    /* model 128 (HC-128): mem[0..0x3FFF] = ROM 0, rom1 = ROM 1, eight
     * 16K banks; 0x4000 = bank 5, 0x8000 = bank 2, 0xC000 = bank 0-7
     * per port_7ffd bits 0-2; bit 3 = shadow screen (bank 7), bit 4 =
     * ROM select, bit 5 = lock. Odd banks are contended. */
    int model;               /* HC91_MODEL_* */
    uint8_t ram128[8][0x4000];
    uint8_t rom1[0x4000];
    uint8_t port_7ffd;
    uint8_t *screen;         /* active display bank (also set for 48K) */
    Ay ay;

    /* HC-2000 "IF1" disk interface: an 8K shadow ROM (0x0000-0x1FFF)
     * paged in when PC hits 0x0008/0x1708 and out after the instruction
     * at 0x0700 (only while no RAM is paged over the ROM); the
     * interface carries 16K RAM ("ext16k") — its 0x2000-0x3FFF half is
     * visible as the shadow ROM's workspace window, and the whole chip
     * pages over 0x0000-0x3FFF via port 0x7E bit 1 (probed exactly that
     * way by HC disk software); and an i8272 FDC at ports 0x85/0x87
     * with a control latch at 0x05/0x07. */
    uint8_t rom_if1[0x4000];
    uint8_t if1_ram16[0x4000];
    int have_if1;
    int if1_paged;
    Fdc fdc;

    /* HC-2000 system configuration (semantics confirmed against Alex
     * Badea's FUSE hc2000 machine): port 0x7E (decode (port&0x81)==0,
     * readable) is a latch — D0 selects the ROM (0 = BASIC = mem[],
     * 1 = CP/M = rom_boot), D1 moves the ROM window from 0x0000 to
     * 0xE000-0xFFFF (upper ROM half) with RAM bank 0 (mem_cpm) mapped
     * low, D2 locks the latch, D3 relocates the video generator to
     * 0xC000. A separate "CPM" flip-flop (set by writing port 0xC7,
     * cleared by 0xC5 or reset) pulls A13 high for CPU accesses to
     * 0xC000-0xDFFF, exposing the 0xE000 RAM there while the ROM
     * occupies 0xE000. */
    uint8_t rom_boot[0x4000];
    int have_boot;
    uint8_t cfg_7e;
    int cfg_locked;
    int cpm_page;
    uint8_t border;          /* last OUT to ULA, bits 0-2 */
    uint8_t beeper;          /* last beeper bit (OUT bit 4) */
    uint32_t frame_counter;
    uint64_t frame_start_ts; /* cpu.tstates at start of current frame */
    int floating_bus;        /* 1 = unattached port reads float (default) */
    uint8_t keyrows[8];      /* bit set = key pressed (active-low on read) */
    Tape tape;
    TapePlayer player;
    int real_tape;           /* 1 = no LD-BYTES trap; load via player */
    int play_at_frame;       /* frame to press PLAY at, -1 = never */
    const char *save_tape;   /* append SA-BYTES output here (SAVE trap) */
    const char *tape_cur;    /* path of the attached tape */
    const char *tape_next;   /* the other cassette side (--tape-b);
                                tape_swap() exchanges the two */

    KeyEvent key_events[HC91_MAX_KEY_EVENTS];
    int nkey_events;

    int kempston_enabled;    /* 1 = port 0x1F attached (else it floats) */
    uint8_t kempston;        /* current Kempston byte */
    int joy_type;            /* JOY_* — where --joy events are routed */
    JoyEvent joy_events[HC91_MAX_JOY_EVENTS];
    int njoy_events;

    Beeper beep;

    /* Beam renderer (video.c): the frame is painted incrementally; border
     * and display-RAM writes catch the painter up to the current T-state
     * first, so mid-frame changes land at the true beam position. */
    uint32_t fb[HC91_FB_W * HC91_FB_H];
    uint32_t render_pos;     /* frame T painted so far */
    int fb_live;             /* 1 = paint this frame while executing */
    int fb_valid;            /* fb holds a completed beam-painted frame */

    struct Debugger *dbg;    /* attached monitor, or NULL (debug.c) */
    struct Rzx *rzx;         /* input record/playback state (rzx.c) */
} Machine;

/* wav.c */
void beep_start(Machine *m, uint64_t now_ts);
void beep_edge(Machine *m, uint64_t now_ts, int new_level);
void beep_flush(Machine *m, uint64_t now_ts);   /* synth to now, no edge */
int  beep_save(Machine *m, const char *path, uint64_t now_ts);

/* machine.c */
int  machine_init(Machine *m, const char *rom_path);   /* 0 ok, -1 error */
int  machine_set_128(Machine *m, const char *rom1_path); /* NULL = dup */
int  machine_set_if1(Machine *m, const char *rom_path); /* 8K or 16K */
int  machine_set_boot(Machine *m, const char *rom_path); /* CP/M boot */
int  machine_load_file(Machine *m, const char *path);  /* by extension */
void machine_run_frame(Machine *m);
uint8_t machine_peek(const Machine *m, uint16_t addr); /* no side effects */

/* ay.c — AY-3-8912 (model 128) */
void ay_reset(Ay *ay);
void ay_select(Ay *ay, uint8_t v);
void ay_write(Ay *ay, uint8_t v);
uint8_t ay_read(const Ay *ay);
int  ay_sample(Ay *ay);          /* one 44.1 kHz sample, signed mix */

/* video.c */
void video_render(const Machine *m, uint32_t *fb /* HC91_FB_W*HC91_FB_H */);
void video_screen_text(const Machine *m, char out[24][33]);
void video_beam_catchup(Machine *m);   /* paint up to current cpu T-state */
void video_beam_finish(Machine *m);    /* paint to frame end; fb_valid=1 */

/* tape.c */
int  tape_load(Machine *m, const char *path);   /* attach .tap/.tzx */
int  tape_swap(Machine *m, const char *path);   /* insert other side */
void tape_trap(Machine *m);                     /* LD-BYTES @0x0556 */
void tape_save_trap(Machine *m);                /* SA-BYTES @0x04C2 */
void tape_play_start(Machine *m);               /* press PLAY now */
int  tape_player_ear(Machine *m);               /* EAR level at cpu T */
void tape_free(Machine *m);

/* snapshot.c */
int  snapshot_load_sna(Machine *m, const char *path);
int  snapshot_load_z80(Machine *m, const char *path);
int  snapshot_load_szx(Machine *m, const char *path);
int  snapshot_save_sna(const Machine *m, const char *path);
int  snapshot_save_z80(const Machine *m, const char *path);
int  snapshot_save_szx(const Machine *m, const char *path);
int  screen_save_scr(const Machine *m, const char *path);
int  screen_load_scr(Machine *m, const char *path);

/* keys.c */
void keys_apply(Machine *m, int frame);                /* set keyrows */
void keys_type(Machine *m, const char *text, int start_frame);
int  keys_raw(Machine *m, int frame, const char *names); /* 0 ok, -1 bad name */
int  keys_joy(Machine *m, int f0, int f1, const char *dirs); /* UDLRF */
int  keys_name_pos(const char *name, int len, int *row, int *bit);

/* sdl.c — optional SDL2 frontend (dlopen'd at runtime; no build deps).
 * Runs the interactive loop; max_frames > 0 auto-quits (for tests).
 * Returns 0 on clean exit, -1 if SDL2 is unavailable. */
int  sdl_run(Machine *m, int max_frames);

/* png.c */
int  png_write(const char *path, const uint32_t *rgba, int w, int h);

#endif
