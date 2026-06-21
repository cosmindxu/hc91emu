# ZX-CHESS — Roadmap

A chess engine and game for the I.C.E. Felix HC-91 / ZX Spectrum, written
in Z80 assembly and designed by studying how the strongest open-source
engines work, then re-deriving the same ideas under the constraints of a
3.5 MHz 8-bit CPU with 48 KB of RAM.

The plan is organised as five phases — **Foundation → Stabilization →
Improvement → Optimization → Excellence**. Phases 1–4 are implemented and
tested; Phase 5's core is in place, with the larger "excellence" items
tracked as remaining work. Every claim marked ✅ is exercised by the
headless test harness (`make test`: golden board, engine reply, and a
perft + Zobrist-key self-test) or by a documented manual check.

> **Reference frame.** "What Stockfish/Leela/lichess do" is the north
> star, but almost none of it ports verbatim. A Z80 has no 64-bit
> registers (so no bitboards), no fast multiply, ~3.5 M cycles/second,
> and 48 KB total. Each phase picks the *idea* with the best
> strength-per-byte and strength-per-cycle and builds an 8-bit analogue.

---

## How the open-source engines map onto this one

| Concern | Modern engines | ZX-CHESS |
|---|---|---|
| Board | Bitboards + mailbox | **0x88 mailbox**, page-aligned (`board[sq]` = H:0xE0,L:sq) |
| Move gen | Magic bitboards, staged | Direction-offset rays over 0x88; captures-first in quiescence |
| Search | Iterative-deepening PVS | Negamax **alpha-beta**, **iterative deepening**, **null-move** |
| Ordering | TT move, MVV-LVA, killers, history | **TT move + PV + MVV-LVA + killers** |
| Quiescence | captures/checks | **captures + promotions, stand-pat** |
| Eval | NNUE | **material + tapered PSTs + bishop pair + pawn structure** |
| Hashing | 64-bit Zobrist, huge TT | **16-bit Zobrist (perft-verified) + 8 KB TT** |
| Draws | rep / 50-move / material | **threefold + 50-move + insufficient material** |
| Correctness | perft, fuzzing | **perft to depth 4 + Kiwipete/ep/promotion + key self-test** |

---

## Phase 1 — Foundation ✅ DONE

- **0x88 board & state**: side, castling rights, ep square, halfmove
  clock, cached king squares, full-move number.
- **Move generation**: every piece via offset tables; pawn
  pushes/captures, **double-push, promotion, en passant, castling**.
- **Legality & rules**: `isAttacked` check detection; `genLegal`
  filters via make/test/unmake; **checkmate, stalemate, fifty-move**.
- **Search & eval**: negamax with material + piece-square tables.
- **UI**: 8×8 board of 2×2 cells with 16×16 piece glyphs, cursor input,
  rank/file labels, ROM-font status, selectable depth, board flip.

---

## Phase 2 — Stabilization ✅ DONE

- **Perft** (`T` key / `make test`): start-position depths 1–4
  (20 / 400 / 8902 / 197281) **plus** Kiwipete d3 = 97862 (castling +
  ep), an en-passant position d4 = 43238, and a promotion position
  d3 = 62379 — proving castling, en passant and under/promotion. ✅
  (This is what exposed and fixed missing castling *generation*.)
- **Zobrist key self-test**: on the top plies of every perft position
  the incremental key is checked against a from-scratch recompute →
  "zobrist key OK". ✅
- **Threefold repetition** via the position-key history. ✅
- **Insufficient material** draws (KvK / KNvK / KBvK). ✅
- *Remaining:* SAN move log (coordinate readout is shown today), a
  promotion piece chooser (auto-queens for now).

---

## Phase 3 — Improvement ✅ DONE (core)

- **Quiescence search** (captures + promotions, stand-pat). ✅
- **Move ordering**: MVV-LVA + **killer moves** + TT/PV move first. ✅
- **Tapered evaluation**: middlegame/endgame king tables switched by
  game phase (king centralises in the endgame); **bishop-pair bonus**;
  **doubled/isolated pawn** penalties; **king-safety pawn shield**. ✅
- **Difficulty levels** 1–5 (search depth, `1`–`5` keys). ✅
- **Opening book** (compact): instant, sound replies to the common first
  moves. ✅
- *Remaining:* history heuristic, mobility / passed-pawn terms, a deeper
  book, explicit beginner weakening (depth already differentiates).

---

## Phase 4 — Optimization ✅ DONE (core)

- **Iterative deepening** with the previous iteration's best move
  carried forward as a PV hint. ✅
- **Alpha-beta** with overflow-correct signed bounds. ✅
- **Aspiration windows** around the previous score, with full-window
  re-search on a fail-high/low. ✅
- **Null-move pruning** (depth≥3, not in check, phase-guarded). ✅
- **Reverse-futility pruning** at shallow depth. ✅
- **Transposition table**: 8 KB / 1024 buckets keyed by the 16-bit
  Zobrist hash; depth-bounded exact/lower/upper cutoffs and a stored
  best move fed (per-ply) to the move ordering. ✅
- *Remaining:* late-move reductions, incremental (make/unmake)
  evaluation, and using the 128K models' extra banks for a larger TT.

---

## Phase 5 — Excellence — core implemented

- **Two-player** (human vs human) mode (`V`). ✅
- **Take-back / undo** (`Z`): a 48-ply stack restores board, side,
  castling/ep, Zobrist key and repetition history exactly. ✅
- **Analysis readout**: level, and after each engine move its move in
  coordinate notation with the evaluation in centipawns. ✅
- **Endgame**: the tapered king table already drives the king to the
  centre to help conversion; insufficient-material draws are detected. ✅
- *Remaining (tracked):* dedicated KQK / KRK / KPK mating logic, a FEN
  set-up screen and game save/load to tape, chess clocks, opening-name
  display, a captured-material tray, AY move sounds, and a serial
  **UCI bridge** so the engine can be driven by external GUIs.

---

### Why this order

Each phase is gated on the previous being correct and shippable: you
cannot trust evaluation tuning (Ph3) without a perft-proven generator
(Ph2), and pruning + a transposition table (Ph4) are only safe once the
search is well-ordered and quiescent (Ph3). The transposition table in
particular was built only after the Zobrist key was proven byte-for-byte
against a from-scratch recompute across the entire perft tree — the same
risk-first sequencing the emulator itself used.
