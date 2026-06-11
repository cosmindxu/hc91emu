/* pchist — debug tool: run a program, histogram PCs and log floating-bus
 * reads. Usage: pchist rom file.tap warm_frames sample_frames [keyframe]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/machine.h"

static uint32_t hist[65536];
static uint8_t (*orig_io_read)(void *ctx, uint16_t port);
static int log_io = 1;
static long io_count = 0;

#define NSPY 256
static struct { uint16_t pc, port; uint32_t count, firstf, lastf; } spy[NSPY];
static int nspy = 0;
static uint32_t curframe = 0;

static uint8_t io_spy(void *ctx, uint16_t port)
{
    Machine *m = (Machine *)ctx;
    uint8_t v = orig_io_read(ctx, port);
    int i;
    (void)log_io; (void)io_count;
    for (i = 0; i < nspy; i++)
        if (spy[i].pc == m->cpu.pc.w && spy[i].port == port) break;
    if (i < NSPY) {
        if (i == nspy) { nspy++; spy[i].pc = m->cpu.pc.w; spy[i].port = port; spy[i].firstf = curframe; }
        spy[i].count++; spy[i].lastf = curframe;
    }
    return v;
}

int main(int argc, char **argv)
{
    static Machine m;
    int warm = atoi(argv[3]), samp = atoi(argv[4]);
    int keyframe = argc > 5 ? atoi(argv[5]) : 0;
    int f, i;

    if (argc < 5) return 1;
    if (machine_init(&m, argv[1]) != 0) return 1;
    if (machine_load_file(&m, argv[2]) != 0) return 1;
    if (strstr(argv[2], ".z80")) {
        keys_type(&m, "n", 100);
        { int q; for (q = 0; q < 14; q++) keys_type(&m, " ", 200 + q * 5); }
    } else {
        keys_type(&m, "j\"\"\n", 250);
        if (keyframe) keys_type(&m, " ", keyframe);
    }

    orig_io_read = m.cpu.io_read;
    m.cpu.io_read = io_spy;

    for (f = 0; f < warm; f++) {
        keys_apply(&m, f);
        curframe = (uint32_t)f;
        machine_run_frame(&m);
    }
    log_io = 1;
    for (f = warm; f < warm + samp; f++) {
        uint64_t start = m.cpu.tstates;
        keys_apply(&m, f);
        curframe = (uint32_t)f;
        if (f >= 5498 && f <= 5507) {
            int k; printf("frame %d rows:", f);
            for (k = 0; k < 8; k++) printf(" %02X", m.keyrows[k]);
            printf("\n");
        }
        m.frame_start_ts = start;
        z80_int(&m.cpu, 0xFF);
        while (m.cpu.tstates - start < 69888) {
            hist[m.cpu.pc.w]++;
            if (m.tape.attached && m.cpu.pc.w == 0x0556) tape_trap(&m);
            z80_step(&m.cpu);
        }
        m.frame_counter++;
    }
    {
        int a, k;
        printf("mem 8200-8280:\n");
        for (a = 0x8200; a < 0x8280; a += 16) {
            printf("%04X:", a);
            for (k = 0; k < 16; k++) printf(" %02X", m.mem[a + k]);
            printf("\n");
        }
    }
    for (i = 0; i < nspy; i++)
        printf("SPY PC=%04X port=%04X count=%u frames %u..%u\n",
               spy[i].pc, spy[i].port, spy[i].count, spy[i].firstf, spy[i].lastf);
    for (i = 0; i < 12; i++) {
        uint32_t best = 0, addr = 0; int j;
        for (j = 0; j < 65536; j++)
            if (hist[j] > best) { best = hist[j]; addr = (uint32_t)j; }
        if (!best) break;
        printf("PC %04X  count %u\n", addr, best);
        hist[addr] = 0;
    }
    return 0;
}
