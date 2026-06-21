#!/usr/bin/env python3
# tt_check.py — verify the 128K banked transposition table.
#
# Given a 48K snapshot (.sna) and a 128K snapshot (.szx), both captured
# after the engine searched the same forced KRK position, assert:
#   * the 48K build did not detect paging (is128 == 0);
#   * the 128K build did     detect paging (is128 == 1);
#   * the engine's move is identical on both (banking must not corrupt it);
#   * the 128K spare RAM banks (1,3,4,6) hold well-formed TT entries
#     (a non-empty slot with a real move), proving the banked store/probe
#     round-trips rather than silently no-opping.
import sys, struct, zlib

LASTFROM, LASTTO, HAVELAST, IS128 = 0xE122, 0xE123, 0xE124, 0xE158
TT_BANKS = (1, 3, 4, 6)


def fail(msg):
    print("chess: FAIL - " + msg)
    sys.exit(1)


def read_sna(path):
    r = open(path, "rb").read()[27:]            # 48K .sna: header then RAM @ 0x4000
    return lambda a: r[a - 0x4000]


def read_szx_pages(path):
    d = open(path, "rb").read()
    if d[:4] != b"ZXST":
        fail("not an SZX snapshot")
    pages, i = {}, 8
    while i + 8 <= len(d):
        cid = d[i:i + 4]
        sz = struct.unpack("<I", d[i + 4:i + 8])[0]
        body = d[i + 8:i + 8 + sz]
        i += 8 + sz
        if cid == b"RAMP":
            flags = struct.unpack("<H", body[:2])[0]
            pno = body[2]
            raw = body[3:]
            if flags & 1:
                raw = zlib.decompress(raw)
            pages[pno] = raw
    return pages


def bank_has_entry(page):
    for off in range(0, len(page) - 7, 8):
        e = page[off:off + 8]
        # entry: key(2) score(2) depth(1) flag(1) from(1) to(1)
        if e[4] != 0 and e[6] != 0xFF:          # non-empty slot with a stored move
            return True
    return False


def main():
    g48 = read_sna(sys.argv[1])
    pages = read_szx_pages(sys.argv[2])

    if g48(IS128) != 0:
        fail("48K build wrongly detected paging")
    # the 128K snapshot keeps is128 in page 0 (mapped at 0xC000 region only when
    # banked); read it from the live workspace via page 0.
    if pages.get(0) is None:
        fail("128K snapshot missing RAM page 0")
    if pages[0][IS128 - 0xC000] != 1:
        fail("128K build did not detect paging (is128 != 1)")

    if g48(HAVELAST) != 1:
        fail("48K engine did not move")
    mv48 = (g48(LASTFROM), g48(LASTTO))
    p0 = pages[0]
    g128 = lambda a: p0[a - 0xC000]
    if g128(HAVELAST) != 1:
        fail("128K engine did not move")
    mv128 = (g128(LASTFROM), g128(LASTTO))
    if mv48 != mv128:
        fail("engine move differs: 48K %02X%02X vs 128K %02X%02X"
             % (mv48[0], mv48[1], mv128[0], mv128[1]))

    banks_used = [b for b in TT_BANKS if b in pages and bank_has_entry(pages[b])]
    if len(banks_used) < len(TT_BANKS):
        fail("128K spare banks missing TT entries: only %s populated" % banks_used)

    print("chess: 128K banked TT OK (is128 split; move %02X%02X; banks %s populated)"
          % (mv48[0], mv48[1], banks_used))


if __name__ == "__main__":
    main()
