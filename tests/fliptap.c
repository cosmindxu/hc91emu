/* fliptap — emit a .tap that exercises multi-load tape auto-pause:
 *   10 CLEAR 32767: LOAD "" CODE : PAUSE 150 : LOAD "" CODE
 * The first CODE block is a dummy at 0x8000; during the 3-second PAUSE
 * the CPU does not poll the EAR port, so the player must pause at the
 * next block boundary or the second block streams past unheard. The
 * second CODE block is a full 6912-byte screen whose attr area is a
 * solid pattern — the suite asserts it arrived.
 *
 * usage: fliptap OUT.tap
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static void tap_block(FILE *f, const uint8_t *data, int n, uint8_t flag)
{
    uint8_t cs = flag;
    int i;
    fputc((n + 2) & 0xFF, f);
    fputc(((n + 2) >> 8) & 0xFF, f);
    fputc(flag, f);
    for (i = 0; i < n; i++) {
        fputc(data[i], f);
        cs ^= data[i];
    }
    fputc(cs, f);
}

int main(int argc, char **argv)
{
    /* 10 CLEAR 32767: LOAD "" CODE : PAUSE 150 : LOAD "" CODE */
    static const uint8_t basic[] = {
        0x00, 0x0A, 0x22, 0x00,                          /* line 10, len 34 */
        0xFD, '3','2','7','6','7',                       /* CLEAR 32767 */
        0x0E, 0x00, 0x00, 0xFF, 0x7F, 0x00,
        0x3A, 0xEF, '"','"', 0xAF,                       /* :LOAD "" CODE */
        0x3A, 0xF2, '1','5','0',                         /* :PAUSE 150 */
        0x0E, 0x00, 0x00, 0x96, 0x00, 0x00,
        0x3A, 0xEF, '"','"', 0xAF,                       /* :LOAD "" CODE */
        0x0D
    };
    static const uint8_t dummy[16] = { 0xC9 };           /* just a RET */
    static uint8_t screen[6912];
    uint8_t hdr[17];
    int blen = (int)sizeof basic;
    FILE *f;
    int i;

    if (argc != 2) {
        fprintf(stderr, "usage: %s OUT.tap\n", argv[0]);
        return 1;
    }
    f = fopen(argv[1], "wb");
    if (!f) {
        fprintf(stderr, "error: cannot write '%s'\n", argv[1]);
        return 1;
    }

    for (i = 0; i < 6144; i++)
        screen[i] = 0xF0;                                /* pixel stripes */
    for (; i < 6912; i++)
        screen[i] = 0x32;                                /* red on green */

    hdr[0] = 0;                                          /* program */
    memcpy(hdr + 1, "flip      ", 10);
    hdr[11] = (uint8_t)blen;  hdr[12] = (uint8_t)(blen >> 8);
    hdr[13] = 10;             hdr[14] = 0;               /* LINE 10 */
    hdr[15] = (uint8_t)blen;  hdr[16] = (uint8_t)(blen >> 8);
    tap_block(f, hdr, 17, 0x00);
    tap_block(f, basic, blen, 0xFF);

    hdr[0] = 3;                                          /* dummy code */
    memcpy(hdr + 1, "side1     ", 10);
    hdr[11] = sizeof dummy;   hdr[12] = 0;
    hdr[13] = 0x00;           hdr[14] = 0x80;            /* 0x8000 */
    hdr[15] = 0x00;           hdr[16] = 0x80;
    tap_block(f, hdr, 17, 0x00);
    tap_block(f, dummy, sizeof dummy, 0xFF);

    hdr[0] = 3;                                          /* "side 2" */
    memcpy(hdr + 1, "side2     ", 10);
    hdr[11] = (uint8_t)(6912 & 0xFF);
    hdr[12] = (uint8_t)(6912 >> 8);
    hdr[13] = 0x00;           hdr[14] = 0x40;            /* 0x4000 */
    hdr[15] = 0x00;           hdr[16] = 0x40;
    tap_block(f, hdr, 17, 0x00);
    tap_block(f, screen, 6912, 0xFF);

    fclose(f);
    return 0;
}
