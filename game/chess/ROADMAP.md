# ZX-CHESS — Roadmap

A chess engine and game for the I.C.E. Felix HC-91 / ZX Spectrum, written
in Z80 assembly and designed by studying how the strongest open-source
engines work, then re-deriving the same ideas under the constraints of a
3.5 MHz 8-bit CPU with 48 KB of RAM.

The plan is organised as five phases — **Foundation → Stabilization →
Improvement → Optimization → Excellence** — mirroring the way the
emulator's own roadmap is structured. Phase 1 is shipped and playable;
the rest is sequenced so every phase leaves a working, stronger program.

> **Reference frame.** "What Stockfish/Leela/lichess do" is the north
> star, but almost none of it ports verbatim. A Z80 has no 64-bit
> registers (so no bitboards), no fast multiply, ~3.5 M cycles/second
> (≈10⁵× slower than a modern core), and 48 KB total. The job of each
> phase is to pick the *idea* with the best strength-per-byte and
> strength-per-cycle and implement a faithful 8-bit analogue.

---

## How the open-source engines are built (and what we borrow)

| Concern | Stockfish / modern | lichess (server) | ZX-CHESS (this engine) |
|---|---|---|---|
| Board | Bitboards (64-bit) + mailbox | uses SF/lc0 | **0x88 mailbox** — the canonical 8-bit choice; off-board test is one `AND 0x88` |
| Move gen | Magic bitboards, staged | — | Direction-offset rays over 0x88; staged (captures first) in quiescence |
| Search | Iterative-deepening PVS, aspiration | — | Negamax → α-β → PVS (phased in) |
| Pruning | Null-move, LMR, futility, SEE | — | Null-move + futility + delta (Optimization phase) |
| Ordering | TT move, MVV-LVA, killers, history, counter-moves | — | TT move + MVV-LVA + killers + history |
| Eval | NNUE (small net, incremental) | — | Hand-crafted material + PST → tapered + pawns/king-safety/mobility |
| Tables | Huge TT (GBs), syzygy tablebases | cloud eval, opening DB | Small TT in spare RAM; compact opening book; KQK/KRK/KPK rules |
| Time | Smart time management | — | Per-move node/time budget with iterative deepening |
| Protocol | UCI | UCI + analysis UI | On-screen UI; UCI-style move log; (optional serial UCI bridge) |

The throughline: the **algorithms** are universal (α-β with a good move
order is provably close to optimal node count; PST/tapered eval captures
most of classical evaluation; TT + iterative deepening is the backbone of
every engine). Only the **data structures** must shrink to 8 bits.

---

## Phase 1 — Foundation ✅ DONE

A complete, legal, playable game versus a real (if shallow) search.

- **Board & state.** 0x88 mailbox at a page-aligned address so `board[sq]`
  is `(H=0xE0, L=sq)`; side, castling rights, en-passant square, halfmove
  clock, cached king squares, full-move number.
- **Move generation.** All pseudo-legal moves for every piece via
  direction-offset tables (knight/king hops; bishop/rook/queen rays;
  pawn pushes, double-pushes, captures, **promotions**, **en passant**).
- **Legality & rules.** `isAttacked(square, side)` powers check detection;
  `genLegal` filters pseudo-legal moves through make/test/unmake;
  **castling, en passant and promotion** are fully implemented in
  make/unmake with a per-ply undo stack.
- **Terminal detection.** Checkmate, stalemate, fifty-move draw, "Check!"
  announcements.
- **Search.** Fixed-depth **negamax** with a static leaf evaluation;
  mate scores are ply-aware (prefers faster mates). Per-ply search frame
  is laid out as `searchPly`-indexed arrays so α-β/PVS drop in without a
  rewrite.
- **Evaluation.** Material (P/N/B/R/Q) + **piece-square tables** for all
  six pieces (white-relative, mirrored for black), returned side-to-move
  relative for negamax.
- **UI.** 8×8 board of 2×2 character squares with **16×16 piece glyphs**,
  yellow/red board, cursor + selection highlighting, rank/file labels,
  ROM-font status line, full keyboard control, selectable difficulty
  (depth 1–5), board flip, new game, auto-queen promotion.

**Acceptance (met):** boots from tape on the emulator; the initial board
golden-matches; the engine answers 1.e4 with a legal developing move and
returns control — both checked by `make test`.

---

## Phase 2 — Stabilization

Make it provably correct and hard to crash before making it strong.

- **Perft.** Add a `perft(n)` self-test driven from the keyboard; verify
  the canonical start-position counts (20 / 400 / 8 902 / 197 281 /
  4 865 609) and a handful of tricky positions (Kiwipete, en-passant
  discovered-check, promotion). Perft is the industry-standard proof that
  move-gen + make/unmake are bug-free.
- **Repetition & insufficient material.** Threefold-repetition draw via a
  position-key history; K vs K, KB vs K, KN vs K draws.
- **Robust input.** Promotion piece chooser (Q/R/B/N) instead of
  auto-queen; legal-move dots for the selected piece; reject-illegal
  feedback already present.
- **Move log / notation.** Show SAN (or at least coordinate) move history
  in the side panel; halfmove/full-move counters.
- **Fuzz the parser of any saved game** and guard every array bound
  (search ply cap, move-buffer cap) — matches the emulator project's
  fuzz-hardening ethos.

**Acceptance:** perft matches reference to depth 5; no illegal move can be
made or generated; 10⁶-node self-play soak test never hangs or corrupts.

---

## Phase 3 — Improvement (playing strength & UX)

Raise the playing strength with classical, well-understood techniques and
make it feel like a real chess program.

- **Quiescence search** at the leaves (captures + promotions, optional
  checks) to kill the horizon effect — the single biggest strength jump.
- **Move ordering:** MVV-LVA for captures, **killer moves** and a
  **history heuristic** for quiets. Ordering is what makes α-β actually
  prune.
- **Better evaluation:** tapered eval (separate middlegame/endgame PSTs
  interpolated by material), passed/doubled/isolated pawns, bishop pair,
  rook on open file, basic **king safety** (pawn shield, attacker count),
  mobility.
- **Opening book:** a compact hashed book of mainline openings (a few KB)
  so the first moves are instant and sound — the role lichess fills with
  its cloud opening DB.
- **Levels:** map difficulty to depth **and** to deliberate weakening
  (random eval noise / skipped pruning) for beginners.

**Acceptance:** beats the Phase-1 build > 90% in self-play at equal nodes;
plays a recognisable, principled opening; visible eval bar / hint.

---

## Phase 4 — Optimization (speed = depth = strength)

Every cycle saved buys search depth. This phase is pure 8-bit engineering.

- **Alpha-beta → PVS** with an aspiration window around the previous
  iteration's score, plus **iterative deepening** with a per-move time/node
  budget (replacing fixed depth).
- **Transposition table** in spare RAM: Zobrist-style 16-bit keys hashed
  into a small bucketed table; stores best move + bound + depth. Feeds the
  TT move to ordering and cuts re-search. (128K models — HC-128/HC-2000 —
  get a much larger TT in the second RAM banks.)
- **Pruning:** null-move pruning, futility/reverse-futility, late-move
  reductions, delta pruning in quiescence.
- **Hot-path assembly:** unrolled ray generation, incremental material/PST
  update inside make/unmake (no full re-scan in `eval`), incremental
  Zobrist key, attack-table lookups, `SP`-as-data tricks for fast copies.
- **128K targets:** use `port 0x7FFD` banking for a large TT and an
  AY-3-8912 "move made" sound.

**Acceptance:** ≥ 3–4× nodes/second over Phase 3; reaches 2–3 plies deeper
in the same wall-clock; TT hit-rate and EBF reported in the panel.

---

## Phase 5 — Excellence (the "lichess on a Speccy" finish)

The features that make modern chess *software*, not just an engine.

- **Endgame knowledge:** KQK / KRK / KPK / KBNK mating logic and a small
  set of bitbase-style win/draw rules (the 8-bit analogue of syzygy).
- **Analysis mode:** take-back/redo, set-up position, show principal
  variation and a numeric/graphical evaluation, "blunder" warnings.
- **Game I/O:** save/load games to tape (`.tap`) and snapshots; export the
  move list; load a position from FEN typed in.
- **Two-player & clocks:** human-vs-human, chess clock, increment.
- **Polish:** opening-name display, captured-material tray, animated moves,
  selectable board themes, AY sound, and a **serial UCI bridge** so the
  same engine can be driven by / play in external GUIs and on lichess via
  a bot adapter.
- **Strength target:** a genuinely club-strength opponent on stock
  hardware, and a markedly stronger one on the 128K family — the same
  engine, more RAM and cycles.

**Acceptance:** plays full games start-to-mate with correct endgame
technique; analysis UI usable; runs on every machine the emulator
supports (48K / HC-85 / HC-90 / HC-91 / HC-128 / HC-2000).

---

### Why this order

Each phase is gated on the previous one being *correct and shippable*:
you cannot tune evaluation (Ph3) on top of buggy move generation (proven
in Ph2), and pruning (Ph4) is only safe once ordering and quiescence
(Ph3) make the search well-behaved. It is the same risk-first sequencing
the emulator used to go from "runs a ROM" to "cycle-exact, ten phases
done".
