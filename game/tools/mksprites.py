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


def sprite_bytes(name, art):
    """Flat byte list for one sprite: 16 rows x (width/8 data + width/8 mask)."""
    w = max(16, ((max(len(r) for r in art) + 7) // 8) * 8)
    out = []
    for line in art:
        out += row_bytes(line, w)
    return out, w


# ---- LZSS codec --------------------------------------------------------------
# Stream = repeated [flag byte][8 items]. Flag bits MSB-first: 1=literal (1 byte
# follows), 0=match (2 bytes: V=lo|hi<<8; length=(V&0x0F)+3, offset=(V>>4)+1,
# copied from already-output data). The Z80 depacker in src/game.asm decodes
# this exact format; compress()/decompress() here must agree with it.
MINM, MAXM, MAXOFF = 3, 18, 4096


def lz_compress(data):
    items = []                       # ('l',b) or ('m',vlo,vhi)
    heads = {}                       # 3-byte prefix -> list of positions
    i, n = 0, len(data)
    while i < n:
        best_len, best_off = 0, 0
        if i + MINM <= n:
            key = bytes(data[i:i + 3])
            for j in reversed(heads.get(key, [])):
                if i - j > MAXOFF:
                    break
                l = 0
                m = min(MAXM, n - i)
                while l < m and data[j + l] == data[i + l]:
                    l += 1
                if l > best_len:
                    best_len, best_off = l, i - j
                    if l == MAXM:
                        break
        if best_len >= MINM:
            V = ((best_off - 1) << 4) | (best_len - MINM)
            items.append(('m', V & 0xFF, (V >> 8) & 0xFF))
            adv = best_len
        else:
            items.append(('l', data[i]))
            adv = 1
        for k in range(i, i + adv):
            if k + MINM <= n:
                heads.setdefault(bytes(data[k:k + 3]), []).append(k)
        i += adv
    # pack items into flag-byte groups
    out = bytearray()
    for g in range(0, len(items), 8):
        group = items[g:g + 8]
        flags = 0
        for b, it in enumerate(group):
            if it[0] == 'l':
                flags |= 0x80 >> b
        out.append(flags)
        for it in group:
            if it[0] == 'l':
                out.append(it[1])
            else:
                out += bytes((it[1], it[2]))
    return bytes(out)


def lz_decompress(packed, rawlen):
    out = bytearray()
    i = 0
    while len(out) < rawlen:
        flags = packed[i]; i += 1
        for b in range(8):
            if len(out) >= rawlen:
                break
            if flags & (0x80 >> b):
                out.append(packed[i]); i += 1
            else:
                V = packed[i] | (packed[i + 1] << 8); i += 2
                length = (V & 0x0F) + MINM
                off = (V >> 4) + 1
                for _ in range(length):
                    out.append(out[-off])
    return bytes(out)


def main():
    # generate climb/dive banking variants for each player ship (0..5)
    for i in range(6):
        base = SPRITES["spr_ship%d" % i]
        SPRITES["spr_ship%d_up" % i] = shear(base, +1)
        SPRITES["spr_ship%d_dn" % i] = shear(base, -1)

    # Flatten all sprites into one raw stream, recording each label's offset.
    raw = bytearray()
    offs = []
    for name, art in SPRITES.items():
        b, w = sprite_bytes(name, art)
        offs.append((name, len(raw), w))
        raw += bytes(b)
    rawlen = len(raw)
    # SPRBASE is 0x6300 in src/game.asm; the unpacked block must stay below
    # 0x8000 (and above the scratch BSS that ends ~0x62F2).
    assert rawlen <= 0x8000 - 0x6300, "sprite data %d > low-RAM window" % rawlen

    packed = lz_compress(raw)
    assert lz_decompress(packed, rawlen) == bytes(raw), "LZ round-trip mismatch!"

    import os
    srcdir = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "src"))

    # sprites.inc: EQU each label at SPRBASE + offset (no data emitted).
    inc = ["; Generated by tools/mksprites.py - do not edit by hand.",
           "; Sprite labels are EQUs into the unpacked low-RAM block (SPRBASE);",
           "; the bytes live compressed in sprpack.inc, depacked at startup.",
           "SPR_RAWLEN equ %d" % rawlen, ""]
    for name, off, w in offs:
        inc.append("%-14s equ SPRBASE + %d   ; %d wide" % (name, off, w))
    inc.append("")
    open(os.path.join(srcdir, "sprites.inc"), "w").write("\n".join(inc))

    # sprpack.inc: the compressed blob.
    pk = ["; Generated by tools/mksprites.py - do not edit by hand.",
          "; LZSS-packed sprite data; unpack_sprites depacks it to SPRBASE.",
          "sprpacked:"]
    for r in range(0, len(packed), 16):
        pk.append("        db  " + ",".join(str(x) for x in packed[r:r + 16]))
    pk.append("")
    open(os.path.join(srcdir, "sprpack.inc"), "w").write("\n".join(pk))

    print("wrote sprites.inc + sprpack.inc: %d sprites, raw %d -> packed %d "
          "(%.0f%%)" % (len(SPRITES), rawlen, len(packed),
                        100 * len(packed) / rawlen))


if __name__ == "__main__":
    main()
