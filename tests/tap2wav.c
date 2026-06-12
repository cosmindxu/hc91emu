/* tap2wav — render a .tap as a sampled WAV "cassette recording" with
 * standard ROM pulse timings (pilot/sync/bit pulses) and a silent gap
 * after each block, to exercise the emulator's WAV cassette input
 * (Schmitt trigger, DC removal, silence-as-boundary).
 *
 * usage: tap2wav IN.tap OUT.wav [rate] [bits] [ch]
 *        rate default 44100; bits 8 or 16 (default 8); ch 1 or 2
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define T_PILOT     2168
#define T_SYNC1      667
#define T_SYNC2      735
#define T_BIT0       855
#define T_BIT1      1710
#define PILOT_HDR   8063
#define PILOT_DATA  3223

static FILE *out;
static int rate = 44100, bits = 8, ch = 1;
static int level;                    /* current line level 0/1 */
static double tpos;                  /* output position, in samples */
static long nsamples;

static void emit_until(double target)
{
    while (nsamples < (long)target) {
        int c;
        for (c = 0; c < ch; c++) {
            if (bits == 8) {
                fputc(level ? 220 : 36, out);
            } else {
                int v = level ? 18000 : -18000;
                fputc(v & 0xFF, out);
                fputc((v >> 8) & 0xFF, out);
            }
        }
        nsamples++;
    }
}

static void pulse(uint32_t t)        /* t T-states, then toggle */
{
    tpos += (double)t * rate / 3500000.0;
    emit_until(tpos);
    level = !level;
}

static void gap(double seconds)      /* flat line, no edges */
{
    tpos += seconds * rate;
    emit_until(tpos);
}

static void w16f(unsigned v) { fputc(v & 0xFF, out); fputc((v >> 8) & 0xFF, out); }
static void w32f(unsigned v) { w16f(v); w16f(v >> 16); }

int main(int argc, char **argv)
{
    FILE *in;
    long size, off = 0, riff_at, data_at, end;
    uint8_t *buf;

    if (argc < 3) {
        fprintf(stderr, "usage: %s IN.tap OUT.wav [rate] [bits] [ch]\n",
                argv[0]);
        return 1;
    }
    if (argc > 3) rate = atoi(argv[3]);
    if (argc > 4) bits = atoi(argv[4]);
    if (argc > 5) ch = atoi(argv[5]);

    in = fopen(argv[1], "rb");
    if (!in) { perror(argv[1]); return 1; }
    fseek(in, 0, SEEK_END);
    size = ftell(in);
    fseek(in, 0, SEEK_SET);
    buf = malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, in) != (size_t)size) {
        fprintf(stderr, "error: cannot read %s\n", argv[1]);
        return 1;
    }
    fclose(in);

    out = fopen(argv[2], "wb");
    if (!out) { perror(argv[2]); return 1; }

    /* canonical 44-byte header; sizes patched at the end */
    fputs("RIFF", out); riff_at = 4; w32f(0); fputs("WAVE", out);
    fputs("fmt ", out); w32f(16);
    w16f(1); w16f((unsigned)ch); w32f((unsigned)rate);
    w32f((unsigned)(rate * ch * bits / 8)); w16f((unsigned)(ch * bits / 8));
    w16f((unsigned)bits);
    fputs("data", out); data_at = ftell(out); w32f(0);

    gap(0.5);                            /* leader silence */
    while (off + 2 <= size) {
        uint16_t len = (uint16_t)(buf[off] | (buf[off + 1] << 8));
        uint32_t i, n;
        int b;
        off += 2;
        if (off + len > size)
            break;
        n = (len && buf[off] < 128) ? PILOT_HDR : PILOT_DATA;
        for (i = 0; i < n; i++) pulse(T_PILOT);
        pulse(T_SYNC1);
        pulse(T_SYNC2);
        for (i = 0; i < len; i++)
            for (b = 0; b < 8; b++) {
                uint32_t t = (buf[off + i] & (0x80u >> b)) ? T_BIT1 : T_BIT0;
                pulse(t);
                pulse(t);
            }
        off += len;
        gap(1.5);                        /* inter-block tape gap */
    }

    end = ftell(out);
    fseek(out, riff_at, SEEK_SET);  w32f((unsigned)(end - 8));
    fseek(out, data_at, SEEK_SET);  w32f((unsigned)(end - data_at - 4));
    fclose(out);
    free(buf);
    printf("%s: %ld samples, %.1f s (%d Hz %d-bit %s)\n", argv[2],
           nsamples, nsamples / (double)rate, rate, bits,
           ch == 2 ? "stereo" : "mono");
    return 0;
}
