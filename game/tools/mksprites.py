#!/usr/bin/env python3
"""Generate Z80 sprite data (data+mask, 16x16) from ASCII art.

Each sprite is 16 wide x 16 tall. In the art:
    'X' or '#'  -> solid pixel  (data bit 1, mask bit 0)
    anything else -> transparent (data bit 0, mask bit 1)

Output rows are emitted as 4 bytes per scan line:
    data_hi, data_lo, mask_hi, mask_lo
so the masked blitter can do:  screen = (screen AND mask) OR data
(after both are shifted together by the sub-cell x offset).
"""

SPRITES = {
# Player ship: a 24-wide side-on rocket, nose to the right, with an
# antenna, cockpit/panel-line windows, swept rear fins and a tail
# exhaust (Chronos-inspired). Travel is left-to-right.
"spr_ship": [
    "........X...............",
    "........X...............",
    "....XX..X...............",
    "...XXXXXXXXXXX..........",
    "..XXXXXXXXXXXXXXXX......",
    ".XXXXXXXXXXXXXXXXXXXX...",
    ".XXXX..XXXX..XXXX..XXXX.",
    "XXXXXXXXXXXXXXXXXXXXXXXX",
    "XXXXXXXXXXXXXXXXXXXXXXXX",
    ".XXXX..XXXX..XXXX..XXXX.",
    ".XXXXXXXXXXXXXXXXXXXX...",
    "..XXXXXXXXXXXXXXXX......",
    "...XXXXXXXXXXX..........",
    "....XX..X...............",
    "........X...............",
    "........X...............",
],
# Asteroid / rock hazard
"spr_rock": [
    "................",
    "....XXXXXX......",
    "...XXXXXXXXX....",
    "..XXXXXXXXXXX...",
    ".XXXXXXX.XXXXX..",
    ".XXXXXX...XXXX..",
    "XXXXXXX..XXXXXX.",
    "XXXXXXXXXXXXXXX.",
    "XXXXX.XXXXXXXXX.",
    "XXXX...XXXXXXXX.",
    ".XXXX..XXXXXXX..",
    ".XXXXXXXXXXXXX..",
    "..XXXXXXXXXXX...",
    "...XXXXXXXXX....",
    "....XXXXXX......",
    "................",
],
# Enemy craft, pointing left (it flies toward the player)
"spr_enemy": [
    "................",
    ".........XX.....",
    "........XXX.....",
    ".......XXXX..X..",
    ".....XXXXXXXXX..",
    "...XXXXXXXXXXXX.",
    ".XXXXXXXXXXXXXX.",
    "XXXXXXXXXXXXXXXX",
    "XXXXXXXXXXXXXXXX",
    ".XXXXXXXXXXXXXX.",
    "...XXXXXXXXXXXX.",
    ".....XXXXXXXXX..",
    ".......XXXX..X..",
    "........XXX.....",
    ".........XX.....",
    "................",
],
# Player bullet (short bright dash, centred vertically)
"spr_bullet": [
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "...XXXXXX.......",
    "...XXXXXX.......",
    "...XXXXXX.......",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
],
# Collectible crystal
"spr_crystal": [
    "................",
    ".......XX.......",
    "......XXXX......",
    ".....XX..XX.....",
    "....XX....XX....",
    "...XX..XX..XX...",
    "..XX..XXXX..XX..",
    ".XX..XX..XX..XX.",
    ".XX..XX..XX..XX.",
    "..XX..XXXX..XX..",
    "...XX..XX..XX...",
    "....XX....XX....",
    ".....XX..XX.....",
    "......XXXX......",
    ".......XX.......",
    "................",
],
# Ship, exhaust frame B (shorter tail flame) - alternated with spr_ship
"spr_shipb": [
    "........X...............",
    "........X...............",
    "....XX..X...............",
    "...XXXXXXXXXXX..........",
    "..XXXXXXXXXXXXXXXX......",
    ".XXXXXXXXXXXXXXXXXXXX...",
    "..XXX..XXXX..XXXX..XXXX.",
    ".XXXXXXXXXXXXXXXXXXXXXXX",
    ".XXXXXXXXXXXXXXXXXXXXXXX",
    "..XXX..XXXX..XXXX..XXXX.",
    ".XXXXXXXXXXXXXXXXXXXX...",
    "..XXXXXXXXXXXXXXXX......",
    "...XXXXXXXXXXX..........",
    "....XX..X...............",
    "........X...............",
    "........X...............",
],
# Asteroid, rotated frame (spins with spr_rock)
"spr_rock2": [
    "................",
    "......XXXXX.....",
    "....XXXXXXXXX...",
    "...XXX.XXXXXX...",
    "..XXXX..XXXXXX..",
    "..XXXXXXXXX.XX..",
    ".XXXXX.XXXXXXXX.",
    ".XXXXXXXXXX.XXX.",
    ".XXX.XXXXXXXXXX.",
    ".XXXXXXX.XXXXXX.",
    "..XX.XXXXXXXXX..",
    "..XXXXXX..XXXX..",
    "...XXXXXX.XXX...",
    "....XXXXXXXXX...",
    "......XXXXX.....",
    "................",
],
# Crystal, contracted pulse frame
"spr_crystal2": [
    "................",
    "................",
    ".......XX.......",
    "......XXXX......",
    ".....XX..XX.....",
    "....XX.XX.XX....",
    "....X.XXXX.X....",
    "....XX....XX....",
    "....XX....XX....",
    "....X.XXXX.X....",
    "....XX.XX.XX....",
    ".....XX..XX.....",
    "......XXXX......",
    ".......XX.......",
    "................",
    "................",
],
# Power-up pod (capsule with a cross marking)
"spr_power": [
    "................",
    "......XXXX......",
    "....XXXXXXXX....",
    "...XXX.XX.XXX...",
    "..XXX..XX..XXX..",
    "..XX...XX...XX..",
    "..XXXXXXXXXXXX..",
    "..XXXXXXXXXXXX..",
    "..XX...XX...XX..",
    "..XXX..XX..XXX..",
    "...XXX.XX.XXX...",
    "....XXXXXXXX....",
    "......XXXX......",
    "................",
    "................",
    "................",
],
# Enemy bullet (small round tracer)
"spr_ebullet": [
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "......XXXX......",
    "......XXXX......",
    "......XXXX......",
    "......XXXX......",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
],
# Explosion, frame 1 (small)
"spr_expl1": [
    "................",
    "................",
    "................",
    "................",
    "................",
    "......XXXX......",
    ".....XXXXXX.....",
    ".....XXXXXX.....",
    ".....XXXXXX.....",
    "......XXXX......",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
],
# Explosion, frame 2 (medium, ragged)
"spr_expl2": [
    "................",
    "................",
    "...X.XXXX.X.....",
    "....XXXXXXX.....",
    "...XXXXXXXXX....",
    "..XXXX..XXXX....",
    "..XXX....XXX....",
    "..XXX....XXXX...",
    "..XXXX..XXXX....",
    "...XXXXXXXXX....",
    "....XXXXXXX.....",
    "...X.XXXX.X.....",
    "................",
    "................",
    "................",
    "................",
],
# Explosion, frame 3 (big burst)
"spr_expl3": [
    "..X..X..X..X...X",
    "X..XXX.XXX.XX.X.",
    ".XXXXXXXXXXXXXX.",
    "X.XXX.XXXX.XXX.X",
    ".XXXXXXXXXXXXXXX",
    "XXXX.XX..XX.XXXX",
    "X.XXX......XXX.X",
    "XXXX........XXXX",
    "X.XXX......XXX.X",
    "XXXX.XX..XX.XXXX",
    ".XXXXXXXXXXXXXXX",
    "X.XXX.XXXX.XXX.X",
    ".XXXXXXXXXXXXXX.",
    "X..XXX.XXX.XX.X.",
    "..X..X..X..X...X",
    "................",
],
# Boss craft: a big 24-wide cruiser facing left (toward the player)
"spr_boss": [
    "........................",
    "....XX..................",
    "...XXXXX................",
    "..XXXXXXXXXX............",
    ".XXXXXXXXXXXXXXXX........",
    ".XXXXX..XXXXXXXXXXXXX....",
    "XXXXX....XXXXXXXXXXXXXX..",
    "XXXX......XXXXXXXXXXXXXXX",
    "XXXX......XXXXXXXXXXXXXXX",
    "XXXXX....XXXXXXXXXXXXXX..",
    ".XXXXX..XXXXXXXXXXXXX....",
    ".XXXXXXXXXXXXXXXX........",
    "..XXXXXXXXXX............",
    "...XXXXX................",
    "....XX..................",
    "........................",
],
}


def row_bytes(line, w):
    line = (line + "." * w)[:w]
    nb = w // 8
    data, mask = [], []
    for b in range(nb):
        d = 0
        for bit in range(8):
            if line[b * 8 + bit] in "X#":
                d |= 1 << (7 - bit)
        data.append(d)
        mask.append((~d) & 0xFF)
    return data + mask          # all data bytes, then all mask bytes


def main():
    out = ["; Generated by tools/mksprites.py - do not edit by hand.",
           "; Per row: width/8 data bytes, then width/8 mask bytes.", ""]
    for name, art in SPRITES.items():
        w = max(16, ((max(len(r) for r in art) + 7) // 8) * 8)
        out.append("%s:  ; %d wide" % (name, w))
        for line in art:
            vals = row_bytes(line, w)
            out.append("        db  " + ",".join("%3d" % v for v in vals))
        out.append("")
    import os
    dest = os.path.join(os.path.dirname(__file__), "..", "src", "sprites.inc")
    dest = os.path.normpath(dest)
    open(dest, "w").write("\n".join(out))
    print("wrote %s (%d sprites)" % (dest, len(SPRITES)))


if __name__ == "__main__":
    main()
