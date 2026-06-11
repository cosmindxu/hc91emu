/* fbcheck — assert that a raw 320x240 RGBA dump (--fb-dump) shows
 * mid-frame border activity: many distinct colors and many vertical
 * color transitions in the left and right border columns. A frame-at-once
 * renderer (single border color) fails this.
 *
 * usage: fbcheck DUMP.raw
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define W 320
#define H 240

static uint32_t fb[W * H];

static int column_stats(int x, const char *name)
{
    uint32_t seen[64];
    int nseen = 0, trans = 0, y, i;

    for (y = 0; y < H; y++) {
        uint32_t c = fb[y * W + x];
        int known = 0;
        if (y && c != fb[(y - 1) * W + x])
            trans++;
        for (i = 0; i < nseen; i++)
            if (seen[i] == c) { known = 1; break; }
        if (!known && nseen < 64)
            seen[nseen++] = c;
    }
    printf("fbcheck: column x=%-3d (%s): %d distinct colors, %d transitions\n",
           x, name, nseen, trans);
    return nseen >= 5 && trans >= 24;
}

/* count distinct colors in rows y0..y0+7 at column x (one attr cell) */
static int cell_bands(int y0, int x)
{
    uint32_t seen[8];
    int nseen = 0, y, i;
    for (y = y0; y < y0 + 8; y++) {
        uint32_t c = fb[y * W + x];
        int known = 0;
        for (i = 0; i < nseen; i++)
            if (seen[i] == c) { known = 1; break; }
        if (!known)
            seen[nseen++] = c;
    }
    printf("fbcheck: cell rows %d..%d x=%d: %d distinct colors\n",
           y0, y0 + 7, x, nseen);
    return nseen;
}

int main(int argc, char **argv)
{
    FILE *f;
    if (argc < 2) {
        fprintf(stderr,
            "usage: %s DUMP.raw            border-stripe check\n"
            "       %s DUMP.raw --band Y0 X MIN  multicolour cell check\n",
            argv[0], argv[0]);
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (!f || fread(fb, 4, W * H, f) != W * H) {
        fprintf(stderr, "error: cannot read %dx%d RGBA from '%s'\n",
                W, H, argv[1]);
        if (f) fclose(f);
        return 2;
    }
    fclose(f);

    if (argc == 6 && !strcmp(argv[2], "--band")) {
        int y0 = atoi(argv[3]), x = atoi(argv[4]), min = atoi(argv[5]);
        if (cell_bands(y0, x) >= min)
            return 0;
        printf("fbcheck: FAILED (cell not multicoloured)\n");
        return 1;
    }

    if (column_stats(5, "left border") && column_stats(315, "right border"))
        return 0;
    printf("fbcheck: FAILED (border looks static)\n");
    return 1;
}
