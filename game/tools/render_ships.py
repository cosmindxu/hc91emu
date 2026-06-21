#!/usr/bin/env python3
"""Dev helper: render several candidate ship sprites for comparison.

For each variant it patches spr_ship in mksprites.py, rebuilds the game,
boots it in the emulator, screenshots the (cell-aligned) ship and writes a
zoomed crop to build/zoom_<name>.png.  Restores the chosen variant at the end.
Run from the game/ directory:  python3 tools/render_ships.py
"""
import re, subprocess, os
from PIL import Image

EMU = "../build/hc91emu"
ROM = "../roms/hc91.rom"

VARIANTS = {
# A: current - rounded rocket, three tightly-clustered centre windows
"A_current": [
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
# B: dart interceptor - sharp, single rear cockpit window, no antenna
"B_dart": [
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
# C: heavy cruiser - blocky, big rear fins, two square windows
"C_cruiser": [
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
# D: manta - wide swept-back wings, slim pointed body
"D_manta": [
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
# E: forward-canopy bomber - antennas, one tall cockpit near the nose
"E_canopy": [
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
# F: striped hull - rocket with long horizontal panel lines
"F_striped": [
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
}

CHOSEN = "A_current"   # restore this one at the end


def patch_ship(rows):
    p = "tools/mksprites.py"
    s = open(p).read()
    body = "\n".join('    "%s",' % r for r in rows)
    s = re.sub(r'("spr_ship": \[\n).*?(\n\],)',
               lambda m: m.group(1) + body + m.group(2), s, count=1, flags=re.S)
    open(p, "w").write(s)


def build_and_shot(name):
    subprocess.run(["python3", "tools/mksprites.py"], check=True)
    subprocess.run(["pasmo", "src/game.asm", "build/game.bin"], check=True)
    subprocess.run(["python3", "tools/mktap.py", "build/game.bin",
                    "build/shiptest.tap", "32768", "stellar"], check=True)
    shot = "build/ship_%s.png" % name
    subprocess.run([EMU, "--rom", ROM, "--autoload", "--turbo", "--kempston",
                    "--joy", "1300-1308:F", "--joy", "1330-1334:R",
                    "--frames", "1470", "--screenshot", shot,
                    "build/shiptest.tap"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    im = Image.open(shot).convert("RGB")
    cx, cy = 32 + 32 - 4, 24 + 88 - 4
    im.crop((cx, cy, cx + 34, cy + 24)).resize((34 * 9, 24 * 9),
            Image.NEAREST).save("build/zoom_%s.png" % name)
    print("rendered", name)


def main():
    for name, rows in VARIANTS.items():
        patch_ship(rows)
        build_and_shot(name)
    patch_ship(VARIANTS[CHOSEN])
    subprocess.run(["python3", "tools/mksprites.py"], check=True)
    print("restored", CHOSEN)


if __name__ == "__main__":
    main()
