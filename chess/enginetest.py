#!/usr/bin/env python3
"""enginetest.py — behavioural tests for the ZX-CHESS search and evaluation.

WHY THIS EXISTS
---------------
The original smoke test played 1.e2-e4 and grepped the emulator's text screen
for the string "Your move".  That check has no discriminating power at all:

  * it never looks at the move the engine played, only at a status message;
  * the reply to 1.e4 comes straight out of the opening book, so `negamax`,
    `eval` and the transposition table are never entered.

Measured: an engine whose `eval` returns a constant 0, and an engine whose
search result is discarded in favour of the first legal move, both still
reply "e7-e5" from the book and both still print "Your move".

The tests here fix that by driving the engine into positions that are NOT in
the opening book and asserting on what it actually plays.

HOW A POSITION IS LOADED
------------------------
No engine-side hook is needed.  ZX-CHESS already has a tape save/load feature
(`G` saves, `L` loads) whose payload is a flat 71-byte record:

    0..63   board a1..h8, rank-major, 0=empty 1..6=white P N B R Q K, +8=black
    64      side to move (0 = white, 8 = black)
    65      castling rights (bit0 WK, bit1 WQ, bit2 BK, bit3 BQ)
    66      en-passant target square (0x88 form), 0xFF = none
    67      halfmove clock
    68,69   full move number (little endian)
    70      difficulty / aiDepth

So a fixture is just that record wrapped as a standard ZX tape data block and
appended to chess.tap.  The emulator boots the game, `L` loads the block, and
because every fixture has Black to move (while the human side is White) the
main loop hands straight over to `aiMove`.  A .sna snapshot taken afterwards
exposes the whole workspace, so the assertions can read the exact move played,
the search score, and the game state.

Symbol addresses are read from chess.sym (emitted by pasmo alongside the
binary), never hardcoded, so they follow the source automatically.

ORACLE
------
Every expectation below — the legal-move lists, the "unique mate in 1", the
"forced win of a queen", the perft node counts — was derived offline with
python-chess 1.11.2 and frozen here as data.  The suite itself needs only the
standard library.  `movegen_diff.py` runs the live differential comparison
against python-chess when that package is available.
"""

import argparse
import os
import re
import struct
import subprocess
import sys
import tempfile

# --------------------------------------------------------------------------
# piece codes (chess.asm: type in bits 0-2, colour in bit 3)
PIECE = {'P': 1, 'N': 2, 'B': 3, 'R': 4, 'Q': 5, 'K': 6}
MATE = 29000          # chess.asm: MATE equ 29000


def sq_name(x88):
    """0x88 square -> 'e4'."""
    return "abcdefgh"[x88 & 7] + str((x88 >> 4) + 1)


def uci(frm, to):
    return sq_name(frm) + sq_name(to)


# --------------------------------------------------------------------------
class Harness:
    """Boots ZX-CHESS under the emulator and reads back its workspace."""

    def __init__(self, emu, rom, tap, sym, tmp):
        self.emu, self.rom, self.tap, self.tmp = emu, rom, tap, tmp
        self.sym = {}
        with open(sym) as f:
            for line in f:
                m = re.match(r"(\w+)\s+EQU\s+([0-9A-Fa-f]+)H\s*$", line.strip())
                if m:
                    self.sym[m.group(1)] = int(m.group(2), 16)
        for need in ("board", "lastFrom", "lastTo", "lastScore", "haveLast",
                     "gameState", "sideToMove", "openingNamePtr", "moveLog",
                     "moveLogN", "aiDepth", "blackDepth"):
            if need not in self.sym:
                raise SystemExit("enginetest: symbol %r missing from %s"
                                 % (need, sym))

    # -- fixture construction ---------------------------------------------
    def fen_block(self, fen, depth=2):
        """FEN -> the 71-byte save record wrapped as a ZX tape data block."""
        parts = fen.split()
        b = bytearray(71)
        rank, file = 7, 0
        for ch in parts[0]:
            if ch == '/':
                rank -= 1
                file = 0
            elif ch.isdigit():
                file += int(ch)
            else:
                b[rank * 8 + file] = PIECE[ch.upper()] + (0 if ch.isupper() else 8)
                file += 1
        b[64] = 0 if parts[1] == 'w' else 8
        rights = parts[2] if len(parts) > 2 else '-'
        b[65] = sum(bit for c, bit in (('K', 1), ('Q', 2), ('k', 4), ('q', 8))
                    if c in rights)
        ep = parts[3] if len(parts) > 3 else '-'
        b[66] = (int(ep[1]) - 1) * 16 + (ord(ep[0]) - ord('a')) if ep != '-' else 0xFF
        b[67] = 0
        b[68], b[69] = 1, 0
        b[70] = depth
        body = bytes([0xFF]) + bytes(b)          # 0xFF = ZX data-block flag
        parity = 0
        for x in body:
            parity ^= x
        body += bytes([parity])
        return struct.pack("<H", len(body)) + body

    # -- running -----------------------------------------------------------
    def run(self, tag, keys, frames, fen=None, depth=2, text=False):
        tape = self.tap
        if fen is not None:
            tape = os.path.join(self.tmp, "fx_%s.tap" % tag)
            with open(tape, "wb") as f:
                f.write(open(self.tap, "rb").read())
                f.write(self.fen_block(fen, depth))
        sna = os.path.join(self.tmp, "fx_%s.sna" % tag)
        cmd = [self.emu, "--machine", "48k", "--rom", self.rom, tape,
               "--autoload", "--turbo"]
        for at, k in keys:
            cmd += ["--keys", "%d:%s" % (at, k)]
        cmd += ["--frames", str(frames), "--save-sna", sna]
        if text:
            cmd.append("--text")
        p = subprocess.run(cmd, capture_output=True, text=True)
        if not os.path.exists(sna):
            raise SystemExit("enginetest: emulator produced no snapshot for %r\n%s"
                             % (tag, p.stderr[-2000:]))
        return State(self, open(sna, "rb").read()[27:]), p.stdout


class State:
    """Read-only view of the 48K RAM image left behind by a run."""

    def __init__(self, h, ram):
        self.h, self.ram = h, ram

    def peek(self, addr):
        return self.ram[addr - 0x4000]

    def word(self, addr):
        return self.peek(addr) | (self.peek(addr + 1) << 8)

    def sword(self, addr):
        v = self.word(addr)
        return v - 65536 if v >= 32768 else v

    def v(self, name):
        return self.peek(self.h.sym[name])

    def square(self, x88):
        return self.peek(self.h.sym['board'] + x88)

    @property
    def moved(self):
        return self.v('haveLast') == 1

    @property
    def move(self):
        return uci(self.v('lastFrom'), self.v('lastTo'))

    @property
    def score(self):
        return self.sword(self.h.sym['lastScore'])

    @property
    def from_book(self):
        return self.word(self.h.sym['openingNamePtr']) != 0

    @property
    def movelog(self):
        n = self.v('moveLogN')
        base = self.h.sym['moveLog']
        return [uci(self.peek(base + 2 * i), self.peek(base + 2 * i + 1))
                for i in range(n)]


# --------------------------------------------------------------------------
# Fixtures.  Every FEN, legal-move list and tactical claim below was verified
# offline with python-chess 1.11.2 (see movegen_diff.py for the live check).
#
# `frames` is the total frame budget.  The boot + tape-load prologue takes
# 1300 frames; measured completion for these positions is 1400-2100 frames,
# so 3600 leaves the search roughly 3x the frames it actually needs.  A budget
# that is too small fails loudly (haveLast stays 0) rather than passing vaguely.

FRAMES = 3600
LOAD_KEYS = [(1300, "L")]

MATE1_FEN = "r5k1/8/8/8/8/8/5PPP/7K b - - 0 1"
MATE1_LEGAL = ("a8a1 a8a2 a8a3 a8a4 a8a5 a8a6 a8a7 a8b8 a8c8 a8d8 a8e8 a8f8 "
               "g8f7 g8f8 g8g7 g8h7 g8h8").split()

FORK_FEN = "6k1/5p1p/8/4n3/6p1/8/3Q1P1P/6K1 b - - 0 1"
FORK_LEGAL = ("e5c4 e5c6 e5d3 e5d7 e5f3 e5g6 f7f5 f7f6 g4g3 g8f8 g8g7 g8h8 "
              "h7h5 h7h6").split()

POISON_FEN = "6k1/5ppp/8/8/8/q7/1P3PPP/1R4K1 b - - 0 1"
POISON_LEGAL = ("a3a1 a3a2 a3a4 a3a5 a3a6 a3a7 a3a8 a3b2 a3b3 a3b4 a3c3 a3c5 "
                "a3d3 a3d6 a3e3 a3e7 a3f3 a3f8 a3g3 a3h3 f7f5 f7f6 g7g5 g7g6 "
                "g8f8 g8h8 h7h5 h7h6").split()

UPROOK_FEN = "r5k1/ppp2ppp/8/8/8/7P/PPP2PP1/6K1 b - - 0 1"
UPROOK_LEGAL = ("a7a5 a7a6 a8b8 a8c8 a8d8 a8e8 a8f8 b7b5 b7b6 c7c5 c7c6 f7f5 "
                "f7f6 g7g5 g7g6 g8f8 g8h8 h7h5 h7h6").split()

DOWNROOK_FEN = "6k1/ppp2pp1/7p/8/8/8/PPP2PPP/R5K1 b - - 0 1"
DOWNROOK_LEGAL = ("a7a5 a7a6 b7b5 b7b6 c7c5 c7c6 f7f5 f7f6 g7g5 g7g6 g8f8 "
                  "g8h7 g8h8 h6h5").split()

RUY_FEN = "r1bq1rk1/1pppbppp/p1n2n2/4p3/B3P3/5N2/PPPP1PPP/RNBQ1RK1 b - - 0 1"
RUY_LEGAL = ("a6a5 a8a7 a8b8 b7b5 b7b6 c6a5 c6a7 c6b4 c6b8 c6d4 d7d5 d7d6 "
             "d8e8 e7a3 e7b4 e7c5 e7d6 f6d5 f6e4 f6e8 f6g4 f6h5 f8e8 g7g5 "
             "g7g6 g8h8 h7h5 h7h6").split()

# Black's 20 legal replies to 1.e4.
AFTER_E4_LEGAL = ("a7a5 a7a6 b7b5 b7b6 b8a6 b8c6 c7c5 c7c6 d7d5 d7d6 e7e5 "
                  "e7e6 f7f5 f7f6 g7g5 g7g6 g8f6 g8h6 h7h5 h7h6").split()

# perft node counts.  These are fixed mathematical facts about chess, not
# properties of this program; re-derived with python-chess rather than taken
# from the engine's own perftExpected table (which the engine would otherwise
# be grading its own homework against).
PERFT_EXPECT = [
    (r"perft 1\s+(\d+)", 20, "startpos perft(1)"),
    (r"perft 2\s+(\d+)", 400, "startpos perft(2)"),
    (r"perft 3\s+(\d+)", 8902, "startpos perft(3)"),
    (r"perft 4\s+(\d+)", 197281, "startpos perft(4)"),
    (r"kiwipete d3\s+(\d+)", 97862, "kiwipete perft(3)"),
    (r"enpassant d4\s+(\d+)", 43238, "position-3 perft(4)"),
    (r"promotion d3\s+(\d+)", 62379, "position-5 perft(3)"),
]


# --------------------------------------------------------------------------
class Report:
    def __init__(self):
        self.fails = []
        self.passes = 0

    def ok(self, name, detail):
        print("chess: %s OK - %s" % (name, detail))
        self.passes += 1

    def fail(self, name, detail):
        print("chess: FAIL [%s] - %s" % (name, detail))
        self.fails.append(name)

    def check(self, name, cond, good, bad):
        (self.ok if cond else self.fail)(name, good if cond else bad)
        return cond


def engine_moved(rep, name, st):
    """Shared preconditions: the engine really moved, and it moved legally."""
    if not st.moved:
        rep.fail(name, "engine never moved within the frame budget "
                       "(haveLast=0) - raise `frames` for this fixture")
        return False
    if st.v('sideToMove') != 0:
        rep.fail(name, "side to move did not return to White (%d) - the run "
                       "was cut off mid-move" % st.v('sideToMove'))
        return False
    return True


def legal(rep, name, st, allowed):
    """The played move must be in the python-chess-derived legal-move list."""
    if st.move not in allowed:
        rep.fail(name, "ILLEGAL MOVE %s - not among the %d legal moves in this "
                       "position (oracle: python-chess)" % (st.move, len(allowed)))
        return False
    return True


def searched(rep, name, st):
    if st.from_book:
        rep.fail(name, "move came from the opening book, so the search was "
                       "never exercised (openingNamePtr != 0)")
        return False
    return True


# --------------------------------------------------------------------------
def t_mate1(h, rep):
    """Unique mate in 1.  Only a8-a1 mates; it is also the *last* of the a8
    rook's moves in generation order, so an engine that returns its first
    legal move plays a8-b8 instead."""
    name = "search/mate-in-1"
    st, _ = h.run("mate1", LOAD_KEYS, FRAMES, fen=MATE1_FEN)
    if not engine_moved(rep, name, st):
        return
    if not legal(rep, name, st, MATE1_LEGAL) or not searched(rep, name, st):
        return
    if st.move != "a8a1":
        rep.fail(name, "played %s; the only mate in this position is a8a1 "
                       "(1 of 17 legal moves)" % st.move)
        return
    if st.v('gameState') != 1:
        rep.fail(name, "played a8a1 but gameState=%d, expected 1 (white mated)"
                 % st.v('gameState'))
        return
    if st.score < MATE - 100:
        rep.fail(name, "played the mate but reported score %d, expected a mate "
                       "score (>= %d)" % (st.score, MATE - 100))
        return
    rep.ok(name, "found the unique mate a8a1, gameState=1, score=%d" % st.score)


def t_fork(h, rep):
    """Forced win of a queen: the engine is a rook down and must find the
    3-ply combination 1...Nf3+ 2.K moves Nxd2.  Requires search AND evaluation:
    with eval() stubbed to 0 the material swing is invisible."""
    name = "search/knight-fork"
    st, _ = h.run("fork", LOAD_KEYS, FRAMES, fen=FORK_FEN)
    if not engine_moved(rep, name, st):
        return
    if not legal(rep, name, st, FORK_LEGAL) or not searched(rep, name, st):
        return
    if st.move != "e5f3":
        rep.fail(name, "played %s; only e5f3 (Nf3+, forking Kg1 and Qd2) wins "
                       "material here - 1 of 14 legal moves" % st.move)
        return
    if st.score < 200:
        rep.fail(name, "played e5f3 but scored it %d; the combination wins a "
                       "queen for a knight, so the score must be clearly "
                       "positive (>= 200) even though the side to move starts "
                       "500 down" % st.score)
        return
    rep.ok(name, "found e5f3 winning the queen, score=%+d (starts 500 behind)"
           % st.score)


def t_poisoned(h, rep):
    """Poisoned pawn: a3b2 is the only capture available and it loses the queen
    to Rxb2.  An engine that cannot evaluate material takes it, because
    MVV-LVA ordering puts the capture first."""
    name = "eval/poisoned-pawn"
    st, _ = h.run("poison", LOAD_KEYS, FRAMES, fen=POISON_FEN)
    if not engine_moved(rep, name, st):
        return
    if not legal(rep, name, st, POISON_LEGAL) or not searched(rep, name, st):
        return
    if st.move == "a3b2":
        rep.fail(name, "grabbed the poisoned pawn a3b2; it is defended by Rb1 "
                       "and drops the queen for a pawn")
        return
    if st.score < 150:
        rep.fail(name, "avoided a3b2 but scored the position %d; Black is a "
                       "queen for rook+pawn up, so the score must be clearly "
                       "positive (>= 150)" % st.score)
        return
    rep.ok(name, "declined the poisoned pawn (played %s), score=%+d"
           % (st.move, st.score))


def t_material(h, rep):
    """Evaluation sanity in two directions: the same quiet structure with the
    extra rook on either side must produce a large score of the correct sign.
    Kills both `eval returns 0` and `eval returns a constant`."""
    name = "eval/material-balance"
    up, _ = h.run("uprook", LOAD_KEYS, FRAMES, fen=UPROOK_FEN)
    dn, _ = h.run("downrook", LOAD_KEYS, FRAMES, fen=DOWNROOK_FEN)
    for st, allowed in ((up, UPROOK_LEGAL), (dn, DOWNROOK_LEGAL)):
        if not engine_moved(rep, name, st):
            return
        if not legal(rep, name, st, allowed) or not searched(rep, name, st):
            return
    if not 250 <= up.score <= 1200:
        rep.fail(name, "a rook up, the engine scores the position %+d; expected "
                       "+250..+1200" % up.score)
        return
    if not -1200 <= dn.score <= -250:
        rep.fail(name, "a rook down, the engine scores the position %+d; "
                       "expected -1200..-250" % dn.score)
        return
    rep.ok(name, "rook up scores %+d, rook down scores %+d"
           % (up.score, dn.score))


def t_outofbook(h, rep):
    """A real opening position (Ruy Lopez, closed) that the book does not
    cover, so the move must come from the search.  Guards the specific hole
    the old 1.e4 check had: it could never leave the book."""
    name = "search/out-of-book"
    st, _ = h.run("ruy", LOAD_KEYS, FRAMES, fen=RUY_FEN)
    if not engine_moved(rep, name, st):
        return
    if not legal(rep, name, st, RUY_LEGAL) or not searched(rep, name, st):
        return
    if st.score == 0:
        rep.fail(name, "search returned an exactly-zero score in a real "
                       "middlegame position - evaluation is not contributing")
        return
    rep.ok(name, "searched (not booked) and played %s, score=%+d"
           % (st.move, st.score))


def t_e4_ui(h, rep):
    """The old smoke test, made to assert something.  This exercises the human
    move UI and the opening book; by construction it cannot test the search,
    so it no longer claims to.  It now checks the board really changed rather
    than grepping for a status string."""
    name = "ui/human-move-and-book"
    keys = [(960, "ENTER"), (985, "Q"), (1010, "Q"), (1035, "ENTER")]
    st, out = h.run("e4", keys, 4000, text=True)
    b = lambda s: st.square(s)
    if b(0x14) != 0 or b(0x34) != 1:
        rep.fail(name, "1.e2-e4 did not take effect on the board "
                       "(e2=%d e4=%d, expected 0 and 1)" % (b(0x14), b(0x34)))
        return
    log = st.movelog
    if len(log) != 2 or log[0] != "e2e4":
        rep.fail(name, "expected a 2-ply move log starting e2e4, got %r" % (log,))
        return
    if not st.moved:
        rep.fail(name, "the engine never replied to 1.e4")
        return
    if st.move != log[1]:
        rep.fail(name, "move log (%s) disagrees with lastFrom/lastTo (%s)"
                 % (log[1], st.move))
        return
    if not legal(rep, name, st, AFTER_E4_LEGAL):
        return
    # a black piece must genuinely have moved: source empty, target black
    src, dst = st.v('lastFrom'), st.v('lastTo')
    if st.square(src) != 0 or not (st.square(dst) & 8):
        rep.fail(name, "reply %s did not move a black piece on the board "
                       "(from-square holds %d, to-square holds %d)"
                 % (st.move, st.square(src), st.square(dst)))
        return
    if "Your move" not in out:
        rep.fail(name, "status line never returned to 'Your move'")
        return
    rep.ok(name, "1.e2-e4 applied, engine replied %s from the book, "
                 "status returned to 'Your move'" % st.move)


def t_perft(h, rep, perft_text):
    """Compare the perft counts the engine PRINTS against externally derived
    values.  The in-ROM self-test only compares against its own perftExpected
    table and prints 'PERFT OK', so grepping for that string would still pass
    if both the generator and the table were wrong together."""
    name = "movegen/perft-node-counts"
    try:
        txt = open(perft_text).read()
    except OSError as e:
        rep.fail(name, "cannot read the perft screen dump: %s" % e)
        return
    bad = []
    for pat, want, label in PERFT_EXPECT:
        m = re.search(pat, txt)
        if not m:
            bad.append("%s: no count on screen" % label)
        elif int(m.group(1)) != want:
            bad.append("%s: engine printed %s, correct value is %d"
                       % (label, m.group(1), want))
    if bad:
        rep.fail(name, "; ".join(bad))
        return
    if "PERFT OK" not in txt:
        rep.fail(name, "counts are right but the engine did not report PERFT OK")
        return
    rep.ok(name, "all 7 printed node counts match externally derived values")


TESTS = {
    "mate1": t_mate1,
    "fork": t_fork,
    "poisoned": t_poisoned,
    "material": t_material,
    "outofbook": t_outofbook,
    "e4": t_e4_ui,
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--emu", required=True)
    ap.add_argument("--rom", required=True)
    ap.add_argument("--tap", required=True)
    ap.add_argument("--sym", required=True)
    ap.add_argument("--tmp", default=None)
    ap.add_argument("--perft-text", default=None,
                    help="text dump of the in-ROM perft self-test screen")
    ap.add_argument("--only", default=None,
                    help="comma-separated subset of: " + ",".join(TESTS))
    a = ap.parse_args()

    for path, what in ((a.emu, "emulator"), (a.rom, "ROM"),
                       (a.tap, "chess.tap"), (a.sym, "chess.sym")):
        if not os.path.exists(path):
            raise SystemExit("enginetest: %s not found at %s" % (what, path))

    tmp = a.tmp or tempfile.mkdtemp(prefix="zxchess-")
    os.makedirs(tmp, exist_ok=True)
    h = Harness(a.emu, a.rom, a.tap, a.sym, tmp)
    rep = Report()

    want = a.only.split(",") if a.only else list(TESTS)
    for k in want:
        if k not in TESTS:
            raise SystemExit("enginetest: no such test %r" % k)
        TESTS[k](h, rep)
    if a.perft_text:
        t_perft(h, rep, a.perft_text)

    if rep.fails:
        print("chess: %d/%d engine checks FAILED (%s)"
              % (len(rep.fails), len(rep.fails) + rep.passes, ", ".join(rep.fails)))
        return 1
    print("chess: %d engine behaviour checks passed" % rep.passes)
    return 0


if __name__ == "__main__":
    sys.exit(main())
