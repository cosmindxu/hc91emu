# Reproducible experiment — making ZX-CHESS play itself (and how to drive it)

This document records, in enough detail to reproduce, **how the engine was
made to play a complete game against itself** (the game in
`SAMPLE_GAME.md`), and the general technique for **driving the running game
through machine memory instead of screenshots**. The same interface is used
to let Claude play the human side in `CLAUDE_VS_ENGINE.md`.

Everything below is deterministic: the HC-91 emulator is run headless in
turbo mode with a fixed key schedule, so the same inputs always produce the
same game.

## 1. The idea in one paragraph

ZX-CHESS has a normal "human vs computer" loop: every ply the main loop hands
control to the AI **unless** it is the human's turn. The human's colour lives
in a single byte, `humanSide`. If you set that byte to a value that can never
equal the side-to-move (`0xFF`, which is neither `0` = white nor `8` = black),
the loop concludes "it is never the human's turn" and calls the AI for
**both** colours. The engine then plays itself. To watch and steer it you do
**not** read the screen — you read and write the program's memory directly by
treating a saved snapshot as a byte array.

## 2. Snapshot interfacing (no screenshots)

A 48K `.sna` snapshot is a **27-byte register header followed by 48 KB of RAM**
(`0x4000`–`0xFFFF`). The byte stored at Z80 address `A` is at file offset:

```
offset(A) = 27 + (A - 0x4000)
```

So to read or poke any game variable you just index the snapshot file at
`offset(address)`. Run the emulator like this (deterministic, headless):

```
hc91emu game.sna --machine 48k --rom roms/48.rom \
        --turbo --frames N --save-sna out.sna --keys "FRAME:KEYNAME"
```

`--keys "F:NAME"` presses a key at emulated frame `F` (held ~6 frames). The
cursor keys are `Q`=rank+1, `A`=rank-1, `O`=file-1, `P`=file+1, `ENTER`
picks/places; `1`–`5` set difficulty.

### Memory map (addresses used by the experiment)

| Address  | Meaning                                                       |
|----------|---------------------------------------------------------------|
| `0xE000` | `board` — 128 bytes, 0x88 layout (`sq = rank*16 + file`)      |
| `0xE080` | `sideToMove` — 0 = white, 8 = black                           |
| `0xE086` | `cursorSq` — UI cursor square                                 |
| `0xE087` | `selSq` — selected from-square (`0xFF` = none)                |
| `0xE088` | `gameState` — 0 play, 1 white-mated, 2 black-mated, 3 stalemate, 4 draw |
| `0xE089` | `humanSide` — colour the human plays (`0xFF` ⇒ engine plays both) |
| `0xE08A` | `aiDepth` — White's search depth (difficulty 1..5)            |
| `0xE122` / `0xE123` | `lastFrom` / `lastTo` — the last move played       |
| `0xE147` / `0xE149` | `wClock` / `bClock` — time left, in 50 Hz frames   |
| `0xE15D` | `moveLogN` — number of plies recorded in the history          |
| `0xE15E` | `blackDepth` — Black's search depth (odds / handicap play)    |
| `0xE200` | `moveLog` — full move history, 2 bytes/ply (`from`, `to`)     |
| `0xA2E9` | `bookTable` — first opening-book entry (this build)           |

Piece codes: white `P N B R Q K` = 1..6, black = 9..14; empty = 0. Square
`e2` = `0x14`, `e4` = `0x34`, etc. (`name = "abcdefgh"[sq & 7] + str((sq>>4)+1)`).

> The `bookTable` address is the only one that moves if you reassemble the
> program. Find it in `chess.bin` by searching for the first entry's bytes
> `A4 D7 64 44` (key `0xD7A4`, reply `e2`→`e4`); its address is
> `0x8000 + offset_in_bin`.

## 3. Making the engine self-play

1. **Boot once and snapshot.** Load `chess.tap` and save a snapshot at the
   "white to move, waiting for a key" position:
   ```
   hc91emu chess.tap --machine 48k --rom roms/48.rom \
           --autoload --turbo --frames 1200 --save-sna boot.sna
   ```
2. **Poke the self-play switch.** Set `humanSide = 0xFF` (`offset(0xE089)`).
   Now the main loop will call the AI for both colours.
3. **(For a decisive game) set odds and switch off the book.** Two equal
   engines draw, so give White the advantage: `aiDepth = 4` (`0xE08A`),
   `blackDepth = 1` (`0xE15E`), and disable the opening book by writing
   `0xFF 0xFF` over the first key of `bookTable` (`0xA2E9`) so weak Black has
   to find its own opening moves.
4. **Top up the clocks.** A Z80 search burns *thousands* of emulated frames,
   and the per-side clocks count emulated frames, so in turbo they flag-fall
   unless refreshed. Before each chunk of play, write `0xFFFF` to both
   `wClock` (`0xE147`) and `bClock` (`0xE149`).
5. **Kick it off.** At boot the program is parked in the keyboard-wait of the
   *human* move routine. Injecting one human move returns control to the main
   loop, which then re-reads the poked `humanSide = 0xFF` and self-plays from
   there. The opening `1.e4` was supplied as the single non-engine action
   (poke `selSq = e2`, `cursorSq = e4`, press `ENTER`; or steer the cursor
   `e2→e4` with `Q/Q/ENTER … ENTER`).
6. **Run in chunks, refreshing the clocks, until it ends.** Read `gameState`
   (`0xE088`) after each chunk; stop on a real result. Read the whole game
   from `moveLog` (`0xE200`) at the end — no OCR, no screenshots.

### Driver (the script that produced `SAMPLE_GAME.md`)

```python
import subprocess
off = lambda a: 27 + (a - 0x4000)
EMU, ROM = "build/hc91emu", "roms/48.rom"

d = bytearray(open("boot.sna", "rb").read())
d[off(0xE089)] = 0xFF                      # humanSide -> engine plays both
d[off(0xE08A)] = 4                         # White depth 4
d[off(0xE15E)] = 1                         # Black depth 1  (odds)
d[off(0xA2E9)] = 0xFF; d[off(0xA2E9)+1] = 0xFF   # disable the opening book
for a in (0xE147, 0xE149):                 # max both clocks
    d[off(a)] = 0xFF; d[off(a)+1] = 0xFF
open("dc.sna", "wb").write(d)

# kick-off: 1.e4 via the cursor (e2 -> e4), then let it self-play
keys = ["--keys","20:ENTER", "--keys","45:Q", "--keys","70:Q", "--keys","95:ENTER"]
first = True
for _ in range(120):
    cmd = [EMU,"--machine","48k","--rom",ROM,"dc.sna","--turbo",
           "--frames","30000","--save-sna","dc.sna"]
    if first: cmd += keys; first = False
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    d = bytearray(open("dc.sna","rb").read())
    for a in (0xE147, 0xE149):             # re-max the clocks each chunk
        d[off(a)] = 0xFF; d[off(a)+1] = 0xFF
    d[off(0xA2E9)] = 0xFF; d[off(0xA2E9)+1] = 0xFF   # keep the book off
    open("dc.sna","wb").write(d)
    if d[off(0xE088)] not in (0, 5):       # 0 = playing, 5 = flag-fall
        break                              # stop on a real result

n = d[off(0xE15D)]                          # read the whole game from moveLog
log = [(d[off(0xE200+2*i)], d[off(0xE200+2*i+1)]) for i in range(n)]
name = lambda s: "abcdefgh"[s & 7] + str((s >> 4) + 1)
print(" ".join(name(f)+name(t) for f, t in log))
```

## 4. Bugs found while getting a game to finish

Running the engine against itself fuzzed out three latent bugs that the unit
tests (perft, KRK, the book) never hit; all are fixed in the source and git
history:

* an **infinite loop** in the passed-pawn evaluation (a clobbered loop
  counter), which only triggers on a pawn-endgame leaf node;
* search move-buffers that had grown to **overwrite the piece glyphs**,
  corrupting the display after the first searched move;
* a **transposition-table cutoff at the root** that could make the engine
  return a score but no move.

## 5. Result

With White at depth 4, Black at depth 1 and no book, the strong side
converted and **checkmated** the weak side — `1-0`, `19.Qg7#` — detected by
the program's own terminal flag (`gameState` = "black mated"). The full game
and final position are in `SAMPLE_GAME.md`.
