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
# A: current rounded rocket, three clustered centre windows (default)
"spr_ship0": [
    "........X...............",
    "........X...............",
    "....XX..X...............",
    "...XXXXXXXXXXX..........",
    "..XXXXXXXXXXXXXXXX......",
    ".XXXXXXXXXXXXXXXXXXXX...",
    ".XXXXXXX..X..X..XXXXXXX.",
    "XXXXXXXXXXXXXXXXXXXXXXXX",
    "XXXXXXXXXXXXXXXXXXXXXXXX",
    ".XXXXXXX..X..X..XXXXXXX.",
    ".XXXXXXXXXXXXXXXXXXXX...",
    "..XXXXXXXXXXXXXXXX......",
    "...XXXXXXXXXXX..........",
    "....XX..X...............",
    "........X...............",
    "........X...............",
],
# B: dart interceptor (sleek, single rear window, no antenna)
"spr_ship1": [
    "........................",
    "............XXX.........",
    "..........XXXXXXX.......",
    ".....XXXXXXXXXXXXXXX....",
    "..XXXXXXXXXXXXXXXXXXXX..",
    ".XXXXXXXXXXXXXXXXXXXXXXX",
    "XXXXXX..XXXXXXXXXXXXXXXX",
    "XXXXXX..XXXXXXXXXXXXXXXX",
    ".XXXXXXXXXXXXXXXXXXXXXXX",
    "..XXXXXXXXXXXXXXXXXXXX..",
    ".....XXXXXXXXXXXXXXX....",
    "..........XXXXXXX.......",
    "............XXX.........",
    "........................",
    "........................",
    "........................",
],
# C: heavy cruiser (blocky, big rear fins, two square windows)
"spr_ship2": [
    "..XX....................",
    "..XXXX..................",
    "..XXXXXXXXXXXXXXXXX.....",
    "XXXXXXXXXXXXXXXXXXXXX...",
    "XXXXXXXXXXXXXXXXXXXXXXX.",
    "XXXXX..XXXXXX..XXXXXXXXX",
    "XXXXX..XXXXXX..XXXXXXXXX",
    "XXXXX..XXXXXX..XXXXXXXXX",
    "XXXXX..XXXXXX..XXXXXXXXX",
    "XXXXXXXXXXXXXXXXXXXXXXX.",
    "XXXXXXXXXXXXXXXXXXXXX...",
    "..XXXXXXXXXXXXXXXXX.....",
    "..XXXX..................",
    "..XX....................",
    "........................",
    "........................",
],
# D: manta (wide swept-back wings, slim body)
"spr_ship3": [
    "X.......................",
    "XXX.....................",
    ".XXXXX..................",
    "..XXXXXXXXX.............",
    "...XXXXXXXXXXXXXX.......",
    "....XXXXXXXXXXXXXXXXXX..",
    ".XXXXXXXXX..XXXXXXXXXXXX",
    "XXXXXXXXXXXXXXXXXXXXXXXX",
    ".XXXXXXXXX..XXXXXXXXXXXX",
    "....XXXXXXXXXXXXXXXXXX..",
    "...XXXXXXXXXXXXXX.......",
    "..XXXXXXXXX.............",
    ".XXXXX..................",
    "XXX.....................",
    "X.......................",
    "........................",
],
# E: forward-canopy bomber (antennas, one tall cockpit near the nose)
"spr_ship4": [
    "........X...............",
    "........X...............",
    "....XX..X...............",
    "...XXXXXXXXXXX..........",
    "..XXXXXXXXXXXXXXXX......",
    ".XXXXXXXXXXXXX..XXXXX...",
    ".XXXXXXXXXXXXX..XXXXXXX.",
    "XXXXXXXXXXXXXX..XXXXXXXX",
    "XXXXXXXXXXXXXX..XXXXXXXX",
    ".XXXXXXXXXXXXX..XXXXXXX.",
    ".XXXXXXXXXXXXX..XXXXX...",
    "..XXXXXXXXXXXXXXXX......",
    "...XXXXXXXXXXX..........",
    "....XX..X...............",
    "........X...............",
    "........X...............",
],
# F: striped hull (rocket with dashed horizontal panel lines)
"spr_ship5": [
    "........X...............",
    "........X...............",
    "....XX..X...............",
    "...XXXXXXXXXXX..........",
    "..XXXXXXXXXXXXXXXX......",
    ".XXXXXXXXXXXXXXXXXXXX...",
    ".XX.XX.XX.XX.XX.XXX.XXX.",
    "XXXXXXXXXXXXXXXXXXXXXXXX",
    "XXXXXXXXXXXXXXXXXXXXXXXX",
    ".XX.XX.XX.XX.XX.XXX.XXX.",
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
# Boss 2: angular dreadnought
"spr_boss1": [
    "........................",
    "..XXXXXX................",
    "..XXXXXXXXX.............",
    ".XXXXXXXXXXXXX..........",
    ".XXXX..XXXXXXXXXXX......",
    "XXXXX..XXXXXXXXXXXXXX...",
    "XXXX....XXXXXXXXXXXXXXX.",
    "XXX......XXXXXXXXXXXXXXX",
    "XXX......XXXXXXXXXXXXXXX",
    "XXXX....XXXXXXXXXXXXXXX.",
    "XXXXX..XXXXXXXXXXXXXX...",
    ".XXXX..XXXXXXXXXXX......",
    ".XXXXXXXXXXXXX..........",
    "..XXXXXXXXX.............",
    "..XXXXXX................",
    "........................",
],
# Boss 3: round battle-saucer
"spr_boss2": [
    "..........XXXX..........",
    ".......XXXXXXXXXX.......",
    ".....XXXXXXXXXXXXXX.....",
    "...XXXXXXXXXXXXXXXXXX...",
    "..XXXXXXXXXXXXXXXXXXXX..",
    ".XXXXX..XXXXXXXX..XXXXX.",
    "XXXXX....XXXXXX....XXXXX",
    "XXXX......XXXX......XXXX",
    "XXXX......XXXX......XXXX",
    "XXXXX....XXXXXX....XXXXX",
    ".XXXXX..XXXXXXXX..XXXXX.",
    "..XXXXXXXXXXXXXXXXXXXX..",
    "...XXXXXXXXXXXXXXXXXX...",
    ".....XXXXXXXXXXXXXX.....",
    ".......XXXXXXXXXX.......",
    "..........XXXX..........",
],
# Boss 4: spiked warlord
"spr_boss3": [
    "....X.......X.......X...",
    "..XXXXX...XXXXX...XXXX..",
    ".XXXXXXXXXXXXXXXXXXXXX..",
    "XXXXXXXXXXXXXXXXXXXXXXX.",
    "XXXX..XXXXXXXXXXX..XXXXX",
    "XXX....XXXXXXXXX....XXXX",
    "XXX....XXXXXXXXX....XXXX",
    "XXXX..XXXXXXXXXXX..XXXXX",
    "XXXXXXXXXXXXXXXXXXXXXXX.",
    "XXXX..XXXXXXXXXXX..XXXXX",
    "XXX....XXXXXXXXX....XXXX",
    "XXX....XXXXXXXXX....XXXX",
    "XXXX..XXXXXXXXXXX..XXXXX",
    "XXXXXXXXXXXXXXXXXXXXXXX.",
    ".XXXXXXXXXXXXXXXXXXXXX..",
    "..XXXXX...XXXXX...XXXX..",
],
# Diver enemy: a swept attacker that swoops at the ship
"spr_diver": [
    "................",
    "XX..............",
    "XXXX............",
    ".XXXXX..........",
    "..XXXXXX........",
    "...XXXXXXX......",
    "....XXXXXXXX....",
    ".....XXXXXXXXX..",
    "......XXXXXXXXX.",
    "....XXXXXXXXXXX.",
    "..XXXXXXXXXXX...",
    ".XXXXXXXX.......",
    "XXXXXX.........",
    "XXXX...........",
    "XX.............",
    "................",
],
# Turret: a domed wall-gun (sits on ceiling/floor)
"spr_turret": [
    "................",
    "................",
    ".....XXXX.......",
    "....XXXXXX......",
    "...XXXXXXXX.....",
    "..XXXXXXXXXX....",
    "..XX.XXXX.XX....",
    "..XXXXXXXXXX....",
    ".XXXXXXXXXXXX...",
    ".XX.XX..XX.XX...",
    ".XXXXXXXXXXXX...",
    "XXXXXXXXXXXXXX..",
    "XXXXXXXXXXXXXX..",
    "XXX.XX..XX.XXX..",
    "XXXXXXXXXXXXXX..",
    "................",
],
# Mine: a spiked drifting ball
"spr_mine": [
    "................",
    ".......XX.......",
    "...X...XX...X...",
    "...XX..XX..XX...",
    "....XX.XX.XX....",
    "XX...XXXXXX...XX",
    ".XXX.XXXXXX.XXX.",
    "...XXXXXXXXXX...",
    "...XXXXXXXXXX...",
    ".XXX.XXXXXX.XXX.",
    "XX...XXXXXX...XX",
    "....XX.XX.XX....",
    "...XX..XX..XX...",
    "...X...XX...X...",
    ".......XX.......",
    "................",
],
# Homing drone: small, compact craft
"spr_drone": [
    "................",
    "................",
    "....XX..XX......",
    "...XXX..XXX.....",
    "....XXXXXX......",
    "..XXXXXXXXXX....",
    ".XXXX.XX.XXXX...",
    ".XXXXXXXXXXXX...",
    ".XXXXXXXXXXXX...",
    ".XXXX.XX.XXXX...",
    "..XXXXXXXXXX....",
    "....XXXXXX......",
    "...XXX..XXX.....",
    "....XX..XX......",
    "................",
    "................",
],
# Distant planet (drifts in the far background, dim ink on black)
"spr_planet": [
    ".....XXXXXX.....",
    "...XXXXXXXXXX...",
    "..XXXXXXX.XXXX..",
    ".XXXXXXXXXXXXXX.",
    ".XXX.XXXXXXXXXX.",
    "XXXXXXXXXXX.XXXX",
    "XXXXXX.XXXXXXXXX",
    "XXXXXXXXXXXXXXXX",
    "XXXXXXXXX.XXXXXX",
    "XXXX.XXXXXXXXXXX",
    ".XXXXXXXXXX.XXX.",
    ".XXXXXX.XXXXXXX.",
    "..XXXXXXXXXXXX..",
    "...XXXXXXXXXX...",
    ".....XXXXXX.....",
    "................",
],
# Exhaust flame frame 1 (short), pointing left from the ship tail
"spr_flame1": [
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    ".....XXXX.......",
    "...XXXXXXX......",
    "...XXXXXXX......",
    ".....XXXX.......",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
],
# Exhaust flame frame 2 (long flare)
"spr_flame2": [
    "................",
    "................",
    "................",
    "................",
    "................",
    "......XXX.......",
    "..XXXXXXXX......",
    "XXXXXXXXXXX.....",
    "XXXXXXXXXXX.....",
    "..XXXXXXXX......",
    "......XXX.......",
    "................",
    "................",
    "................",
    "................",
    "................",
],
# Smart-bomb / debris spark (tiny)
"spr_spark": [
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    ".......XX.......",
    ".......XX.......",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
],
}


def shear(art, direction):
    """Vertical shear of a 24-wide ship for a banking look.
    direction +1 = climb (nose up), -1 = dive (nose down)."""
    w = max(len(r) for r in art)
    h = len(art)
    grid = [[(art[y][x] if x < len(art[y]) else '.') for x in range(w)]
            for y in range(h)]
    out = [['.'] * w for _ in range(h)]
    cx = (w - 1) / 2.0
    for x in range(w):
        vs = int(round((x - cx) / 7.0)) * (-direction)  # nose leads the bank
        for y in range(h):
            ny = y + vs
            if 0 <= ny < h and grid[y][x] in "X#":
                out[ny][x] = 'X'
    return ["".join(r) for r in out]


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
    # generate climb/dive banking variants for each player ship (0..5)
    for i in range(6):
        base = SPRITES["spr_ship%d" % i]
        SPRITES["spr_ship%d_up" % i] = shear(base, +1)
        SPRITES["spr_ship%d_dn" % i] = shear(base, -1)
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
