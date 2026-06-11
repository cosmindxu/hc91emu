# HC-91 Emulator — Roadmap

Current state (2026-06-11): Z80 core passes zexdoc/zexall, Rak's
z80full/z80ccf/z80memptr (160/160) and 162,000 SingleStepTests vectors;
cycle-exact contention + beam video, beeper WAV, pulse tape + TZX, all
snapshot formats save/load, joysticks, HC-91 CP/M paging, debugger; suite
43/43 green (incl. gated slow suites), CI configured. Remaining phases:
4 (SDL2 frontend), 8b (SZX/RZX), 10 (HC family stretch).

---

## Phase 1 — Cycle accuracy: memory & I/O contention — ✅ DONE (2026-06-11)

**What:** The ULA steals bus cycles from the CPU when it accesses contended
memory (0x4000–0x7FFF) or any I/O port during the display. Emulating this
makes the CPU timeline match real hardware exactly.

- Restructure the core so T-states advance *inside* each memory/IO access
  (callbacks gain a "time" role) instead of per-instruction totals; add the
  per-M-cycle breakdowns (4/3/3 fetch patterns, internal cycles).
- ULA contention pattern `6,5,4,3,2,1,0,0` per 8 T-states starting at
  T=14335, lines 64–255, first 128 T of each line; applies to contended
  RAM and to I/O per the standard port-decoding rules.
- Narrow the INT window from the current generous 700 T to the real ~32 T.
- The floating bus then becomes cycle-exact for free.

**Acceptance:** `software/z80test/z80ccfscr.tap` (already downloaded —
requires contended timing + Q register to pass); FUSE's timing test suite;
Aquaplane / Vectron border effects look stable.

**Effort:** large (touches every instruction path). The single biggest
remaining accuracy item.

**Status: shipped.** The core charges T-states per M-cycle (contention
callbacks fire before every bus access with `tstates` current); machine.c
implements the 6,5,4,3,2,1,0,0 pattern from T=14335 plus the documented
I/O contention table; INT window narrowed to 32 T; frame loop is
drift-free. Validation: `tests/ctest.c` asserts the canonical contention
breakdown (address + T-offset of every bus access) for 50 instruction
patterns; Rak's z80full AND z80ccf both report **all 160 tests passed**
in-emulator (fixing z80ccf required Q to *not* latch on POP AF/EX AF,AF');
z80ccfscr's pattern is frozen as `tests/ccfscr_golden.png` and checked by
the suite. `RUN_Z80TEST=1 make test` re-runs the two slow tap suites.

## Phase 2 — Beam-accurate video rendering — ✅ DONE (2026-06-11)

**What:** Render per scanline (or per T-state) instead of once per frame.

- Border changes mid-frame become visible: loading stripes, raster bars,
  "rainbow" effects.
- Attribute/pixel fetch at the real beam position → multicolour engines
  (Nirvana, Bifrost) and overscan demos display correctly.
- Optional: ULA "snow" when I register points into contended RAM.

**Acceptance:** Shock Megademo / Overscan border effects; a Nirvana-engine
game (e.g. Sunbucket) renders without attribute clash artifacts.

**Effort:** medium. Depends on Phase 1 for exact beam↔CPU alignment
(a per-scanline approximation can land first and is already useful).

**Status: shipped.** Lazy catch-up painter in video.c: border/display-RAM
writes first paint the frame up to the current (cycle-exact) T-state;
paper bytes are sampled at the ULA fetch slot in 16-px blocks, border at
2 px/T, lines 40–279 → the 320×240 output. Painting is enabled only for
observable frames (`fb_live`), so turbo runs lose nothing. Acceptance in
the suite: generated border-rainbow tap shows 8 colors/228 transitions
per border column (test 7b); a generated HALT-synced mini multicolour
engine puts ≥3 (measured 5) paper colors inside one attr cell (test 7c);
the z80ccfscr golden stays pixel-identical. ULA snow remains unimplemented
(optional).

## Phase 3 — Sound (beeper) — ✅ DONE (2026-06-10)

**What:** The HC-91 has a 1-bit beeper (ULA bit 4) — no AY chip.

- Record OUT-to-`0xFE` transitions with T-state timestamps (the hook
  already exists; timestamps are exact even pre-Phase-1).
- Headless: `--wav out.wav` writes the rendered audio of a run — testable
  in CI by checking dominant frequency of a `BEEP 1,0` (should be 261.6 Hz).
- Band-limited resampling (or simple low-pass) to 44.1 kHz.
- EAR/MIC mixing for authentic tape-loading noise (cosmetic, optional).

**Acceptance:** BASIC `BEEP` pitch test; Manic Miner title tune recognizable;
multi-channel beeper engines (Tritone, in e.g. Fairlight) don't crackle.

**Effort:** small–medium. Independent of Phases 1–2.

**Status: shipped.** `src/wav.c` streams speaker transitions into 44.1 kHz
WAV (`--wav`); suite test 5 asserts `BEEP 1,0` = 1.0 s @ 261.1 Hz
(`tests/check_beep.py`); Manic Miner's Blue Danube recorded as proof.
Remaining from this phase: band-limited resampling polish, EAR/MIC mix,
live output (folds into Phase 4 SDL).

## Phase 4 — Interactive frontend (SDL2) — ✅ DONE (2026-06-11)

**What:** Today the emulator is headless-only. Add a real-time frontend:

- SDL2 window, 50 Hz pacing (audio-clocked once Phase 3 lands), scaling.
- Host keyboard → matrix mapping incl. composed keys (Backspace =
  CAPS+0 etc.); host gamepad → Kempston.
- Live beeper audio; turbo key; pause; reset (48K vs HC-91 ROM switch).
- Keep the headless path as the default `make` target so tests never need
  a display; SDL behind `make sdl` / `#ifdef`.

**Acceptance:** play Jet Set Willy by hand at correct speed with sound.

**Effort:** medium. Depends on Phase 3 for audio, benefits from Phase 2.

**Status: shipped** (better than planned: no build split needed —
`src/sdl.c` dlopen()s the SDL2 runtime and declares the minimal ABI
itself, so the single `make` target keeps zero build dependencies and
`--sdl` simply errors politely if the .so is missing). 640×480 resizable
window (beam-painted frames), audio-clocked 50 Hz pacing with a
20 ms-tick fallback, live beeper via SDL_QueueAudio (coexists with
`--wav`), full host-keyboard matrix map (Shift=CAPS, Ctrl/LAlt=SYM,
Backspace=DELETE, Esc=BREAK, arrows→cursors/Kempston, punctuation
composes), first game controller → Kempston (d-pad + left stick + A),
Tab=turbo, F5=pause, F10/close=quit; `--sdl-frames N` auto-quits for
tests. Suite test 15 runs a 250-frame session under SDL's dummy drivers
(gated on the runtime being present; CI installs it); verified on a real
X11 display with Jet Set Willy to its menu screen at correct speed with
audio. ROM-switch reset key not implemented (use `--rom` per launch).

## Phase 5 — Real tape emulation + TZX — ✅ DONE (2026-06-11)

**What:** Current `.tap` loading is an instant ROM trap — custom/turbo
loaders (Speedlock etc.) don't work, and `.tzx` is unsupported.

- Pulse-level tape player feeding the EAR bit with T-state timing
  (standard ROM timings for `.tap`; the full block repertoire for `.tzx`:
  turbo blocks, pure tone/data, pauses, loops).
- Keep the trap as the fast path; `--real-tape` to force pulse loading.
- SAVE support: trap SA-BYTES (or capture MIC pulses) → write `.tap`.

**Acceptance:** the Arkanoid `.tap` loads via the real ROM loader (no trap);
a Speedlock-protected TZX (e.g. original release dump) loads. Requires
Phase 1 for timing-sensitive turbo loaders to be reliable.

**Effort:** medium (TZX block zoo is fiddly but well documented).

**Status: shipped.** The whole tape compiles to a flat pulse-duration
stream; EAR toggles per edge against the cycle-exact T-state clock
(`--real-tape`, `--play-at N`, PLAY auto-pressed at frame 320 with
autoload). TZX blocks 0x10–0x14, 0x20–0x25, 0x2A + info blocks are
supported (loops unrolled; 0x10/0x11 also feed the instant trap, so
`.tzx` works in both modes). SAVE is trapped at SA-BYTES 0x04C2 →
byte-exact `.tap` (`--save-tape`). Suite test 10 covers ROM pulse-loading
of .tap and .tzx, trap-loading .tzx, and a byte-exact SAVE; Arkanoid's
commercial .tap also loads through the real ROM loader. Not covered yet:
custom loaders needing in-game tape control UI (stop/start), CSW/
generalized-data blocks (0x18/0x19).

## Phase 6 — HC-91 specifics: CP/M mode (port 0x7E) — ✅ DONE (2026-06-11)

**What:** The genuine HC-91 ROM contains a bootstrap (at `0x386E`) that
copies a stub to RAM, does `OUT (0x7E),1` and jumps to 0 — on real
hardware this pages RAM over the ROM (the machine has 64 KB RAM total)
to boot CP/M from an external interface. This is *the* HC-91-unique
feature; emulating it would set this emulator apart from generic
Spectrum emulators.

- Research the paging hardware (MAME's `hc91` driver in
  `src/mame/sinclair/spectrum.cpp`-family code, Romanian documentation,
  the HC-2000 ROMs at speccy4ever which include the CP/M BIOS).
- Implement port `0x7E` RAM/ROM paging; investigate what disk device the
  BIOS expects (HC-2000-style floppy via an I/O window) and whether a
  CP/M boot is feasible without disk hardware emulation.

**Acceptance:** the ROM stub executes and lands in paged RAM at PC=0
without crashing; stretch goal — boot a CP/M prompt with HC-2000 BIOS +
emulated disk image.

**Effort:** research-bound; paging itself is small, disk emulation medium.

**Status: shipped (paging).** Research result: MAME's hc91 driver is a
plain ROM-swap clone and does *not* implement the paging — this emulator
goes beyond it. The ROM stub disassembles to `DI; LDIR 7 bytes to 0x8000;
JP 0x8000` running `LD A,1 / OUT (0x7E),A / JP 0`, which defines the
contract: port 0x7E (full low-byte decode), bit 0 pages a separate low
16K RAM bank over the ROM (64K machine). Implemented with the bank
pre-filled with HALT so a bare boot parks deterministically; snapshot
savers warn when paged. Suite test 12: marker round-trip through the
bank, ROM readback while paged out, execution at PC=0, and the genuine
`RANDOMIZE USR 14446` bootstrap parking at PC=1. Stretch (HC-2000 CP/M
BIOS + disk interface emulation) remains open.

## Phase 7 — Peripherals — ✅ DONE (2026-06-11)

- **Kempston joystick** (port `0x1F`, gated behind a flag because an
  *absent* Kempston must leave the port floating — Phase 1's bus rules).
- **Sinclair Interface 2 / cursor joysticks** (pure keyboard aliases).
- Out of scope for now: Interface 1/Microdrive, printers.

**Effort:** small. Unlocks joystick-only games and the SDL gamepad path.

**Status: shipped.** `--kempston` attaches the interface (A7-A5=0 decode;
absent ⇒ port keeps floating, so Arkanoid's beam-sync still works);
`--joy "F-G:DIRS"` holds U/D/L/R/F over a frame range, routed by
`--joy-type` to the Kempston byte or to Sinclair (6-0) / cursor (5,6,7,8,0)
matrix keys. Suite test 11 reads the port from BASIC (`IN 31` = 17/0)
and checks both key aliases.

## Phase 8 — Formats & state — 8a ✅ DONE (2026-06-11)

- Snapshot *saving*: `.sna` and `.z80` (we only load today) — also makes
  test recipes trivial (snapshot right before the interesting moment,
  e.g. the Arkanoid round-1 state, instead of long key scripts).
- `.szx` (zx-state) load/save — the modern interchange format.
- `.scr` screen import/export; `.rzx` input-recording playback for
  deterministic regression tests of whole gameplay sequences.

**Effort:** small per format; high testing payoff (snapshot-save first).

**Status: 8a shipped** — `--save-sna` / `--save-z80` (v2, uncompressed
pages) / `--save-scr`; `.scr` also loads (parks the CPU so the ROM can't
wipe it). HALT state survives by re-pointing PC at the HALT opcode.
Suite test 9 saves the running multicolour engine and resumes it from
both snapshot formats. Remaining: 8b (`.szx`, `.rzx`).

## Phase 9 — Debugger & tooling — ✅ DONE (2026-06-11)

- Promote `tests/pchist.c` into a proper monitor: Z80 disassembler,
  breakpoints/watchpoints (PC, memory, port), single-step, register and
  contention-aware T-state display, trace-to-file.
- Cross-check the core against TomHarte's ProcessorTests JSON vectors
  (per-instruction bus traces — would also validate Phase 1 M-cycles).
- Project hygiene: `git init`, CI workflow running `make test` (and the
  WAV/pitch test from Phase 3), AddressSanitizer build in CI.

**Effort:** incremental; the disassembler is the main chunk.

**Status: shipped.** `src/disasm.c` disassembles every opcode (undocumented
DDCB result-copy forms, ED no-ops, dead prefixes; 124-case `dtest`);
`src/debug.c` is a monitor with PC breakpoints, memory read/write and
I/O-port watchpoints, single-step, hex dump, registers with frame-relative
T-state, and per-instruction trace-to-file — scriptable via
`--debug "cmd;cmd"` (then stdin), so the suite drives it (5 tests).
`tests/sst.c` + `tests/get_vectors.sh` replay a 162-file subset of the
SingleStepTests/z80 vectors (registers incl. WZ/Q/EI-pending, full-RAM
diff, T-state totals, I/O transactions): **all 162,000 pass** — after
fixing a real bug they caught (repeating INIR/INDR/OTIR/OTDR set WZ=PC+1
when the repeat is taken); Rak's z80full/z80ccf/z80memptr still pass.
Git history started (software/ and vectors excluded);
`.github/workflows/ci.yml` runs build + suite + an ASan/UBSan job
(vectors cached). `tests/pchist.c` stays as a standalone PC-histogram
tool; the monitor supersedes it for interactive work.

## Phase 10 — Stretch: the rest of the HC family

The same chassis can host the HC-85/HC-88/HC-90 (ROMs already at
speccy4ever, all 48K-class) and, more ambitiously, the **HC-128 /
HC-2000** (128K-class: AY-3-8912 sound, memory paging via `0x7FFD`,
two screens, disk). That means AY emulation, 128K paging/contention
variants, and `.z80`/`.szx` 128K page sets — a separate project phase
of its own.

---

## Suggested order & rationale

| Order | Phase | Why now |
|-------|-------|---------|
| 1 | Contention (P1) | Biggest accuracy gap; everything else inherits exact timing; z80ccfscr already waiting in `software/z80test/` |
| 2 | Beeper sound (P3) | Independent, small, makes the emulator feel alive; WAV test extends CI |
| 3 | Beam video (P2) | Builds directly on P1; visible payoff (demos, loading stripes) |
| 4 | SDL frontend (P4) | Turns a test harness into a usable emulator |
| 5 | Real tape/TZX (P5) | Needs P1 timing; removes the biggest compatibility hole |
| 6 | Snapshot save (P8a) | Cheap, immediately improves the test suite |
| 7 | CP/M mode (P6) | The HC-91's signature feature; research can run in parallel |
| 8 | Joysticks (P7), debugger (P9), formats (P8b) | Quality-of-life, as needed |
| 9 | 128K family (P10) | Separate undertaking |
