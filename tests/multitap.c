/* multitap — emit a .tap with a miniature multicolour engine: each frame
 * it HALT-syncs to the interrupt, burns ~23.4k T to reach the beam lines
 * of attribute cell (row 8, col 0), then rewrites that cell's attribute
 * every ~226 T (about one scanline) for 24 lines. With beam-accurate
 * rendering the single cell shows several paper colors stacked in its 8
 * pixel rows; with frame-at-once rendering it is one solid color.
 *
 * usage: multitap OUT.tap
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
        0x00, 0x0A, 0x20, 0x00,                          /* line 10, len 32 */
        0xFD, '3','2','7','6','7',                       /* CLEAR 32767 */
        0x0E, 0x00, 0x00, 0xFF, 0x7F, 0x00,
        0x3A, 0xEF, '"','"', 0xAF,                       /* :LOAD "" CODE */
        0x3A, 0xF9, 0xC0, '3','2','7','6','8',           /* :RANDOMIZE USR */
        0x0E, 0x00, 0x00, 0x00, 0x80, 0x00,
        0x0D
    };
    static const uint8_t code[] = {
        0x0E, 0x07,        /* 8000  ld c, 7        ; attr value */
        0x21, 0x00, 0x59,  /* 8002  ld hl, 0x5900  ; attr row 8, col 0 */
        0xFB,              /* 8005  ei */
        0x76,              /* 8006  halt           ; sync to frame */
        0x11, 0x84, 0x03,  /* 8007  ld de, 900     ; ~23.4k T delay */
        0x1B,              /* 800a  dec de */
        0x7A,              /* 800b  ld a, d */
        0xB3,              /* 800c  or e */
        0x20, 0xFB,        /* 800d  jr nz, 800a */
        0x06, 0x18,        /* 800f  ld b, 24       ; 24 scanline bands */
        0x71,              /* 8011  ld (hl), c     ; recolor the cell */
        0x79,              /* 8012  ld a, c */
        0xC6, 0x08,        /* 8013  add a, 8       ; next paper color */
        0xE6, 0x3F,        /* 8015  and 0x3f       ; no BRIGHT/FLASH */
        0x4F,              /* 8017  ld c, a */
        0x3E, 0x0B,        /* 8018  ld a, 11       ; pad to ~226 T/band */
        0x3D,              /* 801a  dec a */
        0x20, 0xFD,        /* 801b  jr nz, 801a */
        0x10, 0xF2,        /* 801d  djnz 8011 */
        0x18, 0xE5         /* 801f  jr 8006 */
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
    memcpy(hdr + 1, "multi     ", 10);
    hdr[11] = (uint8_t)blen;  hdr[12] = (uint8_t)(blen >> 8);
    hdr[13] = 10;             hdr[14] = 0;
    hdr[15] = (uint8_t)blen;  hdr[16] = (uint8_t)(blen >> 8);
    tap_block(f, hdr, 17, 0x00);
    tap_block(f, basic, blen, 0xFF);

    hdr[0] = 3;
    memcpy(hdr + 1, "multi     ", 10);
    hdr[11] = (uint8_t)clen;  hdr[12] = (uint8_t)(clen >> 8);
    hdr[13] = 0x00;           hdr[14] = 0x80;
    hdr[15] = 0x00;           hdr[16] = 0x80;
    tap_block(f, hdr, 17, 0x00);
    tap_block(f, code, clen, 0xFF);

    fclose(f);
    return 0;
}
