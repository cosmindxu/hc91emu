# HC-91 Emulator

An emulator for the **I.C.E. Felix HC-91**, a Romanian ZX Spectrum-compatible
home computer produced from 1991 in Bucharest
(see [Muzeul de Calculatoare](https://muzeuldecalculatoare.ro/2018/09/23/i-c-e-felix-hc-91/)).

## The machine

| Component | Spec |
|-----------|------|
| CPU       | Z80A (or Romanian MMN80CPU clone) @ 3.5 MHz |
| ROM       | 16 KB — Sinclair-derived BASIC, boot banner "HC - 91  I.C.E. FELIX" |
| RAM       | 48 KB usable (64 KB address space incl. ROM) |
| Video     | 256×192, 15 colours, ULA-compatible (rendered here with border, 320×240) |
| Storage   | Cassette tape (`.tap` images, instant ROM-trap loading) |
| Extras    | CP/M bootstrap hook via port `0x7E` (present in ROM, not emulated) |

The HC-91 ROM differs from the original Sinclair 48K ROM in only 50 bytes
(the copyright banner and a small CP/M boot stub), so the machine is fully
ZX Spectrum 48K software-compatible — which this emulator exploits and which
the test suite demonstrates with period software.

## Building

```sh
make            # builds build/hc91emu (the emulator) and build/zexrun (CPU test harness)
make test       # suite: unit tests (timing/contention) + zexdoc + boot/BASIC/software
make test-full  # same but also runs the (slow) zexall undocumented-flags exerciser
                # RUN_Z80TEST=1 additionally re-runs Rak's z80full+z80ccf taps (~4 min)
```

No external dependencies: plain C99 + libc. Screenshots are written as PNG
by a built-in encoder; screen contents can also be read back as text
(OCR against the ROM font) for scripted testing.

## Usage

```sh
./build/hc91emu [options] [program.tap|.sna|.z80]

  --rom FILE        ROM image (default roms/hc91.rom)
  --frames N        frames to emulate, 50 frames = 1 emulated second (default 300)
  --screenshot F    write a PNG of the final frame
  --text            print the final screen as text (ROM-font OCR)
  --type "S@F"      type string S starting at frame F (BASIC key mapping)
  --keys "F:NAMES"  press raw key chord (e.g. "100:SYM+P") at frame F
  --autoload        types LOAD "" automatically (for .tap files)
  --wav FILE        record beeper audio to a 44.1 kHz mono WAV
```

Examples:

```sh
# Boot to the HC-91 BASIC banner and capture it
./build/hc91emu --frames 200 --screenshot boot.png --text

# Compute something in BASIC: 'p' is the PRINT keyword key
./build/hc91emu --frames 400 --type 'p2+3*4\n@220' --text

# Load and run a game from tape
./build/hc91emu software/manic_miner.tap --autoload --frames 1500 --screenshot game.png

# Resume a snapshot
./build/hc91emu software/game.z80 --frames 500 --screenshot snap.png

# Save state / screen after a run (.sna, .z80 v2, raw .scr)
./build/hc91emu software/manic_miner.tap --autoload --frames 1500 \
    --save-sna mm.sna --save-z80 mm.z80 --save-scr mm.scr
./build/hc91emu mm.scr --frames 10 --screenshot mm.png   # .scr loads too
```

## ROMs

`roms/hc91.rom` is the genuine HC-91 ROM dump (from
[speccy4ever](https://speccy4ever.speccy.org/_IC.htm)); `roms/48.rom` is the
standard Sinclair 48K ROM, also usable with `--rom`.

## Emulation notes / limitations

- Full Z80 core incl. undocumented opcodes/flags, MEMPTR (WZ), the internal
  **Q register** (SCF/CCF X/Y behavior; not latched by `POP AF`/`EX AF,AF'`,
  matching Zilog NMOS) and interrupted-block-instruction flag leakage.
  Validation: **zexdoc and zexall pass (67/67 each)** and Patrik Rak's
  in-machine suites pass — **z80doc, the strict z80full, AND z80ccf all
  report "all tests passed" (160/160)**, with the core correctly
  identifying as a Zilog NMOS part (NEC/ST variant tests skip).
- **Memory & I/O contention**: T-states are charged per M-cycle with the
  ULA's `6,5,4,3,2,1,0,0` stall pattern (T=14335 origin, 224 T lines) on
  0x4000–0x7FFF accesses and on I/O per the documented port-decoding
  table; the INT pulse is the real ~32 T. `tests/ctest.c` locks down the
  canonical contention breakdown (address + T-offset of every bus access)
  for 50 instruction shapes; `tests/ccfscr_golden.png` freezes the
  z80ccfscr screen pattern.
- ULA port `0xFE`: keyboard matrix, border, EAR input follows the speaker
  bit when idle (Issue-3 behavior).
- **Floating bus**: reads of unattached ports return the byte the ULA is
  fetching at that T-state (pixel/attr during the display, `0xFF` in
  border/idle slots) — verified with Arkanoid's beam-sync routine, which
  freezes without it (`--no-floating-bus` to disable).
- **Beam-accurate video**: the visible frame is painted incrementally at
  the beam position (paper sampled at the ULA fetch slots, border at
  2 px/T), so mid-frame border effects (raster bars, rainbow stripes) and
  per-scanline multicolour (Nirvana-style attr racing) render correctly.
  Verified by generated test taps in the suite (`tests/bordertap.c`,
  `tests/multitap.c` + `tests/fbcheck.c` on `--fb-dump` output).
- **Joysticks**: Kempston on port 0x1F (`--kempston`; left detached by
  default so the port floats authentically), plus Sinclair Interface 2
  and cursor-key joysticks as matrix aliases; drive any of them in
  scripts with `--joy "F-G:U+D+L+R+F"` and `--joy-type`.
- **Beeper audio**: ULA speaker-bit transitions are box-filtered into
  44.1 kHz mono WAV (`--wav out.wav`); verified to tuning accuracy
  (BASIC `BEEP 1,0` renders 261.1 Hz vs the ideal 261.63 Hz middle C).
  No live audio yet (that arrives with the SDL frontend).
- **Tape**: `.tap`/`.tzx` load instantly via the LD-BYTES ROM trap by
  default, or at pulse level with `--real-tape` (the tape becomes an
  EAR edge stream with exact T-state timing; TZX turbo/tone/pulse/loop
  blocks supported). `SAVE` output is captured to `.tap` via the
  SA-BYTES trap (`--save-tape FILE`).
- 50 Hz frame interrupt (drift-free 69888 T frames).
- **HC-91 CP/M mode**: port `0x7E` bit 0 pages a separate low 16K RAM
  bank over the ROM (the machine has 64K RAM). The genuine ROM's
  bootstrap (`RANDOMIZE USR 14446`) runs and lands at PC=0 in paged RAM.
  No disk interface yet, so a full CP/M boot is not possible — the bank
  is pre-filled with HALT so the bare bootstrap parks cleanly.

## Test results (2026-06-11)

| Test | Result |
|------|--------|
| zexdoc (documented flags) | 67/67 OK |
| zexall (undocumented X/Y flags) | 67/67 OK |
| z80doc (Rak, in-machine, tape-loaded) | all 160 passed |
| z80full (Rak, strict: Q reg, MEMPTR, block flags) | all 160 passed |
| z80ccf (Rak, CCF after every instruction — Q + flags) | all 160 passed |
| z80ccfscr (visual CCF pattern) | matches frozen golden (`make test`) |
| ttest (45 instruction T-state totals) | all OK |
| ctest (50 per-M-cycle contention breakdowns) | all OK |
| Beam video: border rainbow tap | 8 colors, 228 transitions per border column |
| Beam video: multicolour attr racing | 5 paper colors inside one 8×8 cell |
| HC-91 ROM boot | "HC - 91 … I.C.E. FELIX" banner |
| Sinclair 48K ROM boot | © 1982 banner |
| BASIC keyboard test | `PRINT 2+3*4` → `14`, report `0 OK` |
| Manic Miner (.tap, LOAD "") | loads, attract mode runs |
| Jet Set Willy (.tap, LOAD "") | loads, playable in-game |
| Jet Set Basic (.tap) | loads and runs |
| Manic Miner (.z80 v1 snapshot) | resumes, demo runs |
| Jetpac (.z80 v1 snapshot) | resumes and runs |
| Arkanoid (.tap + .z80) | loads; round 1 playable — floating-bus beam-sync works (game freezes with `--no-floating-bus`) |
| Beeper: BASIC `BEEP 1,0` | 1.0 s tone at 261.1 Hz (middle C, 0.2% off ideal — ROM quantization) |
| Beeper: Manic Miner title music | Blue Danube renders as WAV, full melody |
