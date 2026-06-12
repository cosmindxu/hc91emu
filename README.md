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
| Storage   | Cassette tape (`.tap`/`.tzx`, ROM-trap or pulse-level loading) |
| Extras    | CP/M mode: port `0x7E` pages RAM over ROM (emulated; the machine has 64K RAM) |

The HC-91 ROM differs from the original Sinclair 48K ROM in only 50 bytes
(the copyright banner and a small CP/M boot stub), so the machine is fully
ZX Spectrum 48K software-compatible — which this emulator exploits and which
the test suite demonstrates with period software.

Other members of the I.C.E. Felix HC family are emulated too
(`--machine`): the **HC-85** and **HC-90** (banner-variant 48K ROMs),
the **HC-128**, whose genuine ROM turns out to be HC-91-derived with
added support for **128K RAM banking via port `0x7FFD`** and an
**AY-3-8912** at `0xFFFD`/`0xBFFD` — both implemented here (8×16K banks,
shadow screen, ROM select, odd-bank contention, full PSG mixed into the
audio path) — and the **HC-2000** with its floppy-disk interface: an
i8272 FDC, IF1-style shadow ROM + 16K interface RAM, the system
configuration latch, and **CP/M 2.2 booting from disk images to the
A> prompt** (`--machine hc2000 --boot-cpm --disk system.img`, or
authentically from BASIC with `RANDOMIZE USR 14446`). The plain
Sinclair 48K is available as `--machine 48k`.

## Documentation

- **`docs/manual.pdf`** — the full user manual (machines, SDL controls,
  tape/disk handling, CP/M, scripting, the debugger, complete option
  reference, troubleshooting).
- **`man ./docs/hc91emu.1`** — the man page, an exhaustive option and
  command reference with examples.
- `make manual` rebuilds both (needs `pdflatex` and `groff`; the PDF is
  committed so end users need neither).

## Building

```sh
tools/get_roms.sh      # if roms/ is absent: fetch the ROM dumps on demand
                       # (sha256-verified; --verify re-checks offline)
make            # builds build/hc91emu (the emulator) and build/zexrun (CPU test harness)
make test       # suite: unit tests (timing/contention/disasm) + zexdoc + boot/BASIC/software
make test-full  # same but also runs the (slow) zexall undocumented-flags exerciser
                # RUN_Z80TEST=1 additionally re-runs Rak's z80full+z80ccf taps (~4 min)
tests/get_vectors.sh   # one-time: fetch the SingleStepTests/z80 vector subset
                       # (~130 MB); 'make test' then cross-checks all 162,000
```

Releases: `tools/release_deb.sh` builds a Debian/Ubuntu package (binary,
ROMs, man page, manual, desktop launcher + icon + MIME types);
`tools/release_windows.sh` cross-builds a Windows x86-64 zip (needs a
mingw-w64 toolchain).

No external dependencies: plain C99 + libc. Screenshots are written as PNG
by a built-in encoder; screen contents can also be read back as text
(OCR against the ROM font) for scripted testing.

## Usage

```sh
# Play interactively: SDL2 window, 50 Hz, live beeper audio.
# (Shift=CAPS SHIFT, Ctrl=SYMBOL SHIFT, Backspace=DELETE, Esc=BREAK,
#  arrows=cursors, gamepad=Kempston, Tab=turbo, F5=pause, F10=quit)
./build/hc91emu --sdl software/jet_set_willy.tap --autoload

./build/hc91emu [options] [program.tap/.tzx/.sna/.z80/.szx/.scr/.rzx]

  --rom FILE        ROM image (default roms/hc91.rom)
  --frames N        frames to emulate, 50 frames = 1 emulated second (default 300)
  --screenshot F    write a PNG of the final frame
  --text            print the final screen as text (ROM-font OCR)
  --type "S@F"      type string S starting at frame F (BASIC key mapping)
  --keys "F:NAMES"  press raw key chord (e.g. "100:SYM+P") at frame F
  --autoload        types LOAD "" automatically (for .tap files)
  --wav FILE        record beeper audio to a 44.1 kHz mono WAV
  --break ADDR      debugger: stop at PC (also --watch/--rwatch/--pwatch,
                    --monitor, --debug "cmds", --trace FILE; see below)
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

# Debug: break at an address, inspect, single-step (scripted or stdin REPL)
./build/hc91emu --frames 5 --break 11CB \
    --debug "regs;dis pc 8;mem 5C00 32;step 3;regs;cont"
./build/hc91emu game.tap --autoload --watch 5C78 --pwatch FE   # watchpoints
./build/hc91emu --frames 2 --trace boot.txt                    # full trace

# Save state / screen after a run (.sna, .z80 v2, .szx, raw .scr)
./build/hc91emu software/manic_miner.tap --autoload --frames 1500 \
    --save-sna mm.sna --save-z80 mm.z80 --save-szx mm.szx --save-scr mm.scr
./build/hc91emu mm.scr --frames 10 --screenshot mm.png   # .scr loads too

# Record a session as RZX, replay it later (inputs come from the file)
./build/hc91emu --frames 450 --type 'p2+3*4\n@260' --rzx-record calc.rzx
./build/hc91emu calc.rzx --frames 460 --text     # prints 14 again
```

## Game library

`tools/get_library.sh` builds a local library of 36 period classics
under `software/library/`, classified by genre (platform, arcade,
isometric, shooter, puzzle, adventure, sports), from the World of
Spectrum file archive — including 128K AY versions of Cybernoid and
Tetris for the HC-128. `tools/verify_library.sh` smoke-loads every
title (screenshots in `tests/out/library/`); the suite plays one 48K
and one 128K title when the library is present. The files are
copyrighted period software and are never committed; the library
README carries the full index.

## ROMs

`roms/` carries the genuine I.C.E. Felix dumps from
[speccy4ever](https://speccy4ever.speccy.org/_IC.htm) — HC-85, HC-90,
HC-91, HC-128 (plus the HC-2000 ROM set for future disk work) — and the
standard Sinclair 48K ROM. `--machine` picks the right one; `--rom`
overrides it.

## Emulation notes / limitations

- Full Z80 core incl. undocumented opcodes/flags, MEMPTR (WZ — incl. the
  repeat-taken `WZ=PC+1` of INxR/OTxR), the internal **Q register**
  (SCF/CCF X/Y behavior; not latched by `POP AF`/`EX AF,AF'`, matching
  Zilog NMOS) and interrupted-block-instruction flag leakage.
  Validation: **zexdoc and zexall pass (67/67 each)**; Patrik Rak's
  in-machine suites pass — **z80doc, the strict z80full, z80ccf AND
  z80memptr all report "all tests passed" (160/160)**, with the core
  correctly identifying as a Zilog NMOS part; and **all 162,000
  SingleStepTests/z80 vectors** in the suite's 162-file subset match
  (registers, WZ/Q/EI-pending, RAM, T-state totals, I/O transactions) —
  `tests/get_vectors.sh` fetches them, `build/sst` replays them.
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
  44.1 kHz mono samples — recorded to WAV (`--wav out.wav`; verified to
  tuning accuracy: BASIC `BEEP 1,0` renders 261.1 Hz vs the ideal
  261.63 Hz middle C) and played live in the SDL frontend.
- **SDL2 frontend** (`--sdl`): resizable 2× window, audio-clocked 50 Hz
  pacing, live beeper, host keyboard → matrix (incl. composed keys),
  game controller → Kempston, Tab turbo / F5 pause / F10 quit. The
  frontend dlopen()s the SDL2 runtime, so building still needs **no**
  SDL headers or libraries — `--sdl` just needs `libSDL2-2.0.so.0` at
  runtime. Headless remains the default; tests use SDL's dummy drivers.
- **Tape**: `.tap`/`.tzx` load instantly via the LD-BYTES ROM trap by
  default, or at pulse level with `--real-tape` (the tape becomes an
  EAR edge stream with exact T-state timing). The TZX block repertoire
  covers standard/turbo data, tone/pulse/pause/loop/info blocks, **CSW
  recordings (0x18, RLE and zlib Z-RLE)** and **generalized data blocks
  (0x19**, symbol alphabets + PRLE pilot + bit stream, incl. the
  polarity flags**)**. `SAVE` output is captured to `.tap` via the
  SA-BYTES trap (`--save-tape FILE`).
- **Multi-load tape control**: the player pauses at block boundaries
  while no loader is polling the EAR port (reads from the ROM keyboard
  scanner don't count, so "press any key" waits don't keep the tape
  rolling) and resumes when loading restarts; TZX stop-the-tape markers
  pause unconditionally; the tape auto-rewinds at the end while a
  loader is still searching (P-47-style "rewind tape" prompts). Second
  cassette sides attach with `--tape-b` (swap with F8 in SDL or
  `--swap-at N` headless; F6 = manual play/stop, F7 = rewind). A
  generated two-stage tape locks the pause/resume behavior in the
  suite.
- 50 Hz frame interrupt (drift-free 69888 T frames).
- **HC-91 CP/M mode**: port `0x7E` bit 0 pages a separate low 16K RAM
  bank over the ROM (the machine has 64K RAM). The genuine ROM's
  bootstrap (`RANDOMIZE USR 14446`) runs and lands at PC=0 in paged RAM.
  No disk interface yet, so a full CP/M boot is not possible — the bank
  is pre-filled with HALT so the bare bootstrap parks cleanly.
- **Snapshots & recordings**: `.sna`, `.z80` (v1/v2/v3) and `.szx`
  (zx-state) all load and save; `.szx` files with zlib-compressed pages
  load through a built-in DEFLATE inflater (still zero external
  dependencies). `.rzx` input recordings both record (`--rzx-record`)
  and replay (pass the `.rzx` as the input file): frames are
  fetch-counted with port reads fed from the recording, so a captured
  session replays bit-exactly — ideal for whole-run regression tests.
- **Debugger/monitor**: full-coverage disassembler (`dtest` locks 124
  cases), PC breakpoints, memory read/write and I/O port watchpoints,
  single-step, hex dump, registers with frame-relative T-states, and
  per-instruction trace-to-file. Scriptable (`--debug "regs;step 3;cont"`)
  for tests, interactive on a tty; commands: `regs dis mem step cont
  break watch rwatch pwatch trace quit help`.
- **HC-128**: 128K banking (`0x7FFD`: banks 0-7 at `0xC000`, shadow
  screen, ROM select with `--rom1`, lock; odd banks contended) and an
  AY-3-8912 (`0xFFFD`/`0xBFFD`, tone/noise/envelope, measured volume
  curve) mixed sample-accurately with the beeper. 128K `.z80`/`.szx`
  snapshots round-trip; 48K snapshots load into a locked USR0-style
  bank set. ULA timing is kept at the 48K clone values (the HC-128 ROM
  is HC-91-derived; no evidence of 228 T lines).
- **HC-2000 disk + CP/M**: the "IF1" interface = i8272 FDC at
  0x85/0x87 with a control latch at 0x05/0x07 (TC, drive select,
  reset), an 8K shadow ROM (paged at 0x0008/0x1708, out after 0x0700)
  with 16K interface RAM, and raw `.img`/`.dsk` images (640K/720K
  80-track, 320K/360K 40-track). The system latch at 0x7E selects
  BASIC/CP/M ROM, moves the ROM window to 0xE000 with RAM low, locks
  itself, and relocates the video generator to 0xC000; ports 0xC7/0xC5
  toggle the CPM A13 flip-flop (0xE000 RAM appears at 0xC000) — all per
  the genuine ROM disassembly and Alex Badea's FUSE hc2000 semantics.
  IF1 BASIC `CAT 1`, disk-loaded games (Golden Axe to its title) and a
  full CP/M 2.2 cold boot to `A>` with working `DIR` are suite-tested.
- **CI**: GitHub Actions workflow builds, fetches/caches the
  SingleStepTests vectors, runs the suite, and repeats the unit/machine
  tests under AddressSanitizer + UBSan.

## Test results (2026-06-11)

| Test | Result |
|------|--------|
| zexdoc (documented flags) | 67/67 OK |
| zexall (undocumented X/Y flags) | 67/67 OK |
| z80doc (Rak, in-machine, tape-loaded) | all 160 passed |
| z80full (Rak, strict: Q reg, MEMPTR, block flags) | all 160 passed |
| z80ccf (Rak, CCF after every instruction — Q + flags) | all 160 passed |
| z80memptr (Rak, WZ behavior) | all 160 passed |
| SingleStepTests/z80 vectors (162-file subset) | 162,000/162,000 match |
| z80ccfscr (visual CCF pattern) | matches frozen golden (`make test`) |
| ttest (45 instruction T-state totals) | all OK |
| ctest (50 per-M-cycle contention breakdowns) | all OK |
| dtest (124 disassembly cases, all prefixes) | all OK |
| Debugger (breakpoint/step/watch/pwatch/trace) | 5 suite tests pass |
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
| SDL2 frontend (dummy drivers, 250-frame session) | boots to banner, paced, audio queue live |
| SDL2 frontend (real X11) | Jet Set Willy to menu at 50 Hz with sound |
| SZX round-trip (incl. zlib-compressed pages) | multicolour engine resumes from both |
| RZX record → replay | typed `PRINT 2+3*4` session replays to the same screen |
| HC-85 / HC-90 boots | family banners |
| HC-128 banking | distinct banks at 0xC000 via `OUT 32765`, PEEK round-trip |
| HC-128 AY tone (reg writes via OUT) | 1007.5 Hz vs ideal 1007.6 Hz |
| HC-128 .z80/.szx round-trip | screen + bank latch + AY state resume (tone continues) |
| HC-2000 `CAT 1` | HC BASIC disk catalog via the i8272 |
| HC-2000 Golden Axe (640K .img) | multi-loads from disk to the title screen |
| HC-2000 CP/M 2.2 | boots to `A>` (golden screen; both entry paths converge); `DIR` lists the disk |
| ULA snow | I=0x40 corrupts fetches deterministically |
| TZX 0x18/0x19 | CSW (RLE+Z-RLE) and generalized-data re-encodings ROM-load |
