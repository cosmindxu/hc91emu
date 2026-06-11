/* cpmtap — emit a .tap exercising the HC-91 CP/M paging port 0x7E:
 * writes a marker into paged-in low RAM, reads a ROM byte while paged
 * out and the marker back while paged in (both stored to the screen for
 * assertions), then jumps to 0x0000 with RAM paged — landing on a HALT.
 *
 * Expected: screen[0]=0x42 (marker via CP/M RAM), screen[1]=0x0D (ROM
 * byte at 0x2000), border red, CPU parked at PC=1.
 *
 * usage: cpmtap OUT.tap
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
    static const uint8_t basic[] = {
        0x00, 0x0A, 0x20, 0x00,
        0xFD, '3','2','7','6','7',
        0x0E, 0x00, 0x00, 0xFF, 0x7F, 0x00,
        0x3A, 0xEF, '"','"', 0xAF,
        0x3A, 0xF9, 0xC0, '3','2','7','6','8',
        0x0E, 0x00, 0x00, 0x00, 0x80, 0x00,
        0x0D
    };
    static const uint8_t code[] = {
        0xF3,              /* 8000  di */
        0x3E, 0x01,        /* 8001  ld a, 1 */
        0xD3, 0x7E,        /* 8003  out (0x7e), a   ; page RAM in */
        0x3E, 0x42,        /* 8005  ld a, 0x42 */
        0x32, 0x00, 0x20,  /* 8007  ld (0x2000), a  ; marker into CP/M RAM */
        0x3E, 0x00,        /* 800a  ld a, 0 */
        0xD3, 0x7E,        /* 800c  out (0x7e), a   ; page ROM back */
        0x3A, 0x00, 0x20,  /* 800e  ld a, (0x2000)  ; ROM byte */
        0x32, 0x01, 0x40,  /* 8011  ld (0x4001), a */
        0x3E, 0x01,        /* 8014  ld a, 1 */
        0xD3, 0x7E,        /* 8016  out (0x7e), a   ; page RAM in again */
        0x3A, 0x00, 0x20,  /* 8018  ld a, (0x2000)  ; marker survives */
        0x32, 0x00, 0x40,  /* 801b  ld (0x4000), a */
        0x3E, 0x02,        /* 801e  ld a, 2 */
        0xD3, 0xFE,        /* 8020  out (0xfe), a   ; red border */
        0xC3, 0x00, 0x00   /* 8022  jp 0x0000       ; exec in paged RAM */
    };
    uint8_t hdr[17];
    int blen = (int)sizeof basic, clen = (int)sizeof code;
    FILE *f;

    if (argc != 2) {
        fprintf(stderr, "usage: %s OUT.tap\n", argv[0]);
        return 1;
    }
    f = fopen(argv[1], "wb");
    if (!f) {
        fprintf(stderr, "error: cannot write '%s'\n", argv[1]);
        return 1;
    }
    hdr[0] = 0;
    memcpy(hdr + 1, "cpm       ", 10);
    hdr[11] = (uint8_t)blen;  hdr[12] = (uint8_t)(blen >> 8);
    hdr[13] = 10;             hdr[14] = 0;
    hdr[15] = (uint8_t)blen;  hdr[16] = (uint8_t)(blen >> 8);
    tap_block(f, hdr, 17, 0x00);
    tap_block(f, basic, blen, 0xFF);
    hdr[0] = 3;
    memcpy(hdr + 1, "cpm       ", 10);
    hdr[11] = (uint8_t)clen;  hdr[12] = (uint8_t)(clen >> 8);
    hdr[13] = 0x00;           hdr[14] = 0x80;
    hdr[15] = 0x00;           hdr[16] = 0x80;
    tap_block(f, hdr, 17, 0x00);
    tap_block(f, code, clen, 0xFF);
    fclose(f);
    return 0;
}
