/* tap2tzx — convert a .tap to a .tzx that exercises the TZX block zoo:
 * text/archive-info blocks (skipped), a loop around a pure tone, a pulse
 * sequence, a group, and every data block as TURBO (0x11) with standard
 * ROM timings (so the ROM loader still reads it).
 *
 * usage: tap2tzx IN.tap OUT.tzx
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static void w8(FILE *f, unsigned v)  { fputc((int)(v & 0xFF), f); }
static void w16(FILE *f, unsigned v) { w8(f, v); w8(f, v >> 8); }
static void w24(FILE *f, unsigned v) { w8(f, v); w8(f, v >> 8); w8(f, v >> 16); }

int main(int argc, char **argv)
{
    static const char text[] = "hc91emu TZX parser test";
    static uint8_t buf[65536];
    FILE *in, *out;
    size_t size;
    long off = 0;

    if (argc != 3) {
        fprintf(stderr, "usage: %s IN.tap OUT.tzx\n", argv[0]);
        return 1;
    }
    in = fopen(argv[1], "rb");
    if (!in) {
        fprintf(stderr, "error: cannot open '%s'\n", argv[1]);
        return 1;
    }
    size = fread(buf, 1, sizeof buf, in);
    fclose(in);
    out = fopen(argv[2], "wb");
    if (!out) {
        fprintf(stderr, "error: cannot write '%s'\n", argv[2]);
        return 1;
    }

    fwrite("ZXTape!\x1A", 1, 8, out);
    w8(out, 1); w8(out, 20);                  /* rev 1.20 */

    w8(out, 0x30);                            /* text description */
    w8(out, (unsigned)strlen(text));
    fwrite(text, 1, strlen(text), out);

    w8(out, 0x32);                            /* archive info (skipped) */
    w16(out, 5);
    w8(out, 1);                               /* 1 string */
    w8(out, 0x00); w8(out, 2);                /* title, 2 chars */
    fwrite("ok", 1, 2, out);

    w8(out, 0x24); w16(out, 2);               /* loop x2 ... */
    w8(out, 0x12); w16(out, 600); w16(out, 50);   /* pure tone */
    w8(out, 0x25);                            /* ... end loop */

    w8(out, 0x13); w8(out, 2);                /* pulse sequence */
    w16(out, 400); w16(out, 400);

    w8(out, 0x21); w8(out, 4);                /* group "data" */
    fwrite("data", 1, 4, out);

    while (off + 2 <= (long)size) {
        unsigned len = (unsigned)(buf[off] | (buf[off + 1] << 8));
        off += 2;
        if (off + (long)len > (long)size)
            break;
        w8(out, 0x11);                        /* turbo data block */
        w16(out, 2168);                       /* pilot */
        w16(out, 667); w16(out, 735);         /* sync */
        w16(out, 855); w16(out, 1710);        /* bit 0 / bit 1 */
        w16(out, (len && buf[off] < 128) ? 8063 : 3223);
        w8(out, 8);                           /* used bits in last byte */
        w16(out, 1000);                       /* pause ms */
        w24(out, len);
        fwrite(buf + off, 1, len, out);
        off += len;
    }

    w8(out, 0x22);                            /* group end */
    fclose(out);
    return 0;
}
