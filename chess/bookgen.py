#!/usr/bin/env python3
# bookgen.py — generate the position-keyed opening book for ZX-CHESS.
#
# Mirrors the engine's 16-bit Zobrist scheme (chess.asm `rng`/`seedRng`,
# zobrist.inc `zobInit`/`computeKey`) so we can compute, on the host, the
# exact hash key the Z80 maintains for any position.  Each book line is a
# sequence of moves from the start; for every position where it is Black's
# turn we emit (key, replyFrom, replyTo, name) for engine.inc's bookTable.
#
# Run:  python3 bookgen.py        (prints the `defb`/`defw` block)

# --- piece constants (match chess.asm) ---
WP,WN,WB,WR,WQ,WK = 1,2,3,4,5,6
BP,BN,BB,BR,BQ,BK = 9,10,11,12,13,14

# --- PRNG (chess.asm rng/seedRng) ---
def build_zob():
    state = 0xA55A
    table = []
    for _ in range(1586):
        h = (state >> 8) & 0xFF
        l = state & 0xFF
        a = (((h & 1) << 7) | (l >> 1)) & 0xFF   # discarded rra(h) sets CF, then rra(l)
        h2 = a ^ h
        l2 = l ^ h2
        state = ((h2 << 8) | l2) & 0xFFFF
        table.append(l2)                          # rng returns A = l
    return table

ZOB = build_zob()
ZP_OFF      = 0
ZCASTLE_OFF = 1536
ZEP_OFF     = 1536 + 32
ZSIDE_OFF   = 1536 + 32 + 16

def word(off):
    return ZOB[off] | (ZOB[off+1] << 8)

def piece_word(piece, sq88):
    idx = ((piece & 7) - 1) + (6 if (piece & 8) else 0)   # 0..11
    sq64 = ((sq88 & 0x70) >> 1) | (sq88 & 7)
    return word(ZP_OFF + (idx*64 + sq64)*2)

# --- board / key ---
def start_board():
    b = [0]*128
    back = [WR,WN,WB,WQ,WK,WB,WN,WR]
    for f in range(8):
        b[0x00+f] = back[f]
        b[0x10+f] = WP
        b[0x60+f] = BP
        b[0x70+f] = back[f] + 8
    return b

def compute_key(b, side, castling, ep):
    k = 0
    for c in range(0x78):
        if c & 0x88:
            continue
        p = b[c]
        if p:
            k ^= piece_word(p, c)
    if side:                       # black to move
        k ^= word(ZSIDE_OFF)
    k ^= word(ZCASTLE_OFF + (castling & 15)*2)
    if ep != 0xFF:
        k ^= word(ZEP_OFF + (ep & 7)*2)
    return k & 0xFFFF

def sq(file, rank):                # a1 = file0 rank0
    return rank*16 + file

def apply(b, frm, to):
    # opening-book moves only: simple piece relocation, set ep on double push
    p = b[frm]
    b[to] = p
    b[frm] = 0
    ep = 0xFF
    if (p & 7) == WP and abs((to >> 4) - (frm >> 4)) == 2:
        ep = (frm + to) // 2
    return ep

# --- book lines: list of (moves, name).  moves = [(from,to),...] from start.
#     We emit an entry for every Black-to-move position along each line. ---
def m(ff,fr,tf,tr): return (sq(ff,fr), sq(tf,tr))

# coordinates: file a=0..h=7, rank 1=0..8=7
e4=m(4,1,4,3); e5=m(4,6,4,4); d4=m(3,1,3,3); d5=m(3,6,3,4)
Ngf3=m(6,0,5,2); Ngf6=m(6,7,5,5); Nbc6=m(1,7,2,5); Nbc3=m(1,0,2,2)
Bb5=m(5,0,1,4); a6=m(0,6,0,5); Ba4=m(1,4,0,3); Bc4=m(5,0,2,3); Bc5=m(5,7,2,4)
c4=m(2,1,2,3); e6=m(4,6,4,5); f4=m(5,1,5,3); c4w=c4
# extras for the wider book (all castling-free; captures appear only as replies)
exd4=m(4,4,3,3)   # ...exd4
c3w=m(2,1,2,2); d3w=m(3,1,3,2); d6b=m(3,6,3,5)
Bf4w=m(2,0,5,3); e3w=m(4,1,4,2); g3w=m(6,1,6,2); b3w=m(1,1,1,2)

LINES = [
    ([e4],                         "Open game"),
    ([e4,e5,Ngf3],                 "King's Knight"),
    ([e4,e5,Ngf3,Nbc6,Bb5],        "Ruy Lopez"),
    ([e4,e5,Ngf3,Nbc6,Bb5,a6,Ba4], "Ruy Lopez"),
    ([e4,e5,Ngf3,Nbc6,Bc4],        "Italian Game"),
    ([e4,e5,Ngf3,Nbc6,Bc4,Bc5,c3w],          "Italian Game"),  # 4.c3 Nf6
    ([e4,e5,Ngf3,Nbc6,Bc4,Bc5,c3w,Ngf6,d3w], "Italian Game"),  # 5.d3 d6
    ([e4,e5,Ngf3,Nbc6,d4],         "Scotch"),                  # 3.d4 exd4
    ([e4,e5,Ngf3,Nbc6,Nbc3],       "King's Knight"),           # Four Knights
    ([e4,e5,Bc4],                  "Open game"),               # Bishop's Opening
    ([e4,e5,Nbc3],                 "Open game"),               # Vienna
    ([e4,e5,d4],                   "Open game"),               # Center Game
    ([d4],                         "Closed game"),
    ([d4,d5,c4],                   "Queen's Gambit"),
    ([d4,d5,Ngf3],                 "Queen's Pawn"),
    ([d4,d5,c4,e6,Nbc3],           "QGD"),
    ([d4,d5,c4,e6,Ngf3],           "QGD"),                     # 3.Nf3 Nf6
    ([d4,d5,Bf4w],                 "London"),                  # 2.Bf4 Nf6
    ([d4,d5,e3w],                  "Closed game"),             # 2.e3 Nf6
    ([Ngf3],                       "Reti"),                    # 1.Nf3 Nf6
    ([Ngf3,Ngf6,c4w],              "Reti"),                    # 2.c4 e6
    ([Ngf3,Ngf6,g3w],              "Reti"),                    # 2.g3 d5
    ([c4],                         "English"),                 # 1.c4 e5
    ([c4,e5,Nbc3],                 "English"),                 # 2.Nc3 Nf6
    ([f4],                         "Bird"),                    # 1.f4 d5
    ([g3w],                        "Closed game"),             # 1.g3 d5
    ([b3w],                        "Open game"),               # 1.b3 e5
]

# replies for the Black-to-move position reached by each line's moves
REPLY = {
    tuple([e4]): e5,
    tuple([e4,e5,Ngf3]): Nbc6,
    tuple([e4,e5,Ngf3,Nbc6,Bb5]): a6,
    tuple([e4,e5,Ngf3,Nbc6,Bb5,a6,Ba4]): Ngf6,
    tuple([e4,e5,Ngf3,Nbc6,Bc4]): Bc5,
    tuple([e4,e5,Ngf3,Nbc6,Bc4,Bc5,c3w]): Ngf6,
    tuple([e4,e5,Ngf3,Nbc6,Bc4,Bc5,c3w,Ngf6,d3w]): d6b,
    tuple([e4,e5,Ngf3,Nbc6,d4]): exd4,
    tuple([e4,e5,Ngf3,Nbc6,Nbc3]): Ngf6,
    tuple([e4,e5,Bc4]): Ngf6,
    tuple([e4,e5,Nbc3]): Ngf6,
    tuple([e4,e5,d4]): exd4,
    tuple([d4]): d5,
    tuple([d4,d5,c4]): e6,
    tuple([d4,d5,Ngf3]): Ngf6,
    tuple([d4,d5,c4,e6,Nbc3]): Ngf6,
    tuple([d4,d5,c4,e6,Ngf3]): Ngf6,
    tuple([d4,d5,Bf4w]): Ngf6,
    tuple([d4,d5,e3w]): Ngf6,
    tuple([Ngf3]): Ngf6,
    tuple([Ngf3,Ngf6,c4w]): e6,
    tuple([Ngf3,Ngf6,g3w]): d5,
    tuple([c4]): e5,
    tuple([c4,e5,Nbc3]): Ngf6,
    tuple([f4]): d5,
    tuple([g3w]): d5,
    tuple([b3w]): e5,
}

def main():
    seen = {}
    out = []
    for moves, name in LINES:
        b = start_board()
        side = 0
        castling = 0x0F
        ep = 0xFF
        for (frm,to) in moves:
            ep = apply(b, frm, to)
            side ^= 8
        key = compute_key(b, 8 if side else 0, castling, ep)
        reply = REPLY[tuple(moves)]
        if key in seen:
            continue
        seen[key] = name
        out.append((key, reply[0], reply[1], name, " ".join(hex(x) for mv in moves for x in mv)))
    # emit asm
    namemap = {"Open game":"nmOpen","Closed game":"nmClosed","Reti":"nmReti",
               "English":"nmEnglish","Bird":"nmBird","King's Knight":"nmKK",
               "Ruy Lopez":"nmRuy","Italian Game":"nmItalian","Queen's Gambit":"nmQG",
               "Queen's Pawn":"nmQP","QGD":"nmQGD","Scotch":"nmScotch",
               "London":"nmLondon"}
    print("; generated by bookgen.py — position-keyed opening book")
    print("; entry: key16, fromTo (2 bytes), name ptr; terminated by 0xFFFF")
    print("bookTable:")
    for key,frm,to,name,_ in out:
        print("        defw 0x%04X" % key)
        print("        defb 0x%02X,0x%02X" % (frm,to))
        print("        defw %-10s ; %s" % (namemap[name], name))
    print("        defw 0xFFFF")
    print()
    print("; names referenced:", sorted(set(namemap[n[3]] for n in out)))

if __name__ == "__main__":
    main()
