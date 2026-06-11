/* snowtap — emit a .tap that demonstrates ULA snow: fills the pixel
 * area with a position-dependent pattern (so displaced fetches are
 * visible), loads I with a caller-chosen value (40 = contended page →
 * snow; 00 = clean), then loops flipping one attr byte so the beam
 * painter catches up mid-frame at many different R values.
 *
 * usage: snowtap OUT.tap IVAL(hex)
 */
#include <stdio.h>
#include <stdlib.h>
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
    /* BASIC: 10 CLEAR 32767: LOAD "" CODE : RANDOMIZE USR 32768 */
    static const uint8_t basic[] = {
        0x00, 0x0A, 0x20, 0x00,                          /* line 10, len 32 */
        0xFD, '3','2','7','6','7',                       /* CLEAR 32767 */
        0x0E, 0x00, 0x00, 0xFF, 0x7F, 0x00,
        0x3A, 0xEF, '"','"', 0xAF,                       /* :LOAD "" CODE */
        0x3A, 0xF9, 0xC0, '3','2','7','6','8',           /* :RANDOMIZE USR */
        0x0E, 0x00, 0x00, 0x00, 0x80, 0x00,
        0x0D
    };
    uint8_t code[40];
    uint8_t hdr[17];
    int blen = (int)sizeof basic, n = 0;
    uint8_t ival;
    FILE *f;

    if (argc != 3) {
        fprintf(stderr, "usage: %s OUT.tap IVAL(hex)\n", argv[0]);
        return 1;
    }
    ival = (uint8_t)strtoul(argv[2], NULL, 16);

    code[n++] = 0xF3;                            /* di */
    code[n++] = 0x3E; code[n++] = ival;          /* ld a,IVAL */
    code[n++] = 0xED; code[n++] = 0x47;          /* ld i,a */
    code[n++] = 0x21; code[n++] = 0x00; code[n++] = 0x40; /* ld hl,4000 */
    code[n++] = 0x75;                            /* fill: ld (hl),l */
    code[n++] = 0x23;                            /* inc hl */
    code[n++] = 0x7C;                            /* ld a,h */
    code[n++] = 0xFE; code[n++] = 0x58;          /* cp 58 (pixel end) */
    code[n++] = 0x20; code[n++] = 0xF9;          /* jr nz,fill */
    code[n++] = 0x36; code[n++] = 0x38;          /* attrf: ld (hl),38 */
    code[n++] = 0x23;                            /* inc hl */
    code[n++] = 0x7C;                            /* ld a,h */
    code[n++] = 0xFE; code[n++] = 0x5B;          /* cp 5B (attr end) */
    code[n++] = 0x20; code[n++] = 0xF9;          /* jr nz,attrf */
    code[n++] = 0x06; code[n++] = 0x10;          /* loop: ld b,16 */
    code[n++] = 0x10; code[n++] = 0xFE;          /* djnz $ */
    code[n++] = 0x3A; code[n++] = 0xFF; code[n++] = 0x5A; /* ld a,(5AFF) */
    code[n++] = 0xEE; code[n++] = 0x01;          /* xor 1 */
    code[n++] = 0x32; code[n++] = 0xFF; code[n++] = 0x5A; /* ld (5AFF),a */
    code[n++] = 0x18; code[n++] = 0xF3;          /* jr loop */

    f = fopen(argv[1], "wb");
    if (!f) {
        fprintf(stderr, "error: cannot write '%s'\n", argv[1]);
        return 1;
    }

    hdr[0] = 0;                                  /* program, autostart 10 */
    memcpy(hdr + 1, "snow      ", 10);
    hdr[11] = (uint8_t)blen;  hdr[12] = (uint8_t)(blen >> 8);
    hdr[13] = 10;             hdr[14] = 0;
    hdr[15] = (uint8_t)blen;  hdr[16] = (uint8_t)(blen >> 8);
    tap_block(f, hdr, 17, 0x00);
    tap_block(f, basic, blen, 0xFF);

    hdr[0] = 3;                                  /* code at 0x8000 */
    memcpy(hdr + 1, "snow      ", 10);
    hdr[11] = (uint8_t)n;     hdr[12] = 0;
    hdr[13] = 0x00;           hdr[14] = 0x80;
    hdr[15] = 0x00;           hdr[16] = 0x80;
    tap_block(f, hdr, 17, 0x00);
    tap_block(f, code, n, 0xFF);

    fclose(f);
    return 0;
}
