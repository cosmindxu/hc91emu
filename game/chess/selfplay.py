#!/usr/bin/env python3
"""selfplay.py — drive the ZX-CHESS engine to play a full game against
itself and log every move, reading state straight from .sna snapshots
(no screenshots).

Mechanism: poke humanSide=0xFF so the engine's AI plays *both* sides
(mainLoop dispatches every turn to aiTurn).  A single keyboard kick-off
move (1.e4) bounces the program out of the human-move wait back into
mainLoop, after which it self-plays continuously.  We step one ply at a
time by running the emulator from the previous ply's snapshot until the
ply counter (gameKeyN) increments, then read lastFrom/lastTo + gameState.

The 5:00 clocks would flag-fall in turbo (a Z80 search burns ~1000
emulated frames), so we max both clocks in each snapshot before running.
"""
import os, subprocess, sys

EMU  = "../../build/hc91emu"
ROM  = "../../roms/48.rom"
HDR  = 27                       # 48K .sna header size
def off(a): return HDR + (a - 0x4000)

SIDE, CASTLE, EP, CUR = 0xE080, 0xE081, 0xE082, 0xE086
GSTATE, HUMAN, GKN     = 0xE088, 0xE089, 0xE113
LFROM, LTO             = 0xE122, 0xE123
WCLK, BCLK             = 0xE147, 0xE149
BOARD                  = 0xE000

def load(p):  return bytearray(open(p, "rb").read())
def save(d,p): open(p, "wb").write(d)

def prep(d):
    """humanSide=AI-both, clocks maxed so they never flag during a step."""
    d[off(HUMAN)] = 0xFF
    for a in (WCLK, BCLK):
        d[off(a)] = 0xFF; d[off(a)+1] = 0xFF
    return d

def run(sna_in, frames, sna_out, keys=None):
    cmd = [EMU, "--machine", "48k", "--rom", ROM, sna_in, "--turbo",
           "--frames", str(frames), "--save-sna", sna_out]
    for k in (keys or []):
        cmd += ["--keys", k]
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return load(sna_out)

def gkn(d):    return d[off(GKN)]
def gstate(d): return d[off(GSTATE)]
def board(d):  return d[off(BOARD):off(BOARD)+128]

def sq(s):     return "abcdefgh"[s & 7] + "12345678"[(s >> 4) & 7]
PIECE = " PNBRQK"

def advance(cur_path, target, keys, est):
    """Find a snapshot where gameKeyN == target (one more ply), stepping
    the emulator from cur_path.  Returns the snapshot bytes + the frames
    used (a good estimate for the next ply)."""
    tmp = "/tmp/sp_try.sna"
    F = max(300, est)
    for _ in range(40):
        d = run(cur_path, F, tmp, keys)
        g = gkn(d)
        if g == target:
            return d, F
        if g < target:                       # not far enough
            F += max(400, (target - g) * est)
        else:                                # overshot a ply
            F = max(150, F - 500)
    raise RuntimeError(f"could not land on ply {target} (got {g})")

def main():
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    cap = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    boot = "/tmp/boot.sna"
    if not os.path.exists(boot):
        sys.exit("missing /tmp/boot.sna (boot the game and --save-sna it first)")
    base = prep(load(boot))
    save(base, "/tmp/sp_cur.sna")

    moves   = []                 # (ply, side, san) tuples
    prev_b  = board(base)
    est     = 1200
    KICK    = ["20:ENTER", "45:Q", "70:Q", "95:ENTER"]   # 1.e2-e4
    ply     = 0
    cur     = "/tmp/sp_cur.sna"

    while True:
        ply += 1
        target = gkn(base) + ply              # gameKeyN after this ply
        keys = KICK if ply == 1 else None
        d0 = prep(load(cur)); save(d0, cur)   # re-max clocks before stepping
        d, est = advance(cur, target, keys, est)
        save(d, cur)
        frm, to = d[off(LFROM)], d[off(LTO)]
        nb = board(d)
        mover = "W" if (d[off(SIDE)] == 8) else "B"   # side now to move != mover
        ptype = nb[to] & 7
        cap_move = prev_b[to] != 0 or (ptype == 1 and (frm & 7) != (to & 7))  # incl. ep
        if ptype == 6 and abs((frm & 7) - (to & 7)) == 2:
            san = "O-O" if (to & 7) == 6 else "O-O-O"
        else:
            pl = "" if ptype == 1 else PIECE[ptype]
            san = f"{pl}{sq(frm)}{'x' if cap_move else '-'}{sq(to)}"
        moves.append((ply, mover, san))
        gs = gstate(d)
        print(f"ply {ply:3d} {mover} {san:8s}  gameKeyN={gkn(d)} gameState={gs}", flush=True)
        prev_b = nb
        if gs != 0 or ply >= cap:
            break

    print("RESULT_GSTATE", gstate(load(cur)))
    with open("/tmp/sp_moves.txt", "w") as f:
        for ply, mv, san in moves:
            f.write(f"{ply}\t{mv}\t{san}\n")

if __name__ == "__main__":
    main()
