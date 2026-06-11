/* machine.h — ICE Felix HC-91 machine layer (48K ZX Spectrum clone).
 *
 * Z80A @ 3.5 MHz, 16K ROM at 0x0000, 48K RAM, ULA-compatible I/O,
 * trap-based instant tape loading, .sna/.z80 snapshots.
 */
#ifndef HC91_MACHINE_H
#define HC91_MACHINE_H

#include <stdint.h>
#include "z80.h"

#define HC91_FRAME_TSTATES 69888   /* 3.5 MHz / 50 Hz */
#define HC91_INT_WINDOW    32      /* ULA INT pulse length (T-states) */

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
 * of pulse durations (T-states); EAR toggles at each boundary. */
typedef struct TapePlayer {
    uint32_t *pulses;
    size_t npulses;
    size_t idx;
    uint64_t edge_ts;    /* T-state of the next EAR toggle */
    int ear;             /* current level 0/1 */
    int playing;
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

typedef struct Machine {
    Z80 cpu;
    uint8_t mem[65536];      /* 0x0000-0x3FFF ROM (write-protected) */
    uint8_t mem_cpm[0x4000]; /* low 16K RAM, paged over ROM by port 0x7E
                                (HC-91 CP/M mode); filled with HALT so a
                                bare boot into it parks deterministically */
    int ram_paged;           /* 1 = mem_cpm mapped at 0x0000-0x3FFF */
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
} Machine;

/* wav.c */
void beep_start(Machine *m, uint64_t now_ts);
void beep_edge(Machine *m, uint64_t now_ts, int new_level);
int  beep_save(Machine *m, const char *path, uint64_t now_ts);

/* machine.c */
int  machine_init(Machine *m, const char *rom_path);   /* 0 ok, -1 error */
int  machine_load_file(Machine *m, const char *path);  /* by extension */
void machine_run_frame(Machine *m);
uint8_t machine_peek(const Machine *m, uint16_t addr); /* no side effects */

/* video.c */
void video_render(const Machine *m, uint32_t *fb /* HC91_FB_W*HC91_FB_H */);
void video_screen_text(const Machine *m, char out[24][33]);
void video_beam_catchup(Machine *m);   /* paint up to current cpu T-state */
void video_beam_finish(Machine *m);    /* paint to frame end; fb_valid=1 */

/* tape.c */
int  tape_load(Machine *m, const char *path);   /* attach .tap/.tzx */
void tape_trap(Machine *m);                     /* LD-BYTES @0x0556 */
void tape_save_trap(Machine *m);                /* SA-BYTES @0x04C2 */
void tape_play_start(Machine *m);               /* press PLAY now */
int  tape_player_ear(Machine *m);               /* EAR level at cpu T */
void tape_free(Machine *m);

/* snapshot.c */
int  snapshot_load_sna(Machine *m, const char *path);
int  snapshot_load_z80(Machine *m, const char *path);
int  snapshot_save_sna(const Machine *m, const char *path);
int  snapshot_save_z80(const Machine *m, const char *path);
int  screen_save_scr(const Machine *m, const char *path);
int  screen_load_scr(Machine *m, const char *path);

/* keys.c */
void keys_apply(Machine *m, int frame);                /* set keyrows */
void keys_type(Machine *m, const char *text, int start_frame);
int  keys_raw(Machine *m, int frame, const char *names); /* 0 ok, -1 bad name */
int  keys_joy(Machine *m, int f0, int f1, const char *dirs); /* UDLRF */

/* png.c */
int  png_write(const char *path, const uint32_t *rgba, int w, int h);

#endif
