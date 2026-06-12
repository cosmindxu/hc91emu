/* sdl.c — interactive SDL2 frontend, loaded via dlopen() at runtime so
 * the build needs no SDL headers or link-time libraries (this VM has the
 * runtime .so but no -dev package). The small slice of the SDL2 ABI used
 * here (struct layouts, constants) has been stable since 2.0.0.
 *
 * Controls: Spectrum keys map 1:1 (letters/digits/Enter/Space), Shift =
 * CAPS SHIFT, Ctrl/LAlt = SYMBOL SHIFT, Backspace = CAPS+0, Esc = BREAK,
 * arrows = BASIC cursors (plain 5678 with --joy-type cursor, Kempston
 * with --kempston, fire = RAlt). First game controller drives Kempston.
 * Tab = turbo while held, F5 = pause, F10/close = quit.
 *
 * Pacing is audio-clocked: beeper samples are queued each frame and the
 * loop sleeps while more than ~4 frames are buffered; without an audio
 * device it falls back to a 20 ms tick. --wav recording keeps working
 * (the sample buffer is then kept, not drained).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef _WIN32
/* the same late-binding idea, via the Win32 loader */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef CreateWindow             /* windows.h macros vs our members */
#define dlopen(name, flags) ((void *)LoadLibraryA(name))
#define dlsym(handle, sym)  ((void *)(uintptr_t)GetProcAddress( \
                                 (HMODULE)(handle), (sym)))
#define dlerror()           "LoadLibrary failed"
#define SDL_LIB_PRIMARY     "SDL2.dll"
#define SDL_LIB_FALLBACK    "SDL2.dll"
#define RTLD_NOW    0
#define RTLD_GLOBAL 0
#else
#include <dlfcn.h>
#define SDL_LIB_PRIMARY     "libSDL2-2.0.so.0"
#define SDL_LIB_FALLBACK    "libSDL2.so"
#endif
#include "machine.h"

/* ---- minimal SDL2 ABI ---- */

#define MYSDL_INIT_AUDIO          0x10u
#define MYSDL_INIT_VIDEO          0x20u
#define MYSDL_INIT_GAMECONTROLLER 0x2000u
#define MYSDL_QUIT_EV             0x100u
#define MYSDL_KEYDOWN             0x300u
#define MYSDL_KEYUP               0x301u
#define MYSDL_WINDOWPOS_CENTERED  0x2FFF0000u
#define MYSDL_WINDOW_RESIZABLE    0x20u
#define MYSDL_PIXFMT_ABGR8888     0x16762004u   /* bytes R,G,B,A */
#define MYSDL_TEXTURE_STREAMING   1
#define MYSDL_AUDIO_S16LSB        0x8010u

typedef struct {
    int freq;
    uint16_t format;
    uint8_t channels, silence;
    uint16_t samples, pad;
    uint32_t size;
    void (*callback)(void *, uint8_t *, int);
    void *userdata;
} MyAudioSpec;

typedef struct {
    uint32_t scancode;
    int32_t sym;
    uint16_t mod;
    uint32_t unused;
} MyKeysym;

typedef struct {
    uint32_t type, timestamp, windowID;
    uint8_t state, repeat, p2, p3;
    MyKeysym keysym;
} MyKeyEvent;

typedef union {
    uint32_t type;
    MyKeyEvent key;
    uint8_t pad[56];                 /* sizeof(SDL_Event) */
} MyEvent;

/* SDL keycodes used below (scancode | 0x40000000 for non-printables) */
#define K_LSHIFT 0x400000E1
#define K_RSHIFT 0x400000E5
#define K_LCTRL  0x400000E0
#define K_RCTRL  0x400000E4
#define K_LALT   0x400000E2
#define K_RALT   0x400000E6
#define K_RIGHT  0x4000004F
#define K_LEFT   0x40000050
#define K_DOWN   0x40000051
#define K_UP     0x40000052
#define K_F5     0x4000003E
#define K_F6     0x4000003F
#define K_F7     0x40000040
#define K_F8     0x40000041
#define K_F10    0x40000043

static struct {
    int (*Init)(uint32_t);
    void (*Quit)(void);
    const char *(*GetError)(void);
    void *(*CreateWindow)(const char *, int, int, int, int, uint32_t);
    void *(*CreateRenderer)(void *, int, uint32_t);
    void *(*CreateTexture)(void *, uint32_t, int, int, int);
    int (*UpdateTexture)(void *, const void *, const void *, int);
    int (*RenderClear)(void *);
    int (*RenderCopy)(void *, void *, const void *, const void *);
    void (*RenderPresent)(void *);
    int (*PollEvent)(MyEvent *);
    void (*Delay)(uint32_t);
    uint32_t (*GetTicks)(void);
    void (*SetWindowTitle)(void *, const char *);
    uint32_t (*OpenAudioDevice)(const char *, int, const MyAudioSpec *,
                                MyAudioSpec *, int);
    void (*PauseAudioDevice)(uint32_t, int);
    int (*QueueAudio)(uint32_t, const void *, uint32_t);
    uint32_t (*GetQueuedAudioSize)(uint32_t);
    /* optional (gamepad) */
    int (*NumJoysticks)(void);
    int (*IsGameController)(int);
    void *(*GameControllerOpen)(int);
    uint8_t (*GameControllerGetButton)(void *, int);
    int16_t (*GameControllerGetAxis)(void *, int);
} S;

static int sdl_load(void)
{
    void *dl = dlopen(SDL_LIB_PRIMARY, RTLD_NOW | RTLD_GLOBAL);
    if (!dl) dl = dlopen(SDL_LIB_FALLBACK, RTLD_NOW | RTLD_GLOBAL);
    if (!dl) {
        fprintf(stderr, "error: SDL2 runtime not found (%s): %s\n",
                SDL_LIB_PRIMARY, dlerror());
        return -1;
    }
#define REQ(name) do { \
        *(void **)&S.name = dlsym(dl, "SDL_" #name); \
        if (!S.name) { \
            fprintf(stderr, "error: SDL2 lacks SDL_%s\n", #name); \
            return -1; \
        } \
    } while (0)
#define OPT(name) (*(void **)&S.name = dlsym(dl, "SDL_" #name))
    REQ(Init); REQ(Quit); REQ(GetError);
    REQ(CreateWindow); REQ(CreateRenderer); REQ(CreateTexture);
    REQ(UpdateTexture); REQ(RenderClear); REQ(RenderCopy);
    REQ(RenderPresent); REQ(PollEvent); REQ(Delay); REQ(GetTicks);
    REQ(SetWindowTitle);
    REQ(OpenAudioDevice); REQ(PauseAudioDevice);
    REQ(QueueAudio); REQ(GetQueuedAudioSize);
    OPT(NumJoysticks); OPT(IsGameController); OPT(GameControllerOpen);
    OPT(GameControllerGetButton); OPT(GameControllerGetAxis);
#undef REQ
#undef OPT
    return 0;
}

/* ---- host keyboard -> Spectrum matrix ---- */

typedef struct { int32_t sym; const char *k1, *k2; } HostKey;
static const HostKey hostmap[] = {
    { 13,       "ENTER", NULL },
    { ' ',      "SPACE", NULL },
    { K_LSHIFT, "CAPS",  NULL },
    { K_RSHIFT, "CAPS",  NULL },
    { K_LCTRL,  "SYM",   NULL },
    { K_RCTRL,  "SYM",   NULL },
    { K_LALT,   "SYM",   NULL },
    { 8,        "CAPS",  "0" },      /* Backspace = DELETE */
    { 27,       "CAPS",  "SPACE" },  /* Esc = BREAK */
    { ',',      "SYM",   "N" },
    { '.',      "SYM",   "M" },
    { ';',      "SYM",   "O" },
    { '\'',     "SYM",   "7" },
    { '-',      "SYM",   "J" },
    { '=',      "SYM",   "L" },
    { '/',      "SYM",   "V" },
};

#define MAX_DOWN 24
static int32_t down[MAX_DOWN];
static int ndown;

static void key_down(int32_t sym)
{
    int i;
    for (i = 0; i < ndown; i++)
        if (down[i] == sym) return;
    if (ndown < MAX_DOWN)
        down[ndown++] = sym;
}

static void key_up(int32_t sym)
{
    int i;
    for (i = 0; i < ndown; i++)
        if (down[i] == sym) { down[i] = down[--ndown]; return; }
}

static void press_name(Machine *m, const char *name)
{
    int row, bit;
    if (keys_name_pos(name, (int)strlen(name), &row, &bit) == 0)
        m->keyrows[row] |= (uint8_t)(1u << bit);
}

/* OR the currently held host keys into keyrows/kempston (after
 * keys_apply() has applied any scheduled --type/--keys events). */
static void apply_live(Machine *m)
{
    int i;
    size_t k;
    for (i = 0; i < ndown; i++) {
        int32_t sym = down[i];
        if ((sym >= 'a' && sym <= 'z') || (sym >= '0' && sym <= '9')) {
            char one[2] = { (char)sym, 0 };
            press_name(m, one);
            continue;
        }
        if (sym == K_UP || sym == K_DOWN || sym == K_LEFT || sym == K_RIGHT
            || sym == K_RALT) {
            if (m->kempston_enabled) {           /* Kempston UDLRF */
                m->kempston |= (uint8_t)(sym == K_UP ? 0x08 :
                                         sym == K_DOWN ? 0x04 :
                                         sym == K_LEFT ? 0x02 :
                                         sym == K_RIGHT ? 0x01 : 0x10);
            } else if (sym != K_RALT) {
                const char *d = sym == K_LEFT ? "5" : sym == K_DOWN ? "6"
                              : sym == K_UP ? "7" : "8";
                if (m->joy_type != JOY_CURSOR)
                    press_name(m, "CAPS");       /* BASIC cursor moves */
                press_name(m, d);
            }
            continue;
        }
        for (k = 0; k < sizeof hostmap / sizeof hostmap[0]; k++) {
            if (hostmap[k].sym == sym) {
                press_name(m, hostmap[k].k1);
                if (hostmap[k].k2)
                    press_name(m, hostmap[k].k2);
                break;
            }
        }
    }
}

/* SDL_GameController buttons/axes (ABI enum values) */
#define GC_BTN_A 0
#define GC_DPAD_UP 11
#define GC_DPAD_DOWN 12
#define GC_DPAD_LEFT 13
#define GC_DPAD_RIGHT 14
#define GC_AXIS_LX 0
#define GC_AXIS_LY 1

static void apply_pad(Machine *m, void *pad)
{
    int16_t x, y;
    if (!pad) return;
    if (S.GameControllerGetButton(pad, GC_DPAD_UP))    m->kempston |= 0x08;
    if (S.GameControllerGetButton(pad, GC_DPAD_DOWN))  m->kempston |= 0x04;
    if (S.GameControllerGetButton(pad, GC_DPAD_LEFT))  m->kempston |= 0x02;
    if (S.GameControllerGetButton(pad, GC_DPAD_RIGHT)) m->kempston |= 0x01;
    if (S.GameControllerGetButton(pad, GC_BTN_A))      m->kempston |= 0x10;
    x = S.GameControllerGetAxis(pad, GC_AXIS_LX);
    y = S.GameControllerGetAxis(pad, GC_AXIS_LY);
    if (x < -16384) m->kempston |= 0x02;
    if (x > 16384)  m->kempston |= 0x01;
    if (y < -16384) m->kempston |= 0x08;
    if (y > 16384)  m->kempston |= 0x04;
}

/* ---- main loop ---- */

#define AUDIO_RATE 44100
#define FRAME_SAMPLES (AUDIO_RATE / 50)          /* 882 per 20 ms frame */

int sdl_run(Machine *m, int max_frames)
{
    static uint32_t fb[HC91_FB_W * HC91_FB_H];
    void *win, *ren, *tex, *pad = NULL;
    uint32_t dev = 0, next_tick;
    int frames = 0, paused = 0, turbo = 0, quit = 0;
    int keep_samples;                /* --wav active: don't drain buffer */
    size_t sent = 0;
    MyAudioSpec want, have;

    if (sdl_load() != 0)
        return -1;
    if (S.Init(MYSDL_INIT_VIDEO | MYSDL_INIT_AUDIO |
               MYSDL_INIT_GAMECONTROLLER) != 0 &&
        S.Init(MYSDL_INIT_VIDEO | MYSDL_INIT_AUDIO) != 0 &&
        S.Init(MYSDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "error: SDL_Init: %s\n", S.GetError());
        return -1;
    }

    win = S.CreateWindow("HC-91", (int)MYSDL_WINDOWPOS_CENTERED,
                         (int)MYSDL_WINDOWPOS_CENTERED,
                         HC91_FB_W * 2, HC91_FB_H * 2,
                         MYSDL_WINDOW_RESIZABLE);
    ren = win ? S.CreateRenderer(win, -1, 0) : NULL;
    tex = ren ? S.CreateTexture(ren, MYSDL_PIXFMT_ABGR8888,
                                MYSDL_TEXTURE_STREAMING,
                                HC91_FB_W, HC91_FB_H) : NULL;
    if (!tex) {
        fprintf(stderr, "error: SDL window/renderer: %s\n", S.GetError());
        S.Quit();
        return -1;
    }

    memset(&want, 0, sizeof want);
    want.freq = AUDIO_RATE;
    want.format = MYSDL_AUDIO_S16LSB;
    want.channels = 1;
    want.samples = 1024;
    dev = S.OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (dev)
        S.PauseAudioDevice(dev, 0);

    keep_samples = m->beep.enabled;              /* --wav started it */
    if (!m->beep.enabled)
        beep_start(m, m->cpu.tstates);
    sent = m->beep.n;

    if (S.NumJoysticks && S.IsGameController && S.GameControllerOpen) {
        int i, n = S.NumJoysticks();
        for (i = 0; i < n && !pad; i++)
            if (S.IsGameController(i))
                pad = S.GameControllerOpen(i);
        if (pad) {
            m->kempston_enabled = 1;
            fprintf(stderr, "sdl: game controller -> Kempston\n");
        }
    }

    fprintf(stderr, "sdl: %dx%d window, audio %s%s\n",
            HC91_FB_W * 2, HC91_FB_H * 2, dev ? "on" : "off",
            pad ? ", controller on" : "");

    next_tick = S.GetTicks() + 20;
    while (!quit) {
        MyEvent ev;
        while (S.PollEvent(&ev)) {
            if (ev.type == MYSDL_QUIT_EV) {
                quit = 1;
            } else if (ev.type == MYSDL_KEYDOWN && !ev.key.repeat) {
                int32_t sym = ev.key.keysym.sym;
                if (sym == K_F10) quit = 1;
                else if (sym == K_F5) {
                    paused = !paused;
                    S.SetWindowTitle(win, paused ? "HC-91 (paused)"
                                                 : "HC-91");
                } else if (sym == K_F6) {        /* manual tape toggle */
                    if (m->player.playing) {
                        m->player.paused = !m->player.paused;
                        if (!m->player.paused)
                            m->player.edge_ts = m->cpu.tstates
                                + m->player.pulses[m->player.idx];
                        fprintf(stderr, "tape: %s (F6)\n",
                                m->player.paused ? "paused" : "playing");
                    }
                } else if (sym == K_F7) {        /* manual tape rewind */
                    if (m->player.npulses) {
                        m->player.idx = 0;
                        m->player.ear = 0;
                        m->player.nextb = 1;
                        m->player.playing = 1;
                        m->player.paused = 1;
                        fprintf(stderr, "tape: rewound (F7)\n");
                    }
                } else if (sym == K_F8) {        /* swap cassette side */
                    if (m->tape_next)
                        tape_swap(m, m->tape_next);
                } else if (sym == 9) turbo = 1;  /* Tab */
                else key_down(sym);
            } else if (ev.type == MYSDL_KEYUP) {
                if (ev.key.keysym.sym == 9) turbo = 0;
                else key_up(ev.key.keysym.sym);
            }
        }

        if (paused) {
            S.RenderClear(ren);
            S.RenderCopy(ren, tex, NULL, NULL);
            S.RenderPresent(ren);
            S.Delay(20);
            continue;
        }

        keys_apply(m, (int)m->frame_counter);
        apply_live(m);
        apply_pad(m, pad);

        m->fb_live = 1;
        machine_run_frame(m);

        video_render(m, fb);
        S.UpdateTexture(tex, NULL, fb, HC91_FB_W * 4);
        S.RenderClear(ren);
        S.RenderCopy(ren, tex, NULL, NULL);
        S.RenderPresent(ren);

        if (dev) {
            Beeper *b = &m->beep;
            beep_flush(m, m->cpu.tstates);
            if (b->n > sent)
                S.QueueAudio(dev, b->buf + sent,
                             (uint32_t)((b->n - sent) * 2));
            if (keep_samples) {
                sent = b->n;
            } else {
                b->n = 0;
                sent = 0;
            }
        }

        if (!turbo) {
            if (dev) {
                int spin = 0;
                while (S.GetQueuedAudioSize(dev) > 4 * FRAME_SAMPLES * 2
                       && ++spin < 50)
                    S.Delay(1);
            } else {
                uint32_t t = S.GetTicks();
                if ((int32_t)(next_tick - t) > 0)
                    S.Delay(next_tick - t);
                t = S.GetTicks();
                next_tick += 20;
                if ((int32_t)(next_tick - t) < -200)
                    next_tick = t + 20;          /* fell behind: resync */
            }
        }

        frames++;
        if (max_frames > 0 && frames >= max_frames)
            quit = 1;
    }

    fprintf(stderr, "sdl: exiting after %d frames\n", frames);
    S.Quit();
    return 0;
}
