#!/usr/bin/env python3
"""movegen_diff.py — differential test of ZX-CHESS move generation against
python-chess, used as an external oracle.

Unlike perft, which only compares node *totals*, this compares the actual set
of legal moves square by square, so a generator that invents one move and
misses another (a compensating pair perft cannot see) still fails here.

Technique
---------
The position is loaded through the ordinary tape-load path (see enginetest.py
for the record layout).  Two-player mode is switched on first with `V` so the
engine never moves and the position survives.  The cursor is then driven to
make one deliberately *illegal* king move: `validateHumanMove` generates the
full ply-0 legal-move list to check it, rejects the move, and leaves the list
intact in the ply-0 move buffer, where the snapshot can read it.  Nothing is
played, so the board is still the fixture position.

The move-buffer layout is 4 bytes per move (from, to, flag, score) at
moveBufBase + ply*512; the count is in genCount.  Both addresses come from
chess.sym.

Provenance: the load-position-and-diff-against-python-chess technique, and the
16 regression positions below, come from the movegen defect investigation run
alongside this work; they are kept here so those defects stay fixed.

Requires: python-chess (pip install chess).  `make test` runs this when the
package is importable and prints an explicit SKIP line when it is not;
`make test-oracle` requires it.
"""

import argparse
import os
import sys

try:
    import chess
except ImportError:
    sys.exit("movegen_diff: python-chess is not installed (pip install chess)")

import enginetest as ET


def to88(s):
    return (s // 8) * 16 + (s % 8)


def ref_moves(board):
    return {(to88(m.from_square), to88(m.to_square), m.promotion or 0)
            for m in board.legal_moves}


def keypath(cur, dst):
    """Cursor keystrokes from 0x88 square `cur` to `dst`: files, then ranks."""
    ks = []
    while (cur & 7) != (dst & 7):
        ks.append("P" if (cur & 7) < (dst & 7) else "O")
        cur += 1 if (cur & 7) < (dst & 7) else -1
    while (cur >> 4) != (dst >> 4):
        ks.append("Q" if (cur >> 4) < (dst >> 4) else "A")
        cur += 16 if (cur >> 4) < (dst >> 4) else -16
    return ks, cur


def illegal_probe(board):
    """A (from,to) pair that is certainly illegal: the king, and an empty
    square at least 3 files/ranks away so it can never be a real king move."""
    k = board.king(board.turn)
    kf, kr = k % 8, k // 8
    for s in range(64):
        if board.piece_at(s) is None and \
           max(abs(s % 8 - kf), abs(s // 8 - kr)) >= 3:
            return to88(k), to88(s)
    return None


def board_to_fen_fields(board):
    """python-chess board -> a FEN for enginetest.Harness.fen_block.

    The castling field is rebuilt from board.castling_rights rather than taken
    from board.fen(), so the deliberately inconsistent rights in the
    stale-castling cases below survive into the fixture.
    """
    cast = "".join(c for c, bb in (("K", chess.BB_H1), ("Q", chess.BB_A1),
                                   ("k", chess.BB_H8), ("q", chess.BB_A8))
                   if board.castling_rights & bb) or "-"
    ep = chess.square_name(board.ep_square) if board.ep_square is not None else "-"
    return "%s %s %s %s 0 1" % (board.board_fen(), "w" if board.turn else "b",
                                cast, ep)


def engine_moves(h, tag, board):
    probe = illegal_probe(board)
    if probe is None:
        return None, "no square available for an unambiguously illegal probe move"
    src, dst = probe
    k1, cur = keypath(0x14, src)          # cursor starts on e2 (0x14)
    k2, _ = keypath(cur, dst)
    keys = k1 + ["ENTER"] + k2 + ["ENTER"]

    sched = [(1000, "V"), (1300, "L")]
    sched += [(1450 + 30 * i, k) for i, k in enumerate(keys)]
    frames = 1450 + 30 * len(keys) + 400

    st, _ = h.run("diff_" + tag, sched, frames, fen=board_to_fen_fields(board))

    # The fixture must really be loaded and untouched, or the diff is meaningless.
    live = {r * 16 + f: st.square(r * 16 + f)
            for r in range(8) for f in range(8) if st.square(r * 16 + f)}
    want = {to88(s): board.piece_at(s).piece_type +
            (0 if board.piece_at(s).color else 8)
            for s in range(64) if board.piece_at(s)}
    if live != want:
        return None, "board differs from the fixture after load"
    if st.v('sideToMove') != (0 if board.turn else 8):
        return None, "side to move changed - the probe move was played"

    n = st.v('genCount')
    base = h.sym['moveBufBase']
    got = set()
    for i in range(n):
        a = base + 4 * i
        got.add((st.peek(a), st.peek(a + 1), (st.peek(a + 2) >> 4) & 0x0F))
    return got, n


CASES = [
    ("startpos", chess.STARTING_FEN, None),
    ("kiwipete-w",
     "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", None),
    ("kiwipete-b",
     "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R b KQkq - 0 1", None),
    ("pos3-ep", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", None),
    ("pos5-promo",
     "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", None),
    ("pos4-w",
     "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", None),
    ("ep-avail",
     "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3", None),
    # castling rights recorded with a missing or wrong corner piece
    ("stale-KQ-norooks", "4k3/8/8/8/8/8/8/4K3 w KQ - 0 1", "KQ"),
    ("stale-K-bishop-h1", "4k3/8/8/8/8/8/8/4K2B w K - 0 1", "K"),
    ("stale-kq-black", "r3k3/8/8/8/8/8/8/4K3 b kq - 0 1", "kq"),
    ("real-KQ-rooks", "4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1", None),
    ("mixed-K-real-Q-stale", "4k3/8/8/8/8/8/8/4K2R w KQ - 0 1", None),
    # pawns stranded on a back rank (cannot move forward, may still capture)
    ("wpawn-a8", "P3k3/8/8/4r3/8/8/8/4K3 w - - 0 1", None),
    ("wpawn-h8-plus", "4k2P/8/8/8/8/8/4P3/4K3 w - - 0 1", None),
    ("bpawn-a1", "4k3/8/8/8/8/8/8/p3K3 b - - 0 1", None),
    ("bpawn-rank1-full", "4k3/8/8/8/8/8/4p3/p2pK2p b - - 0 1", None),
    ("both-backrank", "P3k2P/8/8/8/8/8/8/p2K3p w - - 0 1", None),
]

RIGHTS = {"K": chess.BB_H1, "Q": chess.BB_A1, "k": chess.BB_H8, "q": chess.BB_A8}


def build(fen, force):
    b = chess.Board(fen)
    if force:                       # re-assert rights python-chess normalises away
        b.castling_rights = 0
        for c in force:
            b.castling_rights |= RIGHTS[c]
    return b


def name88(x):
    return "abcdefgh"[x & 7] + str((x >> 4) + 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--emu", required=True)
    ap.add_argument("--rom", required=True)
    ap.add_argument("--tap", required=True)
    ap.add_argument("--sym", required=True)
    ap.add_argument("--tmp", default=None)
    a = ap.parse_args()

    import tempfile
    tmp = a.tmp or tempfile.mkdtemp(prefix="zxchess-diff-")
    os.makedirs(tmp, exist_ok=True)
    h = ET.Harness(a.emu, a.rom, a.tap, a.sym, tmp)
    for need in ("genCount", "moveBufBase"):
        if need not in h.sym:
            sys.exit("movegen_diff: symbol %r missing from %s" % (need, a.sym))

    fails = []
    for tag, fen, force in CASES:
        board = build(fen, force)
        ref = ref_moves(board)
        got, info = engine_moves(h, tag, board)
        if got is None:
            print("chess: FAIL [movegen/%s] - harness: %s" % (tag, info))
            fails.append(tag)
            continue
        if got == ref:
            continue
        fails.append(tag)
        print("chess: FAIL [movegen/%s] - engine generated %d legal moves, "
              "python-chess says %d" % (tag, len(got), len(ref)))
        for m in sorted(got - ref)[:8]:
            print("           invents %s-%s" % (name88(m[0]), name88(m[1])))
        for m in sorted(ref - got)[:8]:
            print("           misses  %s-%s" % (name88(m[0]), name88(m[1])))
    if fails:
        print("chess: %d/%d positions disagree with python-chess"
              % (len(fails), len(CASES)))
        return 1
    print("chess: movegen matches python-chess exactly in all %d positions"
          % len(CASES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
