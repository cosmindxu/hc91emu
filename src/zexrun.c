/* zexrun.c — minimal CP/M harness for running zexdoc/zexall against the
 * Z80 core. 64K flat RAM, BDOS console calls trapped at 0x0005.
 *
 * Build: gcc -O2 -Wall -o build/zexrun src/zexrun.c src/z80.c
 * Usage: build/zexrun tests/zexdoc.com
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "z80.h"

static uint8_t mem[0x10000];

static uint8_t mem_read(void *ctx, uint16_t addr)
{
    (void)ctx;
    return mem[addr];
}

static void mem_write(void *ctx, uint16_t addr, uint8_t val)
{
    (void)ctx;
    mem[addr] = val;
}

static uint8_t io_read(void *ctx, uint16_t port)
{
    (void)ctx; (void)port;
    return 0xff;
}

static void io_write(void *ctx, uint16_t port, uint8_t val)
{
    (void)ctx; (void)port; (void)val;
}

int main(int argc, char **argv)
{
    Z80 z;
    FILE *f;
    size_t n;

    if (argc < 2) {
        fprintf(stderr, "usage: %s program.com\n", argv[0]);
        return 1;
    }

    f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 1;
    }
    n = fread(mem + 0x0100, 1, sizeof(mem) - 0x0100, f);
    fclose(f);
    if (n == 0) {
        fprintf(stderr, "empty program\n");
        return 1;
    }

    /* CP/M scaffolding: RET at BDOS entry, BDOS vector at 0x0006 */
    mem[0x0000] = 0x76;                 /* HALT at warm-boot, just in case */
    mem[0x0005] = 0xc9;                 /* RET */
    mem[0x0006] = 0x00;                 /* word at 6 = 0xF000 (stack top) */
    mem[0x0007] = 0xf0;

    memset(&z, 0, sizeof(z));
    z.ctx = NULL;
    z.mem_read = mem_read;
    z.mem_write = mem_write;
    z.io_read = io_read;
    z.io_write = io_write;

    z80_reset(&z);
    z.pc.w = 0x0100;
    z.sp.w = 0xf000;

    for (;;) {
        if (z.pc.w == 0x0000)
            break;                      /* warm boot = program done */

        if (z.pc.w == 0x0005) {         /* BDOS call */
            uint8_t fn = z.bc.b.l;
            if (fn == 2) {              /* console output: E */
                putchar(z.de.b.l);
                if (z.de.b.l == '\n') fflush(stdout);
            } else if (fn == 9) {       /* print string at DE until '$' */
                uint16_t a = z.de.w;
                while (mem[a] != '$') {
                    putchar(mem[a]);
                    a++;
                }
                fflush(stdout);
            }
            /* fall through: the RET at 0x0005 executes normally */
        }

        z80_step(&z);
    }

    fflush(stdout);
    printf("ZEX RUN COMPLETE\n");
    return 0;
}
