/* wav.c — beeper audio rendering for the HC-91 emulator.
 *
 * The ULA speaker is driven by bit 4 of OUTs to port 0xFE. machine.c
 * calls beep_edge() on every level change with the CPU T-state counter;
 * the level is box-filtered into 44.1 kHz 16-bit mono samples on the fly
 * (no event buffer), and beep_save() writes a WAV file at exit.
 *
 * Timing granularity is the instruction boundary (a few T-states, <4 us),
 * far below audibility.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "machine.h"

#define BEEP_RATE 44100
#define BEEP_AMP  12000.0
#define TPS (3500000.0 / BEEP_RATE)   /* T-states per output sample */

static int append(Beeper *b, int16_t s)
{
    if (b->n == b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 1 << 16;
        int16_t *nb = realloc(b->buf, nc * sizeof(int16_t));
        if (!nb) return -1;
        b->buf = nb; b->cap = nc;
    }
    b->buf[b->n++] = s;
    return 0;
}

void beep_start(Machine *m, uint64_t now_ts)
{
    Beeper *b = &m->beep;
    b->enabled = 1;
    b->start_ts = now_ts;
    b->last_td = (double)now_ts;
    b->pos = 0.0;
    b->level = m->beeper;
    b->acc = 0.0;
    b->n = 0;
}

/* Advance synthesis up to T-state `now`, integrating the current level.
 * b->last_td is fractional T-state time; b->pos is the fill position
 * within the current output sample (0..TPS). */
static void advance(Machine *m, uint64_t now)
{
    Beeper *b = &m->beep;
    double t = b->last_td;
    while (t < (double)now) {
        double take = (double)now - t;
        double room = TPS - b->pos;
        if (take > room) take = room;
        if (b->level) b->acc += take;
        b->pos += take;
        t += take;
        if (b->pos >= TPS - 1e-9) {
            double avg = b->acc / TPS;          /* 0..1 duty in sample */
            int s = (int)((avg - 0.5) * 2.0 * BEEP_AMP);
            if (m->model == HC91_MODEL_128)
                s += ay_sample(&m->ay);         /* mix the PSG */
            if (s > 32767) s = 32767;
            if (s < -32768) s = -32768;
            if (append(b, (int16_t)s))
                return;
            b->acc = 0.0;
            b->pos = 0.0;
        }
    }
    b->last_td = t;
}

void beep_edge(Machine *m, uint64_t now_ts, int new_level)
{
    Beeper *b = &m->beep;
    if (!b->enabled || new_level == b->level) return;
    advance(m, now_ts);
    b->level = new_level;
}

/* Synthesize up to `now` without a level change — the live (SDL) frontend
 * calls this once per frame, drains buf[0..n) to the audio device and
 * resets n; pos/acc/last_td carry the fractional state across frames. */
void beep_flush(Machine *m, uint64_t now_ts)
{
    if (m->beep.enabled)
        advance(m, now_ts);
}

static void put32(FILE *f, uint32_t v) { fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f); fputc((v >> 16) & 0xff, f); fputc(v >> 24, f); }
static void put16(FILE *f, uint16_t v) { fputc(v & 0xff, f); fputc(v >> 8, f); }

int beep_save(Machine *m, const char *path, uint64_t now_ts)
{
    Beeper *b = &m->beep;
    FILE *f;
    uint32_t bytes;

    if (!b->enabled) return -1;
    advance(m, now_ts);
    bytes = (uint32_t)(b->n * 2);

    f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "error: cannot write '%s'\n", path); return -1; }
    fwrite("RIFF", 1, 4, f); put32(f, 36 + bytes); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); put32(f, 16);
    put16(f, 1);                       /* PCM */
    put16(f, 1);                       /* mono */
    put32(f, BEEP_RATE);
    put32(f, BEEP_RATE * 2);           /* byte rate */
    put16(f, 2);                       /* block align */
    put16(f, 16);                      /* bits */
    fwrite("data", 1, 4, f); put32(f, bytes);
    fwrite(b->buf, 2, b->n, f);
    fclose(f);
    fprintf(stderr, "wav: wrote %zu samples (%.2f s) to %s\n",
            b->n, (double)b->n / BEEP_RATE, path);
    return 0;
}
