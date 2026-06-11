/* main.c — HC-91 emulator CLI driver. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "machine.h"
#include "debug.h"
#include "rzx.h"

#define MAX_TYPE_OPTS 16
#define MAX_KEYS_OPTS 16

static void usage(const char *prog)
{
    fprintf(stderr,
        "usage: %s [options] [file.tap/.tzx/.sna/.z80/.szx/.scr/.rzx]\n"
        "  --rom FILE        ROM image (default: roms/hc91.rom)\n"
        "  --frames N        frames to run (default 300)\n"
        "  --screenshot F    write PNG of final frame\n"
        "  --text            print 24x32 OCR text of final screen\n"
        "  --type \"S@F\"      type string S starting at frame F (repeatable)\n"
        "  --keys \"F:NAMES\"  raw key chord (e.g. \"50:CAPS+1\") at frame F\n"
        "  --autoload        type j\"\"<enter> at frame 250 (for .tap files)\n"
        "  --turbo           accepted for compatibility (no pacing anyway)\n"
        "  --no-floating-bus unattached ports read 0xFF instead of floating\n"
        "  --wav FILE        record beeper audio to a 44.1kHz mono WAV\n"
        "  --fb-dump FILE    write final frame as raw 320x240 RGBA\n"
        "  --save-sna FILE   save .sna snapshot after the run\n"
        "  --save-z80 FILE   save .z80 (v2) snapshot after the run\n"
        "  --save-szx FILE   save .szx (zx-state) snapshot after the run\n"
        "  --save-scr FILE   save raw 6912-byte screen after the run\n"
        "  --rzx-record FILE record inputs to .rzx (replays with FILE.rzx)\n"
        "  --real-tape       load via the pulse player (no ROM trap)\n"
        "  --play-at N       press PLAY at frame N (default 320 w/ autoload)\n"
        "  --save-tape FILE  capture SAVE (SA-BYTES) output as .tap\n"
        "  --kempston        attach Kempston interface (port 0x1F)\n"
        "  --joy \"F-G:DIRS\"  hold joystick DIRS (U/D/L/R/F) frames F..G\n"
        "  --joy-type T      kempston (default) | sinclair | cursor\n"
        "  --trace-frames    print frame/PC every 50 frames to stderr\n"
        "  --sdl             interactive SDL2 window (50 Hz, live audio;\n"
        "                    Shift=CAPS Ctrl=SYM Tab=turbo F5=pause F10=quit)\n"
        "  --sdl-frames N    auto-quit the SDL session after N frames\n"
        "debugger (addresses/ports in hex):\n"
        "  --monitor         stop in the monitor before the first instr\n"
        "  --break ADDR      PC breakpoint (repeatable)\n"
        "  --watch ADDR      memory write watchpoint (repeatable)\n"
        "  --rwatch ADDR     memory read watchpoint (repeatable)\n"
        "  --pwatch PORT     I/O watchpoint, <=FF matches low byte\n"
        "  --debug \"C;C;..\"  scripted monitor commands (else stdin)\n"
        "  --trace FILE      per-instruction trace to FILE\n",
        prog);
}

/* Hex u16 for --break & friends: optional $ or 0x prefix. */
static int parse_hex16(const char *s, uint16_t *out)
{
    char *end;
    unsigned long v;
    if (s[0] == '$') s++;
    else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    if (!*s) return -1;
    v = strtoul(s, &end, 16);
    if (*end || v > 0xFFFF) return -1;
    *out = (uint16_t)v;
    return 0;
}

/* Convert literal "\n" escapes to real newlines, in place (shrinks). */
static void unescape_newlines(char *s)
{
    char *r = s, *w = s;
    while (*r) {
        if (r[0] == '\\' && r[1] == 'n') {
            *w++ = '\n';
            r += 2;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

int main(int argc, char **argv)
{
    static Machine machine;       /* large: keep off the stack */
    Machine *m = &machine;
    const char *rom_path = "roms/hc91.rom";
    const char *file_path = NULL;
    const char *screenshot_path = NULL;
    const char *wav_path = NULL;
    const char *fbdump_path = NULL;
    const char *save_sna = NULL, *save_z80 = NULL, *save_scr = NULL;
    const char *save_szx = NULL, *rzx_record = NULL;
    static Rzx rzx_state;
    const char *save_tape = NULL;
    const char *joy_opts[MAX_KEYS_OPTS];
    const char *joy_type = "kempston";
    int njoy = 0, kempston = 0;
    int real_tape = 0, play_at = -1;
    static Debugger dbg;
    const char *dbg_script = NULL, *trace_path = NULL;
    uint16_t dbg_bp[DBG_MAX_BP], dbg_ww[DBG_MAX_BP],
             dbg_rw[DBG_MAX_BP], dbg_pw[DBG_MAX_BP];
    int n_bp = 0, n_ww = 0, n_rw = 0, n_pw = 0, monitor = 0;
    char *type_opts[MAX_TYPE_OPTS];
    const char *keys_opts[MAX_KEYS_OPTS];
    int ntype = 0, nkeys = 0;
    int frames = 300;
    int want_text = 0, autoload = 0, trace = 0, no_floating_bus = 0;
    int sdl_mode = 0, sdl_frames = 0;
    int i, f;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--rom")) {
            if (++i >= argc) { usage(argv[0]); return 1; }
            rom_path = argv[i];
        } else if (!strcmp(a, "--frames")) {
            if (++i >= argc) { usage(argv[0]); return 1; }
            frames = atoi(argv[i]);
            if (frames < 0) {
                fprintf(stderr, "error: bad --frames value '%s'\n", argv[i]);
                return 1;
            }
        } else if (!strcmp(a, "--screenshot")) {
            if (++i >= argc) { usage(argv[0]); return 1; }
            screenshot_path = argv[i];
        } else if (!strcmp(a, "--text")) {
            want_text = 1;
        } else if (!strcmp(a, "--type")) {
            if (++i >= argc) { usage(argv[0]); return 1; }
            if (ntype >= MAX_TYPE_OPTS) {
                fprintf(stderr, "error: too many --type options (max %d)\n",
                        MAX_TYPE_OPTS);
                return 1;
            }
            type_opts[ntype++] = argv[i];
        } else if (!strcmp(a, "--keys")) {
            if (++i >= argc) { usage(argv[0]); return 1; }
            if (nkeys >= MAX_KEYS_OPTS) {
                fprintf(stderr, "error: too many --keys options (max %d)\n",
                        MAX_KEYS_OPTS);
                return 1;
            }
            keys_opts[nkeys++] = argv[i];
        } else if (!strcmp(a, "--autoload")) {
            autoload = 1;
        } else if (!strcmp(a, "--turbo")) {
            /* accepted; emulator never paces */
        } else if (!strcmp(a, "--no-floating-bus")) {
            no_floating_bus = 1;
        } else if (!strcmp(a, "--wav")) {
            if (++i >= argc) { usage(argv[0]); return 1; }
            wav_path = argv[i];
        } else if (!strcmp(a, "--fb-dump")) {
            if (++i >= argc) { usage(argv[0]); return 1; }
            fbdump_path = argv[i];
        } else if (!strcmp(a, "--save-sna")) {
            if (++i >= argc) { usage(argv[0]); return 1; }
            save_sna = argv[i];
        } else if (!strcmp(a, "--save-z80")) {
            if (++i >= argc) { usage(argv[0]); return 1; }
            save_z80 = argv[i];
        } else if (!strcmp(a, "--save-scr")) {
            if (++i >= argc) { usage(argv[0]); return 1; }
            save_scr = argv[i];
        } else if (!strcmp(a, "--save-szx")) {
            if (++i >= argc) { usage(argv[0]); return 1; }
            save_szx = argv[i];
        } else if (!strcmp(a, "--rzx-record")) {
            if (++i >= argc) { usage(argv[0]); return 1; }
            rzx_record = argv[i];
        } else if (!strcmp(a, "--real-tape")) {
            real_tape = 1;
        } else if (!strcmp(a, "--play-at")) {
            if (++i >= argc) { usage(argv[0]); return 1; }
            play_at = atoi(argv[i]);
        } else if (!strcmp(a, "--save-tape")) {
            if (++i >= argc) { usage(argv[0]); return 1; }
            save_tape = argv[i];
        } else if (!strcmp(a, "--kempston")) {
            kempston = 1;
        } else if (!strcmp(a, "--joy")) {
            if (++i >= argc) { usage(argv[0]); return 1; }
            if (njoy >= MAX_KEYS_OPTS) {
                fprintf(stderr, "error: too many --joy options (max %d)\n",
                        MAX_KEYS_OPTS);
                return 1;
            }
            joy_opts[njoy++] = argv[i];
        } else if (!strcmp(a, "--joy-type")) {
            if (++i >= argc) { usage(argv[0]); return 1; }
            joy_type = argv[i];
        } else if (!strcmp(a, "--trace-frames")) {
            trace = 1;
        } else if (!strcmp(a, "--sdl")) {
            sdl_mode = 1;
        } else if (!strcmp(a, "--sdl-frames")) {
            if (++i >= argc) { usage(argv[0]); return 1; }
            sdl_frames = atoi(argv[i]);
        } else if (!strcmp(a, "--monitor")) {
            monitor = 1;
        } else if (!strcmp(a, "--debug")) {
            if (++i >= argc) { usage(argv[0]); return 1; }
            dbg_script = argv[i];
        } else if (!strcmp(a, "--trace")) {
            if (++i >= argc) { usage(argv[0]); return 1; }
            trace_path = argv[i];
        } else if (!strcmp(a, "--break") || !strcmp(a, "--watch")
                   || !strcmp(a, "--rwatch") || !strcmp(a, "--pwatch")) {
            uint16_t *arr = !strcmp(a, "--break") ? dbg_bp
                          : !strcmp(a, "--watch") ? dbg_ww
                          : !strcmp(a, "--rwatch") ? dbg_rw : dbg_pw;
            int *n = !strcmp(a, "--break") ? &n_bp
                   : !strcmp(a, "--watch") ? &n_ww
                   : !strcmp(a, "--rwatch") ? &n_rw : &n_pw;
            if (++i >= argc) { usage(argv[0]); return 1; }
            if (*n >= DBG_MAX_BP) {
                fprintf(stderr, "error: too many %s options (max %d)\n",
                        a, DBG_MAX_BP);
                return 1;
            }
            if (parse_hex16(argv[i], &arr[(*n)++]) != 0) {
                fprintf(stderr, "error: bad %s address '%s' (hex)\n",
                        a, argv[i]);
                return 1;
            }
        } else if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            usage(argv[0]);
            return 0;
        } else if (a[0] == '-') {
            fprintf(stderr, "error: unknown option '%s'\n", a);
            usage(argv[0]);
            return 1;
        } else if (!file_path) {
            file_path = a;
        } else {
            fprintf(stderr, "error: more than one input file given\n");
            return 1;
        }
    }

    if (machine_init(m, rom_path) != 0)
        return 1;
    if (no_floating_bus)
        m->floating_bus = 0;
    m->real_tape = real_tape;
    m->rzx = &rzx_state;
    if (file_path && machine_load_file(m, file_path) != 0)
        return 1;

    if (monitor || dbg_script || trace_path || n_bp || n_ww || n_rw || n_pw) {
        debug_attach(m, &dbg);
        dbg.script = dbg_script;
        dbg.stop_now = monitor;
        memcpy(dbg.bp, dbg_bp, sizeof dbg_bp);         dbg.nbp = n_bp;
        memcpy(dbg.wwatch, dbg_ww, sizeof dbg_ww);     dbg.nww = n_ww;
        memcpy(dbg.rwatch, dbg_rw, sizeof dbg_rw);     dbg.nrw = n_rw;
        memcpy(dbg.pwatch, dbg_pw, sizeof dbg_pw);     dbg.npw = n_pw;
        if (trace_path) {
            dbg.trace = fopen(trace_path, "w");
            if (!dbg.trace) {
                fprintf(stderr, "error: cannot create '%s'\n", trace_path);
                return 1;
            }
        }
    }

    if (real_tape || play_at >= 0)
        m->play_at_frame = (play_at >= 0) ? play_at : (autoload ? 320 : 1);
    if (save_tape) {
        FILE *tf = fopen(save_tape, "wb");   /* truncate; trap appends */
        if (!tf) {
            fprintf(stderr, "error: cannot create '%s'\n", save_tape);
            return 1;
        }
        fclose(tf);
        m->save_tape = save_tape;
    }

    if (autoload)
        keys_type(m, "j\"\"\n", 250);

    for (i = 0; i < ntype; i++) {
        char *at = strrchr(type_opts[i], '@');
        int start;
        if (!at || at == type_opts[i]) {
            fprintf(stderr, "error: --type needs \"STRING@FRAME\": '%s'\n",
                    type_opts[i]);
            return 1;
        }
        start = atoi(at + 1);
        *at = '\0';
        unescape_newlines(type_opts[i]);
        keys_type(m, type_opts[i], start);
    }

    for (i = 0; i < nkeys; i++) {
        const char *colon = strchr(keys_opts[i], ':');
        if (!colon) {
            fprintf(stderr, "error: --keys needs \"FRAME:NAMES\": '%s'\n",
                    keys_opts[i]);
            return 1;
        }
        if (keys_raw(m, atoi(keys_opts[i]), colon + 1) != 0)
            return 1;
    }

    if (!strcmp(joy_type, "kempston"))      m->joy_type = JOY_KEMPSTON;
    else if (!strcmp(joy_type, "sinclair")) m->joy_type = JOY_SINCLAIR;
    else if (!strcmp(joy_type, "cursor"))   m->joy_type = JOY_CURSOR;
    else {
        fprintf(stderr, "error: bad --joy-type '%s'\n", joy_type);
        return 1;
    }
    m->kempston_enabled = kempston;
    for (i = 0; i < njoy; i++) {
        const char *colon = strchr(joy_opts[i], ':');
        int f0, f1;
        char dash;
        if (!colon || sscanf(joy_opts[i], "%d%c%d", &f0, &dash, &f1) != 3
            || dash != '-' || f1 <= f0) {
            fprintf(stderr, "error: --joy needs \"FRAME-FRAME:DIRS\": '%s'\n",
                    joy_opts[i]);
            return 1;
        }
        if (keys_joy(m, f0, f1, colon + 1) != 0)
            return 1;
    }

    if (wav_path)
        beep_start(m, m->cpu.tstates);

    if (rzx_record && rzx_record_start(m, rzx_record) != 0)
        return 1;

    if (sdl_mode) {
        if (sdl_run(m, sdl_frames) != 0)
            return 1;
    } else {
        for (f = 0; f < frames; f++) {
            keys_apply(m, (int)m->frame_counter);
            /* beam-paint only the frame a screenshot/dump can observe */
            m->fb_live = (f == frames - 1);
            machine_run_frame(m);
            if (trace && (f % 50) == 0)
                fprintf(stderr, "frame %d  PC=%04X\n", f, m->cpu.pc.w);
        }
    }

    if (rzx_record && rzx_record_finish(m) != 0)
        return 1;

    if (wav_path && beep_save(m, wav_path, m->cpu.tstates) != 0)
        return 1;

    if (save_sna && snapshot_save_sna(m, save_sna) != 0)
        return 1;
    if (save_z80 && snapshot_save_z80(m, save_z80) != 0)
        return 1;
    if (save_szx && snapshot_save_szx(m, save_szx) != 0)
        return 1;
    if (save_scr && screen_save_scr(m, save_scr) != 0)
        return 1;

    if (screenshot_path || fbdump_path) {
        static uint32_t fb[HC91_FB_W * HC91_FB_H];
        video_render(m, fb);
        if (screenshot_path &&
            png_write(screenshot_path, fb, HC91_FB_W, HC91_FB_H) != 0) {
            fprintf(stderr, "error: failed to write '%s'\n", screenshot_path);
            return 1;
        }
        if (fbdump_path) {
            FILE *fp = fopen(fbdump_path, "wb");
            if (!fp || fwrite(fb, 4, HC91_FB_W * HC91_FB_H, fp)
                       != HC91_FB_W * HC91_FB_H) {
                fprintf(stderr, "error: failed to write '%s'\n", fbdump_path);
                if (fp) fclose(fp);
                return 1;
            }
            fclose(fp);
        }
    }

    if (want_text) {
        char text[24][33];
        int row;
        video_screen_text(m, text);
        printf("=== SCREEN ===\n");
        for (row = 0; row < 24; row++)
            printf("%s\n", text[row]);
        printf("=== END ===\n");
    }

    if (dbg.trace)
        fclose(dbg.trace);
    rzx_free(m);
    tape_free(m);
    return 0;
}
