#!/usr/bin/env python3
"""Wrap a raw Z80 binary into a ZX Spectrum .tap with a BASIC autoloader.

Usage: mktap.py code.bin out.tap [ORG] [NAME]

The loader is:  CLEAR VAL "ORG-1": LOAD ""CODE: RANDOMIZE USR VAL "ORG"
Using VAL "..." keeps every constant a string, so we never have to embed
the 5-byte floating-point form of a number in the tokenised BASIC.
"""
import sys, struct


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
CLEAR, VAL, LOAD, CODE, RANDOMIZE, USR = 0xFD, 0xB0, 0xEF, 0xAF, 0xF9, 0xC0


def basic_loader(org):
    def s(text):  # VAL "text"
        return bytes([VAL, 0x22]) + text.encode("ascii") + bytes([0x22])
    line = (bytes([CLEAR]) + s(str(org - 1)) + b":" +
            bytes([LOAD, 0x22, 0x22, CODE]) + b":" +
            bytes([RANDOMIZE, USR]) + s(str(org)) + bytes([0x0D]))
    # line: number (big-endian), length (LE), tokens
    return struct.pack(">H", 10) + struct.pack("<H", len(line)) + line


def main():
    binf, outf = sys.argv[1], sys.argv[2]
    org = int(sys.argv[3]) if len(sys.argv) > 3 else 32768
    name = sys.argv[4] if len(sys.argv) > 4 else "game"
    code = open(binf, "rb").read()
    prog = basic_loader(org)
    tap = b""
    tap += header(0, name, len(prog), 10, len(prog))   # program, autostart line 10
    tap += tap_block(0xFF, prog)
    tap += header(3, name, len(code), org, 0x8000)      # code at ORG
    tap += tap_block(0xFF, code)
    open(outf, "wb").write(tap)
    print(f"wrote {outf}: {len(code)} code bytes, org {org}")


if __name__ == "__main__":
    main()
