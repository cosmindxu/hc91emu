/* mkicon — render the hc91emu application icon from the machine's own
 * ROM character set: "HC" in the Spectrum font, white on black, with
 * the four-colour diagonal stripes across the lower-right corner.
 * Any square size renders cleanly (the deb packaging uses 256/128/48).
 *
 *   mkicon ROM OUT.png SIZE [OUT.png SIZE]...
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

int png_write(const char *path, const uint32_t *rgba, int w, int h);

static uint8_t font[768];            /* chars 32..127, 8 bytes each */

/* Pixels are framebuffer-format uint32: bytes R,G,B,A (LE). */
#define C_BLACK  0xFF000000u
#define C_WHITE  0xFFFFFFFFu

static void draw_char(uint32_t *px, int S, int x0, int y0, int sc, char c)
{
    const uint8_t *g = font + ((unsigned char)c - 32) * 8;
    int r, b, dy, dx;
    for (r = 0; r < 8; r++)
        for (b = 0; b < 8; b++) {
            int x, y;
            if (!(g[r] & (0x80u >> b)))
                continue;
            for (dy = 0; dy < sc; dy++)
                for (dx = 0; dx < sc; dx++) {
                    x = x0 + b * sc + dx;
                    y = y0 + r * sc + dy;
                    if (x >= 0 && x < S && y >= 0 && y < S)
                        px[y * S + x] = C_WHITE;
                }
        }
}

static void render(uint32_t *px, int S)
{
    static const uint32_t stripe[4] = {
        0xFF0000FFu, 0xFF00FFFFu, 0xFF00FF00u, 0xFFFFFF00u
    };                               /* red, yellow, green, cyan */
    int x, y, sc, x0, y0;
    int b0 = (S * 27) / 20;          /* stripes cross x+y in [b0,b0+4w) */
    int w = S / 18 > 0 ? S / 18 : 1;

    for (y = 0; y < S; y++)
        for (x = 0; x < S; x++) {
            int c = x + y, k = (c - b0) / w;
            px[y * S + x] = (c >= b0 && k < 4) ? stripe[k] : C_BLACK;
        }

    sc = (S * 5 + 64) / 128 > 0 ? (S * 5 + 64) / 128 : 1;
    x0 = S / 8;                      /* "HC" is 2 cells = 16 px wide,  */
    y0 = S / 6;                      /* up-left, clear of the stripes  */
    draw_char(px, S, x0, y0, sc, 'H');
    draw_char(px, S, x0 + 8 * sc, y0, sc, 'C');
}

int main(int argc, char **argv)
{
    FILE *f;
    uint8_t rom[16384];
    int i;

    if (argc < 4 || (argc - 2) % 2 != 0) {
        fprintf(stderr, "usage: %s ROM OUT.png SIZE [OUT.png SIZE]...\n",
                argv[0]);
        return 1;
    }
    f = fopen(argv[1], "rb");
    if (!f || fread(rom, 1, sizeof rom, f) != sizeof rom) {
        fprintf(stderr, "error: cannot read 16K ROM '%s'\n", argv[1]);
        return 1;
    }
    fclose(f);
    memcpy(font, rom + 0x3D00, sizeof font);

    for (i = 2; i + 1 < argc; i += 2) {
        int S = atoi(argv[i + 1]);
        uint32_t *px;
        if (S < 16 || S > 1024) {
            fprintf(stderr, "error: bad size '%s'\n", argv[i + 1]);
            return 1;
        }
        px = malloc((size_t)S * S * 4);
        if (!px)
            return 1;
        render(px, S);
        if (png_write(argv[i], px, S, S) != 0) {
            fprintf(stderr, "error: cannot write '%s'\n", argv[i]);
            return 1;
        }
        free(px);
        printf("%s (%dx%d)\n", argv[i], S, S);
    }
    return 0;
}
