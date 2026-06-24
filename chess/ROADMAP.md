# ZX-CHESS — Roadmap

A chess engine and game for the I.C.E. Felix HC-91 / ZX Spectrum, written
in Z80 assembly and designed by studying how the strongest open-source
engines work, then re-deriving the same ideas under the constraints of a
3.5 MHz 8-bit CPU with 48 KB of RAM.

The plan is organised as five phases — **Foundation → Stabilization →
Improvement → Optimization → Excellence**. Phases 1–4 are complete and
Phase 5 is substantially implemented; the leftover work is then split
into **Phase A** (feasible *and* headlessly verifiable here) and
**Phase B** (valuable but not verifiable in this environment — the UCI
bridge). Every claim marked ✅ is exercised by the headless test harness
(`make test`: golden board, engine reply, and a perft + Zobrist-key
self-test) or by a documented manual check.

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
- **Promotion piece chooser** (Q/R/B/N prompt). ✅
- *Remaining:* SAN move log (a coordinate readout is shown today).

---

## Phase 3 — Improvement ✅ DONE (core)

- **Quiescence search** (captures + promotions, stand-pat). ✅
- **Move ordering**: TT/PV move first, MVV-LVA captures, **killer
  moves**, then a **history heuristic** for the remaining quiet moves. ✅
- **Tapered evaluation**: middlegame/endgame king tables switched by
  game phase (king centralises in the endgame); **bishop-pair bonus**;
  **doubled / isolated / passed pawn** terms; **king-safety pawn
  shield**. ✅
- **Difficulty levels** 1–5 (search depth, `1`–`5` keys). ✅
- **Opening book** (compact): instant, sound replies to the common first
  moves, with the opening named on screen. ✅
- *Remaining:* a mobility term, a deeper book, explicit beginner
  weakening (depth already differentiates).

---

## Phase 4 — Optimization ✅ DONE (core)

- **Iterative deepening** with the previous iteration's best move
  carried forward as a PV hint. ✅
- **Alpha-beta** with overflow-correct signed bounds. ✅
- **Aspiration windows** around the previous score, with full-window
  re-search on a fail-high/low. ✅
- **Null-move pruning** (depth≥3, not in check, phase-guarded). ✅
- **Reverse-futility pruning** at shallow depth. ✅
- **Late move reductions**: late quiet moves searched a ply shallower,
  re-searched at full depth on a fail-high. ✅
- **Transposition table**: 8 KB / 1024 buckets keyed by the 16-bit
  Zobrist hash; depth-bounded exact/lower/upper cutoffs and a stored
  best move fed (per-ply) to the move ordering. ✅
- *Remaining:* incremental (make/unmake) evaluation, and using the 128K
  models' extra banks for a larger TT.

---

## Phase 5 — Excellence — substantially implemented

- **Two-player** (human vs human) mode (`V`). ✅
- **Take-back / undo** (`Z`): a 48-ply stack restores board, side,
  castling/ep, Zobrist key and repetition history exactly. ✅
- **Analysis readout**: level, two-player flag, opening name, the
  engine's last move (coordinate notation), its evaluation, and the
  material balance (a captured-material-at-a-glance line). ✅
- **Endgame**: a **KQK / KRK mating drive** pushes the lone king to a
  corner and the stronger king toward it; the tapered king table
  centralises in the endgame; insufficient-material draws are detected. ✅
- **Move sound**: a beeper click on every move (the 48K equivalent of a
  128K AY blip). ✅
- **Position loader** wired into the game (an `E` key loads a KRK demo) —
  the foundation for a full set-up / FEN screen. ✅
- *Remaining (tracked):* a full FEN/keyboard set-up screen and game
  save/load to tape, chess clocks (needs an interrupt time-base), a
  serial **UCI bridge**, and AY voices / a 128K-banked TT on the 128K
  family.

---

## Remaining work, split by verifiability

The leftover items fall into two groups, decided by one question: *can it
be built with `pasmo` **and proven headlessly** on the emulator?* The
verification surface here is screenshots / OCR text, recorded `--wav`
audio, scripted `--keys` / `--type` input, the `--machine hc128` (AY +
128K banking) and `--save-tape` features, and on-device self-test
screens.

### Phase A — feasible *and* verifiable here

Roughly in value-per-risk order. Each line is *what it is — why it brings
value — how it would be proven.* Items marked ✅ are now implemented and
verified.

**Status: Phase A is complete.** Items 1, 3, 4, 5, 6, 7 and 8 are
implemented and verified (the 48K smoke + perft, the tape save/load
round-trip and the 128K banked-TT check all run from `make test`). Item 2
(mobility) is the one line deliberately left undone — see its rationale
below.

1. **Incremental evaluation.** ✅ *What:* keep the material + piece-square
   score up to date inside make/unmake (add the moved piece on its new
   square, subtract its old one, handle captures/promotions/castling)
   instead of re-scanning all 64 squares at every leaf. *Value:*
   evaluation is the hottest code in the search, so this is potentially a
   several-fold speedup — and on a fixed-time move that speed converts
   directly into extra search depth, i.e. playing strength, for free.
   *Verify:* compare the incremental score against a from-scratch
   recompute at every node of the perft tree — the exact technique that
   already proved the Zobrist key — so a refactor this large stays
   low-risk. *Done:* `pstScore` and `gamePhase` are maintained in
   make/unmake and the `T` self-test now also recomputes both from scratch
   at every node; depth-4 search dropped from ~18000 to ~14000 frames with
   identical play.

2. **Mobility term.** ⏸ *Deliberately deferred.* *What:* a small bonus
   per legal move available to a side. *Value:* mobility is one of the
   cheapest positional signals that tracks who is better; it discourages
   the cramped, passive positions that material + piece-square tables
   can't perceive. *Why deferred:* every other Phase A item was a net win,
   but a mobility term must re-count moves at each leaf — exactly the
   per-node scan that item 1 (incremental evaluation) just removed from the
   hot path. Re-introducing a full move-count at every leaf would give back
   most of that speedup (and on a fixed clock, depth = strength), so the
   trade is poor *as a plain leaf term*. It is worth revisiting only in a
   cheaper form — e.g. folded into the attack tables the move generator
   already builds, or limited to a few piece types — which is really a new
   design rather than this line item. Left unticked on purpose.

3. **Deeper opening book.** ✅ *What:* extend the current one-reply book to
   a handful of principal variations several plies deep, keyed by the
   position's Zobrist hash. *Value:* instant, theory-sound openings save
   search time and dodge early inaccuracies — the role lichess fills with
   its cloud opening database. *Verify:* pure data; play the lines and
   check the booked replies appear. *Done:* the book is now keyed by the
   position's 16-bit Zobrist hash (probed against the live `hashKey`), so a
   single table follows transpositions and fires at any ply instead of only
   Black's first move. `bookgen.py` mirrors the engine's PRNG + key scheme
   on the host to generate the entry keys; the Ruy Lopez / Italian / Queen's
   Gambit / QGD mainlines are booked several plies deep. Verified by playing
   1.e4 (→ ...e5, "Open game") and 1.e4 e5 2.Nf3 (→ ...Nc6, "King's
   Knight") — a deep hit that also confirms the host-computed keys match the
   Z80's incrementally-maintained key bit-for-bit.

4. **AY voices on the 128K family.** ✅ *What:* drive the AY-3-8912 (ports
   `0xFFFD`/`0xBFFD`) for distinct move / capture / check / mate cues and
   simple jingles. *Value:* far richer feedback than the 1-bit beeper,
   using the genuine sound chip of the HC-128 / HC-2000. *Verify:* run
   `--machine hc128` and inspect the recorded `--wav` for the expected
   tones (the emulator mixes the PSG into the audio path). *Done:*
   `moveSound` now plays a tone-A blip on the AY (via an `ayWrite`
   register helper) alongside the beeper click; verified on `--machine
   hc128` by the AY raising the recorded waveform's peak (12000→14432, 189
   samples above +13000 versus none on 48k).

5. **128K-banked transposition table.** ✅ *What:* on 128K machines, page
   the spare 16 KB RAM banks through port `0x7FFD` to host a much larger
   TT than the 8 KB that fits in 48 KB. *Value:* a bigger table means
   more cache hits and fewer re-searched transpositions — measurably
   deeper search on the same clock. *Verify:* run on `--machine hc128`;
   perft / play confirm correctness and the hit-rate confirms the gain.
   *Done:* `detect128` probes `0x7FFD` paging at boot (writing distinct
   markers to two banks and seeing which survives) and sets `is128`. On a
   128K the table grows to **8192 buckets across the four spare banks
   (1,3,4,6)** — 8× the 48K's 1024 — indexed by 13 key bits (top 2 select
   the bank, low 11 the slot). The risk that the workspace and **stack**
   also live in the pageable `0xC000-0xFFFF` window is sidestepped: every
   entry is copied to/from `ttStage` in non-pageable RAM with a
   register-only `LDIR` inside a `DI`/`EI` window — no stack op and no ISR
   runs while a bank is paged, and bank 0 is restored before any `ret`.
   Verified end-to-end: `is128` is 0 on 48K and 1 on hc128; hc128 perft
   passes (banking never corrupts the board/key/phase/pst); a forced KRK
   search yields the *same* move on both machines; and an SZX snapshot
   shows all four spare banks populated with well-formed entries
   (valid key, score, depth and stored move) — proving the banked
   store/probe round-trips rather than silently no-opping. Covered by a new
   `make test` step (`tt_check.py`).

6. **FEN / set-up position screen.** ✅ *What:* enter an arbitrary position,
   either with a cursor-driven board editor or by typing a FEN string, on
   top of the existing `setupBoard` / `loadGamePos`. *Value:* lets you
   analyse real games, compose puzzles, or resume a position — table
   stakes for chess *software* rather than just an engine. *Verify:*
   drive the editor with `--keys`, or type a FEN with `--type`, then
   screenshot the resulting board. *Done:* the `S` key opens a
   cursor-driven board editor (`Q`/`A`/`O`/`P` move, `SPACE` cycles the
   square through empty → white → black pieces, `W` toggles side, `C`
   clears, `ENTER` plays). On `ENTER` it re-runs `finalizePosition` so the
   kings, hash key, phase and evaluation accumulators are rebuilt; verified
   with `--keys` by cycling a pawn to a knight and reading the updated
   material display.

7. **Chess clocks.** ✅ *What:* per-side countdown timers with flag-fall,
   driven by enabling the 50 Hz frame interrupt (the one subsystem
   currently left off under `DI`). *Value:* makes it a real competitive
   game — blitz, rapid, increment. *Verify:* the emulator models
   interrupts, so run a fixed number of frames and OCR the displayed
   time. *Done:* `start` now installs an IM1 handler (`ld iy,0x5C3A / im 1
   / ei`) so the ROM ticks `FRAMES` at 0x5C78; each turn's full elapsed
   time (human thinking *or* AI searching) is charged to the side to move
   when the move completes, the human's clock also ticking live once per
   second while they think. Both sides start at 5:00; a clock reaching zero
   is a flag-fall loss unless the position was already terminal. Verified by
   screenshot: White's clock counts down on move 1, then freezes when Black
   is charged its (instant, booked) reply.

8. **Game save / load to tape.** ✅ *What:* write the game state (board +
   move history) to a tape block via the ROM `SA-BYTES`, and read it back
   with `LD-BYTES`. *Value:* persistence — the authentic Spectrum way to
   keep a game between sessions. *Verify:* capture the save with
   `--save-tape`, then load the resulting `.tap` in a second run. *Done:*
   `G` saves a 71-byte block (the board in `setupBoard` layout plus side,
   castling, en-passant, halfmove, move number and difficulty) through the
   ROM `SA-BYTES`; `L` reads it back with `LD-BYTES`, reuses `setupBoard`
   to rebuild the position and restores the extras. Interrupts are disabled
   around the timing-critical ROM calls and re-enabled after. `make test`
   now plays 1.e4 e5, saves with `--save-tape`, appends that block to the
   boot tape, boots fresh, loads with `L`, and asserts from a snapshot that
   the e4/e5 pawns and the vacated e2/e7 squares came back.

### Phase B — valuable, but not verifiable in this environment

1. **Serial UCI bridge.** *What:* speak the Universal Chess Interface —
   the text protocol every modern engine and GUI uses (`uci`,
   `position`, `go`, `bestmove`, …) — over an RS-232 link such as the
   Interface 1 / HC-2000 serial port. *Value:* this is the big one for
   reach. It would let the *same* 8-bit engine be driven by desktop GUIs
   (Arena, Cute Chess), entered into engine tournaments, or wired to a
   lichess-bot adapter — turning a retro curiosity into a real engine you
   can measure against others. The internal hooks already suit it: search
   is a single `aiMove` entry point over `mvFrom/mvTo/mvFlag`, so a UCI
   front-end is a *parser*, not an engine rewrite. *Why it's Phase B:*
   the emulator models no RS-232/UART, and there is no stdin/stdout
   channel to carry UCI traffic, so the protocol code could be written
   but **could not be driven or tested here**. It belongs on real
   hardware, or on an emulator that exposes a serial pipe.

This split keeps the project honest: Phase A is a backlog that can
actually be finished *and proven*; Phase B records the one genuinely
valuable feature whose verification is blocked by the toolchain, with
enough design notes that it is a small job wherever a serial channel
exists.

---

### Why this order

Each phase is gated on the previous being correct and shippable: you
cannot trust evaluation tuning (Ph3) without a perft-proven generator
(Ph2), and pruning + a transposition table (Ph4) are only safe once the
search is well-ordered and quiescent (Ph3). The transposition table in
particular was built only after the Zobrist key was proven byte-for-byte
against a from-scratch recompute across the entire perft tree — the same
risk-first sequencing the emulator itself used.
