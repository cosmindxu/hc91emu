#!/usr/bin/env python3
"""Wrap a raw Z80 binary into a ZX Spectrum .tap with a BASIC autoloader.

Usage: mktap.py code.bin out.tap [ORG] [NAME] [loading.scr]

The loader is:
    CLEAR VAL "ORG-1": [LOAD ""SCREEN$:] LOAD ""CODE: RANDOMIZE USR VAL "ORG"
Using VAL "..." keeps every constant a string, so we never have to embed
the 5-byte floating-point form of a number in the tokenised BASIC.  If a
6912-byte .scr is given it is added as a SCREEN$ block that paints in as
the game loads.
"""
import sys, struct, os


def tap_block(flag, data):
    body = bytes([flag]) + data
    chk = 0
    for b in body:
        chk ^= b
    body += bytes([chk])
    return struct.pack("<H", len(body)) + body


def header(ftype, name, length, p1, p2):
    name = name.encode("ascii")[:10].ljust(10, b" ")
    return tap_block(0x00, bytes([ftype]) + name +
                     struct.pack("<HHH", length, p1, p2))


# BASIC tokens we use
CLEAR, VAL, LOAD, CODE, RANDOMIZE, USR, SCRN = 0xFD, 0xB0, 0xEF, 0xAF, 0xF9, 0xC0, 0xAA


def basic_loader(org, with_screen):
    def s(text):  # VAL "text"
        return bytes([VAL, 0x22]) + text.encode("ascii") + bytes([0x22])
    line = bytes([CLEAR]) + s(str(org - 1)) + b":"
    if with_screen:
        line += bytes([LOAD, 0x22, 0x22, SCRN]) + b":"   # LOAD ""SCREEN$
    line += (bytes([LOAD, 0x22, 0x22, CODE]) + b":" +
             bytes([RANDOMIZE, USR]) + s(str(org)) + bytes([0x0D]))
    return struct.pack(">H", 10) + struct.pack("<H", len(line)) + line


def main():
    binf, outf = sys.argv[1], sys.argv[2]
    org = int(sys.argv[3]) if len(sys.argv) > 3 else 32768
    name = sys.argv[4] if len(sys.argv) > 4 else "game"
    scrf = sys.argv[5] if len(sys.argv) > 5 else None
    has_scr = bool(scrf) and os.path.exists(scrf) and os.path.getsize(scrf) == 6912
    code = open(binf, "rb").read()
    prog = basic_loader(org, has_scr)
    tap = b""
    tap += header(0, name, len(prog), 10, len(prog))   # program, autostart line 10
    tap += tap_block(0xFF, prog)
    if has_scr:
        scr = open(scrf, "rb").read()
        tap += header(3, name, 6912, 16384, 0x8000)     # SCREEN$
        tap += tap_block(0xFF, scr)
    tap += header(3, name, len(code), org, 0x8000)      # code at ORG
    tap += tap_block(0xFF, code)
    open(outf, "wb").write(tap)
    print(f"wrote {outf}: {len(code)} code bytes, org {org}, screen={has_scr}")


if __name__ == "__main__":
    main()
