/* bordertap — emit a .tap whose program cycles the border color in a
 * tight loop (LD A,C / OUT (0xFE),A / INC C / JR), ~31 T per OUT. With a
 * beam-accurate renderer the final screenshot shows diagonal rainbow
 * stripes in the border; a frame-at-once renderer shows a single color.
 *
 * usage: bordertap OUT.tap
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
    static const uint8_t code[] = {
        0xF3,             /* di */
        0x0E, 0x00,       /* ld c,0 */
        0x79,             /* loop: ld a,c */
        0xD3, 0xFE,       /* out (0xfe),a */
        0x0C,             /* inc c */
        0x18, 0xFA        /* jr loop */
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

    /* program header: type 0, autostart line 10 */
    hdr[0] = 0;
    memcpy(hdr + 1, "border    ", 10);
    hdr[11] = (uint8_t)blen;  hdr[12] = (uint8_t)(blen >> 8);
    hdr[13] = 10;             hdr[14] = 0;        /* autostart LINE 10 */
    hdr[15] = (uint8_t)blen;  hdr[16] = (uint8_t)(blen >> 8);
    tap_block(f, hdr, 17, 0x00);
    tap_block(f, basic, blen, 0xFF);

    /* code header: type 3, start 0x8000 */
    hdr[0] = 3;
    memcpy(hdr + 1, "border    ", 10);
    hdr[11] = (uint8_t)clen;  hdr[12] = (uint8_t)(clen >> 8);
    hdr[13] = 0x00;           hdr[14] = 0x80;     /* start address */
    hdr[15] = 0x00;           hdr[16] = 0x80;
    tap_block(f, hdr, 17, 0x00);
    tap_block(f, code, clen, 0xFF);

    fclose(f);
    return 0;
}
