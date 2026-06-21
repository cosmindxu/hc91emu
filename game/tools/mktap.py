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


def block_body(flag, data):
    """Return flag + data + XOR checksum (the tape block payload)."""
    body = bytes([flag]) + data
    chk = 0
    for b in body:
        chk ^= b
    return body + bytes([chk])


def tap_block(flag, data):
    body = block_body(flag, data)
    return struct.pack("<H", len(body)) + body


def header_body(ftype, name, length, p1, p2):
    name = name.encode("ascii")[:10].ljust(10, b" ")
    return block_body(0x00, bytes([ftype]) + name +
                      struct.pack("<HHH", length, p1, p2))


def header(ftype, name, length, p1, p2):
    body = header_body(ftype, name, length, p1, p2)
    return struct.pack("<H", len(body)) + body


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


def write_tzx(path, blocks):
    """Standard-speed .tzx: header + one 0x10 block per tape block."""
    out = b"ZXTape!\x1a\x01\x14"          # signature + version 1.20
    for body in blocks:
        out += bytes([0x10])              # standard-speed data block
        out += struct.pack("<H", 1000)    # 1000 ms pause after
        out += struct.pack("<H", len(body))
        out += body
    open(path, "wb").write(out)


def main():
    binf, outf = sys.argv[1], sys.argv[2]
    org = int(sys.argv[3]) if len(sys.argv) > 3 else 32768
    name = sys.argv[4] if len(sys.argv) > 4 else "game"
    scrf = sys.argv[5] if len(sys.argv) > 5 else None
    has_scr = bool(scrf) and os.path.exists(scrf) and os.path.getsize(scrf) == 6912
    code = open(binf, "rb").read()
    prog = basic_loader(org, has_scr)
    blocks = [header_body(0, name, len(prog), 10, len(prog)),
              block_body(0xFF, prog)]
    if has_scr:
        scr = open(scrf, "rb").read()
        blocks.append(header_body(3, name, 6912, 16384, 0x8000))
        blocks.append(block_body(0xFF, scr))
    blocks.append(header_body(3, name, len(code), org, 0x8000))
    blocks.append(block_body(0xFF, code))
    # .tap = each block prefixed with its length
    tap = b"".join(struct.pack("<H", len(b)) + b for b in blocks)
    open(outf, "wb").write(tap)
    if outf.endswith(".tap"):
        write_tzx(outf[:-4] + ".tzx", blocks)
    print(f"wrote {outf}: {len(code)} code bytes, org {org}, screen={has_scr}")


if __name__ == "__main__":
    main()
