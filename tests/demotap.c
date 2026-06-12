/* demotap — emit a .tap with a self-contained "multicolour demo": a
 * static paper gradient (24 horizontal colour bands) plus a border that
 * is recoloured once per scanline, so the visible border shows a stack
 * of ~240 colour bars. The whole effect is purely timing-driven and
 * input-free, hence bit-for-bit deterministic — a beam-accurate
 * renderer reproduces the stacked bars exactly, a frame-at-once
 * renderer collapses the border to a single colour. The suite
 * golden-freezes the rendered PNG.
 *
 * usage: demotap OUT.tap
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
        /* ---- one-time: clear the bitmap so paper colour fills cells ---- */
        0x21, 0x00, 0x40,  /* 8000  ld hl, 0x4000     ; bitmap start */
        0x11, 0x01, 0x40,  /* 8003  ld de, 0x4001 */
        0x01, 0xFF, 0x17,  /* 8006  ld bc, 0x17FF     ; 6143 */
        0x36, 0x00,        /* 8009  ld (hl), 0 */
        0xED, 0xB0,        /* 800b  ldir              ; clear pixels */
        /* ---- one-time: paint a 24-band paper gradient ---- */
        0x21, 0x00, 0x58,  /* 800d  ld hl, 0x5800     ; attribute file */
        0x0E, 0x18,        /* 8010  ld c, 24          ; rows */
        /* rowloop: */
        0x3E, 0x18,        /* 8012  ld a, 24 */
        0x91,              /* 8014  sub c             ; a = row 0..23 */
        0xE6, 0x07,        /* 8015  and 7 */
        0x07, 0x07, 0x07,  /* 8017  rlca x3           ; -> paper bits 3-5 */
        0xF6, 0x47,        /* 801a  or 0x47           ; BRIGHT+white ink */
        0x06, 0x20,        /* 801c  ld b, 32          ; cells per row */
        /* cellloop: */
        0x77,              /* 801e  ld (hl), a */
        0x23,              /* 801f  inc hl */
        0x10, 0xFC,        /* 8020  djnz 801e */
        0x0D,              /* 8022  dec c */
        0x20, 0xED,        /* 8023  jr nz, 8012 */
        /* ---- main: border bars, recoloured every scanline ---- */
        /* main: */
        0xFB,              /* 8025  ei */
        0x76,              /* 8026  halt              ; sync to frame top */
        0xAF,              /* 8027  xor a             ; colour 0 */
        0x06, 0xF0,        /* 8028  ld b, 240         ; visible lines */
        /* lineloop: */
        0xD3, 0xFE,        /* 802a  out (0xFE), a     ; 11T  border = a */
        0x4F,              /* 802c  ld c, a           ; save colour */
        0x1E, 0x0B,        /* 802d  ld e, 11          ; line-length pad */
        /* delay: */
        0x1D,              /* 802f  dec e */
        0x20, 0xFD,        /* 8030  jr nz, 802f */
        0x00,              /* 8032  nop               ; +4T trim */
        0x00,              /* 8033  nop               ; +4T trim */
        0x79,              /* 8034  ld a, c           ; restore colour */
        0x3C,              /* 8035  inc a */
        0xE6, 0x07,        /* 8036  and 7             ; 8-colour rainbow */
        0x10, 0xF0,        /* 8038  djnz 802a */
        0x18, 0xE9         /* 803a  jr 8025           ; next frame */
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
    memcpy(hdr + 1, "demo      ", 10);
    hdr[11] = (uint8_t)blen;  hdr[12] = (uint8_t)(blen >> 8);
    hdr[13] = 10;             hdr[14] = 0;
    hdr[15] = (uint8_t)blen;  hdr[16] = (uint8_t)(blen >> 8);
    tap_block(f, hdr, 17, 0x00);
    tap_block(f, basic, blen, 0xFF);

    hdr[0] = 3;
    memcpy(hdr + 1, "demo      ", 10);
    hdr[11] = (uint8_t)clen;  hdr[12] = (uint8_t)(clen >> 8);
    hdr[13] = 0x00;           hdr[14] = 0x80;
    hdr[15] = 0x00;           hdr[16] = 0x80;
    tap_block(f, hdr, 17, 0x00);
    tap_block(f, code, clen, 0xFF);

    fclose(f);
    return 0;
}
