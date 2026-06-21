#!/usr/bin/env python3
"""zxtap — wrap a raw machine-code binary into a bootable ZX Spectrum .tap.

The tape is the authentic two-block layout a SAVE...LINE / SAVE...CODE pair
produces: a short tokenised BASIC autoloader followed by the CODE image.
Typing LOAD "" (which the emulator's --autoload does) loads and auto-runs it.

    zxtap.py CODE.bin OUT.tap [--org 0x8000] [--name CHESS]

The BASIC loader uses the VAL"..." trick so no 5-byte float constants have to
be synthesised by hand:

    1 CLEAR VAL "ORG-1": LOAD ""CODE: RANDOMIZE USR VAL "ORG"
"""
import sys, struct

# Spectrum BASIC tokens used by the loader.
CLEAR, LOAD, CODE, RANDOMIZE, USR, VAL = 0xFD, 0xEF, 0xAF, 0xF9, 0xC0, 0xB0


def tap_block(flag, data):
    body = bytes([flag]) + data
    cs = 0
    for b in body:
        cs ^= b
    body += bytes([cs])
    return struct.pack("<H", len(body)) + body


def header(typ, name, length, p1, p2):
    name = (name.encode()[:10] + b" " * 10)[:10]
    return tap_block(0x00, bytes([typ]) + name + struct.pack("<HHH", length, p1, p2))


def basic_loader(org):
    # 1 CLEAR VAL"org-1": LOAD ""CODE: RANDOMIZE USR VAL"org"
    line = bytes([CLEAR, VAL, ord('"')]) + str(org - 1).encode() + bytes([ord('"'), ord(':')])
    line += bytes([LOAD, ord('"'), ord('"'), CODE, ord(':')])
    line += bytes([RANDOMIZE, USR, VAL, ord('"')]) + str(org).encode() + bytes([ord('"')])
    line += bytes([0x0D])
    prog = struct.pack(">H", 1) + struct.pack("<H", len(line)) + line  # line# big-endian
    return prog


def main():
    args = sys.argv[1:]
    org, name = 0x8000, "CHESS"
    pos = []
    i = 0
    while i < len(args):
        if args[i] == "--org":
            org = int(args[i + 1], 0); i += 2
        elif args[i] == "--name":
            name = args[i + 1]; i += 2
        else:
            pos.append(args[i]); i += 1
    if len(pos) != 2:
        sys.exit("usage: zxtap.py CODE.bin OUT.tap [--org N] [--name S]")
    code = open(pos[0], "rb").read()
    prog = basic_loader(org)
    tap = header(0, name, len(prog), 1, len(prog))      # Program, autostart line 1
    tap += tap_block(0xFF, prog)
    tap += header(3, name, len(code), org, 0x8000)       # Code, load addr = org
    tap += tap_block(0xFF, code)
    open(pos[1], "wb").write(tap)
    print(f"wrote {pos[1]}: {len(code)} code bytes @ {org:#06x}, tap {len(tap)} bytes")


if __name__ == "__main__":
    main()
