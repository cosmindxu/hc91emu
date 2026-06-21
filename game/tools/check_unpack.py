#!/usr/bin/env python3
"""Verify the runtime sprite depack is byte-identical to the source art.

Usage: check_unpack.py game.sna   (a 48K .sna saved after boot)

Rebuilds the raw sprite stream the same way mksprites.py does, then compares
it against the depacked block at SPRBASE (0x6300) in the snapshot's RAM. This
is the end-to-end guard that the Z80 LZSS depacker matches the packer.
"""
import sys, os

SPRBASE = 0x6300

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mksprites as M


def raw_bytes():
    for i in range(6):
        b = M.SPRITES["spr_ship%d" % i]
        M.SPRITES["spr_ship%d_up" % i] = M.shear(b, +1)
        M.SPRITES["spr_ship%d_dn" % i] = M.shear(b, -1)
    raw = bytearray()
    for name, art in M.SPRITES.items():
        bb, _ = M.sprite_bytes(name, art)
        raw += bytes(bb)
    return bytes(raw)


def main():
    sna = open(sys.argv[1], "rb").read()
    raw = raw_bytes()
    off = 27 + (SPRBASE - 0x4000)          # .sna = 27-byte header + RAM 0x4000+
    mem = sna[off:off + len(raw)]
    if mem == raw:
        print("depack OK: %d bytes at 0x%04X byte-identical" % (len(raw), SPRBASE))
        return 0
    bad = [k for k in range(len(raw)) if mem[k] != raw[k]]
    print("DEPACK MISMATCH: %d/%d bytes differ, first @0x%04X (mem=%d raw=%d)"
          % (len(bad), len(raw), SPRBASE + bad[0], mem[bad[0]], raw[bad[0]]))
    return 1


if __name__ == "__main__":
    sys.exit(main())
