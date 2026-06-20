# HC-91 Emulator — Architecture

A source-code-level tour of `hc91emu`: a cycle-accurate, dependency-free
emulator for the I.C.E. Felix **HC-91** (a Romanian ZX Spectrum 48K clone)
and the wider HC family (HC-85, HC-90, HC-128, HC-2000).

This document is derived from an audit of the source tree and is meant to
orient a new contributor: what each module does, how the pieces connect,
where the timing-critical design decisions live, and how the project is
validated. File/line references are relative to the repository root.

---

## 1. What is being emulated

| Aspect | Emulated target |
|--------|-----------------|
| CPU    | Z80A / Romanian MMN80 clone @ 3.5 MHz (3,500,000 T-states/s) |
| Video  | ULA: 256×192 paper, 15 colours, 32×24 border → rendered at 320×240 |
| Frame  | 50 Hz, **69,888 T-states/frame** (312 lines × 224 T) |
| Audio  | 1-bit beeper (ULA bit 4); AY-3-8912 PSG on the HC-128 |
| Memory | 16K ROM + 48K RAM (HC-91); 128K banked + AY (HC-128); disk + CP/M (HC-2000) |
| I/O    | ULA port `0xFE` (keyboard/border/EAR/speaker), Kempston `0x1F`, AY `0xFFFD/0xBFFD`, i8272 FDC `0x85/0x87` |

The HC-91 ROM differs from the Sinclair 48K ROM by only ~50 bytes
(copyright banner + a small CP/M boot stub), so the machine is fully
48K-Spectrum software compatible — a fact the project leans on hard.

---

## 2. Repository layout

```
hc91emu/
├── src/            # all emulator code, plain C99 + libc (one TU per concern)
│   ├── z80.[ch]        # bus-callback Z80 core (no machine knowledge)
│   ├── machine.[ch]    # the machine: memory map, I/O bus, frame loop, file dispatch
│   ├── video.c         # beam-accurate renderer + ROM-font OCR
│   ├── wav.c           # beeper → 44.1 kHz WAV synthesis
│   ├── ay.c            # AY-3-8912 PSG (HC-128)
│   ├── tape.c          # .tap/.tzx/.wav: trap loader + pulse player + SAVE
│   ├── fdc.[ch]        # uPD765/i8272 floppy controller (HC-2000)
│   ├── snapshot.c      # .sna / .z80 v1-v3 / .szx / .scr load+save
│   ├── rzx.[ch]        # RZX input record/replay (deterministic regression)
│   ├── keys.c          # scheduled keyboard events, text typing, joysticks
│   ├── disasm.[ch]     # full-coverage Z80 disassembler (debugger-driven)
│   ├── debug.[ch]      # scriptable monitor: bp/watch/step/trace
│   ├── inflate.[ch]    # ~300-line built-in DEFLATE/zlib inflater (SZX/RZX)
│   ├── png.c           # minimal PNG writer (STORED deflate, no deps)
│   ├── sdl.c           # optional SDL2 frontend (dlopen'd at runtime)
│   ├── main.c          # CLI driver: option parsing, run loop, output
│   └── zexrun.c        # standalone CP/M harness for zexdoc/zexall
├── tests/          # unit + integration tests + golden images + test-tap generators
├── tools/          # ROM/library fetchers, packaging (deb/windows), icon
├── docs/           # manual.pdf, man page, FUTURE.md
├── .github/workflows/ci.yml
└── Makefile
```

The whole emulator is **~7,700 lines of C99** with **zero external
dependencies** (only libc + `-ldl` for the optional SDL runtime binding).
PNG encoding, DEFLATE inflation, WAV writing and the full SDL2 ABI slice
are all implemented in-tree.

---

## 3. Layered architecture

The design is a clean, dependency-pointing-downwards stack. The Z80 core
sits at the bottom and knows nothing about the machine; everything above
plugs bus behaviour into it through callbacks.

```
                ┌──────────────────────────────────────────────┐
  CLI / UX      │ main.c  (option parse, run loop, output)      │
                │ sdl.c   (optional interactive frontend)       │
                ├──────────────────────────────────────────────┤
  Peripherals   │ tape.c  fdc.c  ay.c  keys.c  wav.c           │
                ├──────────────────────────────────────────────┤
  Machine       │ machine.c  — memory map, I/O bus, frame loop │
                │ video.c    — beam renderer (reads m->screen) │
                │ snapshot.c rzx.c debug.c  (hook the bus)     │
                ├──────────────────────────────────────────────┤
  CPU core      │ z80.c — bus-agnostic, contention via callback │
                ├──────────────────────────────────────────────┤
  Utilities     │ inflate.c  png.c  disasm.c                   │
                └──────────────────────────────────────────────┘
```

The single shared state object is `struct Machine` (`machine.h:114`),
an aggregate that owns the CPU, all memory banks, every peripheral and
the framebuffer. It is allocated statically in `main()` (off the stack,
because it is large) and passed by pointer everywhere. There is **no
global mutable state** outside it, which is what makes the test harness
(`zexrun.c`) and the headless/SDL split trivial.

---

## 4. The Z80 CPU core (`src/z80.c`, `z80.h`)

This is the heart of the project (~1,140 lines) and the place where
emulation accuracy lives. It is deliberately **bus-agnostic**.

### 4.1 Register model

`struct Z80` (`z80.h:18`) holds the full documented register set plus the
hardware-internal state needed for cycle-exact behaviour:

- main + alternate register pairs (`af..hl`, `af_..hl_`) using a little-
  endian `RegPair` union (`z80.h:11`) so `af.b.h`/`af.b.l` access bytes;
- `ix`, `iy`, `sp`, `pc`;
- **`memptr` (WZ)** — the internal address latch;
- **`q`** — the internal Q register (the previous F if the last
  instruction modified flags, else 0) which drives SCF/CCF X/Y behaviour;
- `iff1/iff2`, `im`, `halted`, `ei_pending`;
- two 64-bit counters: `tstates` (the master clock) and `fetches`
  (every R bump, used as RZX frame currency).

### 4.2 Bus-callback contract

The core never touches memory directly. All access goes through six
function pointers in the struct (`z80.h:37-52`):

| Callback | Purpose |
|----------|---------|
| `mem_read` / `mem_write` | data transfer |
| `io_read` / `io_write` | port I/O |
| `mem_contend` | T-states to stall for a given address |
| `io_contend_early` / `io_contend_late` | I/O contention (pre/post value) |

This is the key abstraction: `z80.c` is reusable and testable in
isolation. `tests/zexrun.c` wires it to flat RAM with no contention;
`machine.c` wires it to the full contended Spectrum bus.

### 4.3 Incremental T-state accounting

Timing is charged **per M-cycle**, not per instruction. The helpers
(`z80.c:58-108`) are the load-bearing primitives:

- `ct(addr, len)` — adds contention for `addr`, then `len` T-states;
- `intern(addr, n)` — `n` individually-contended 1T internal cycles;
- `mrd`/`mwr` — 3T memory read/write (contention first);
- `fetch_op` — 4T M1 fetch, bumps R and `fetches`;
- `io_in`/`io_out` — the full 4T I/O cycle: `early` contention, 1T,
  `late` contention, value transfer, 3T.

Because contention is evaluated *at the address and time of each access*
(the callee sees an up-to-date `z->tstates`), the CPU timeline tracks
real hardware exactly, and the floating bus + beam video fall out for
free.

### 4.4 Instruction decode

`z80_step()` (`z80.c:1050`) is the public entry point:

1. Handle `HALT` (4T NOP-like cycle, R bump).
2. Consume any run of `0xDD`/`0xFD` prefixes (only the last wins, as on
   real hardware) into a `pfx` selector {0=none, 1=IX, 2=IY}.
3. Dispatch on `0xCB` (do_cb), `0xED` (do_ed), or the main table
   (do_main), passing `pfx` so register selection can remap H/L→IXH/IXL.
4. Set `q` from a precomputed `*_modifies_f()` predicate (`z80.c:1014`)
   — notably `POP AF` and `EX AF,AF'` do **not** latch Q, matching
   Zilog NMOS (this was a real bug caught by Rak's z80ccf).

Interrupts: `z80_int(bus)` (`z80.c:1092`) implements the 7T INT ack
(honouring `ei_pending`, IFF1/2, IM 0/1/2); `z80_nmi()` the 5T NMI to
`0x0066`. `z80_reset()` (`z80.c:993`) sets power-on register values.

Validation is exhaustive: **zexdoc/zexall 67/67**, Patrik Rak's
z80full/z80ccf/z80memptr **160/160**, and **all 162,000 SingleStepTests/z80**
vectors (replayed by `tests/sst.c`).

---

## 5. The Machine layer (`src/machine.c`, `machine.h`)

This is where the emulated computer lives. It owns the bus callbacks and
the frame loop.

### 5.1 Memory maps

`machine_peek()` (`machine.c:22`) and `bus_mem_write()` (`machine.c:69`)
encode the memory map, branching on the active model:

- **48K (HC-85/90/91, plain 48K):** `mem[65536]` with the low 16K ROM
  write-protected. `mem_cpm[0x4000]` is a separate low RAM bank paged
  over the ROM by **port `0x7E` bit 0** — the HC-91's signature CP/M
  feature (the ROM bootstrap at `0x386E` does `OUT (0x7E),1; JP 0`).
  It is pre-filled with `0x76` (HALT) so a bare boot parks cleanly.
- **128K (HC-128):** eight 16K banks in `ram128[8]`, with `0x4000`=bank5,
  `0x8000`=bank2, `0xC000`=bank (latch & 7); a second ROM `rom1`;
  `port_7ffd` bits select shadow screen (bit 3), ROM (bit 4), lock
  (bit 5). Odd banks are contended. `m->screen` always points at the
  active display bank so the renderer is model-agnostic.
- **HC-2000:** the most complex map. The `cfg_7e` system latch
  (`(port & 0x81)==0`, read-back, self-locking) selects BASIC vs CP/M
  ROM, can relocate the ROM window to `0xE000` with `mem_cpm` low,
  relocate video to `0xC000`; a separate CPM flip-flop (write `0xC7`
  set / `0xC5` clear) pulls A13 high so `0xE000` RAM appears at
  `0xC000`. Plus the "IF1" disk interface: an 8K shadow ROM
  (`rom_if1`) paged at `0x0008`/`0x1708` and out after `0x0700`, with
  its own 16K interface RAM (`if1_ram16`).

### 5.2 Contention model

`contention_delay()` (`machine.c:126`) implements the canonical ULA
stall pattern `6,5,4,3,2,1,0,0` per 8 T-states, starting at **T=14335**
after the frame interrupt, for the first 128 T of each of the 192 display
lines. The callbacks route it:

- `bus_mem_contend` — contended RAM (`0x4000-0x7FFF`) and odd 128K banks;
- `bus_io_contend_early`/`_late` — the documented I/O contention table
  (early for ULA/contended high-byte; late re-evaluates contention at
  each simulated sub-cycle for non-ULA contended ports, `machine.c:168`).

### 5.3 The I/O bus

`bus_io_read_raw()` (`machine.c:187`) decodes, in order: the HC-2000
config latch, the ULA port (even addresses) — keyboard matrix + EAR bit
(which carries the tape signal while playing, else follows the speaker
bit — Issue-3 behaviour) — Kempston (`0x1F`), AY read (`0xFFFD`), and
the FDC (`0x85/0x87`). Anything unattached falls through to the
**floating bus** (`machine.c:231`): while the ULA fetches display data it
leaves that byte on the bus (pixel/attr at the right T-slot, `0xFF`
otherwise) — essential for games like Arkanoid that beam-sync on it.

`bus_io_write()` (`machine.c:266`) routes writes to the 128K bank latch
(`0x7FFD`), AY select/data, FDC, the HC-2000 config/CPM latches, the
HC-91 `0x7E` pager, and finally the ULA (border colour + beeper edge,
which feeds `wav.c`).

Two wrappers add orthogonal concerns: `bus_io_read`/`_write` splice in
**RZX playback/recording** (`rzx_in`/`rzx_log_in`) and **debugger
watchpoints** (`debug_note_*`), without polluting the raw decode.

### 5.4 The frame loop

`machine_run_frame()` (`machine.c:501`) is the master clock driver and
is worth understanding in full:

```
end = frame_start_ts + 69888          # drift-free frame boundary
z80_int(0xFF)                          # 50 Hz maskable interrupt
while cpu.tstates < end:
    pc0 = cpu.pc
    debug_step_hook()                  # trace + breakpoint decisions
    if tape attached & !real_tape & pc==0x0556: tape_trap()   # instant LOAD
    if save_tape & pc==0x04C2: tape_save_trap()               # instant SAVE
    if have_if1 & !if1_paged & pc in {0x0008,0x1708}: if1_paged=1
    z80_step()
    if if1_paged & pc0==0x0700: if1_paged=0                    # /ROMCS timing
    if !int_taken & within 32T window: retry z80_int()         # real INT width
rzx_frame_end()
if fb_live: video_beam_finish()
frame_start_ts = end ; frame_counter++
```

Three properties matter:

1. **Drift-free:** frame boundaries are exact multiples of 69,888 T;
   instruction overshoot carries into the next frame, keeping the
   contention/floating-bus phase locked to the interrupt forever.
2. **The INT window is the real ~32 T**, not a generous approximation.
3. **ROM traps** (tape LOAD/SAVE) and the IF1 shadow-ROM paging are PC
   predicates evaluated *before* each step, modelling the real hardware
   hooks (`0x0556` = LD-BYTES, `0x04C2` = SA-BYTES, `0x0008`/`0x1708` =
   IF1 /ROMCS).

---

## 6. Beam-accurate video (`src/video.c`)

Rather than rendering once per frame, the display is **painted
incrementally at the beam position** so mid-frame effects are correct.

- The 320×240 framebuffer lives in `m->fb` (`machine.h:194`).
- Whenever the CPU writes display RAM or changes the border *during a
  live frame*, `bus_mem_write`/`bus_io_write` call `video_beam_catchup`
  first, which runs `paint_span()` (`video.c:93`) up to the current
  T-state — paper bytes are sampled at the ULA fetch slot (8-T blocks,
  giving exact write-race semantics), border at 2 px/T.
- `video_beam_finish()` paints to frame end and marks `fb_valid`.
- Painting is **gated on `fb_live`**: turbo/headless runs that nobody
  observes skip it entirely (`main.c` sets `fb_live = (f == frames-1)`).
- `paint_cell()` (`video.c:23`) also models **ULA snow**: with `I` in
  `0x40-0x7F` the refresh address collides with the ULA fetch on the
  shared bus, so the low 7 bits of the pixel/attr address come from `R`.
- `video_render()` is the fallback full-frame blit when no beam frame
  exists, and `video_screen_text()` does **ROM-font OCR** (matching each
  8×8 cell against the font at `0x3D00`, direct or inverted) — this is
  how the test suite asserts on screen contents without image diffing.

Colours are packed RGBA with the Spectrum's B/R/G bit ordering
(`spec_color`, `video.c:12`).

---

## 7. Audio

### 7.1 Beeper (`src/wav.c`)

The ULA speaker (bit 4 of OUT `0xFE`) is the only sound on 48K machines.
`machine.c` calls `beep_edge(tstates, level)` on every transition; the
level is **box-filtered into 44.1 kHz 16-bit mono samples on the fly**
(no event buffer) and `beep_save()` writes the WAV at exit. Granularity
is the instruction boundary (<4 µs). `tests/check_beep.py` asserts
`BEEP 1,0` renders ~1 s of ~261.6 Hz (middle C).

### 7.2 AY-3-8912 (`src/ay.c`, HC-128)

A full PSG: 3 tone channels + noise + envelope, clocked at 1.7734 MHz
with a /16 prescale. `ay_sample()` advances a fractional accumulator
(~2.513 master ticks per 44.1 kHz sample) and sums the three channels
through the classic measured 4-bit log volume curve (`vol_tab`).
Register writes are **sample-accurate**: `machine.c` calls
`beep_flush()` before applying an AY write so audio synthesis advances
to the exact current T-state first.

---

## 8. Storage: tape and disk

### 8.1 Tape (`src/tape.c`)

Two loading paths share one pulse stream:

- **Trap loading** (default, instant): when PC hits `0x0556` (LD-BYTES),
  `tape_trap()` copies the next block straight into RAM. `.tap` blocks
  are also stored in `m->tape.blocks[]` for this path.
- **Pulse loading** (`--real-tape`): the whole tape is compiled at load
  time into a flat list of pulse durations (`pulse_push`/`emit_data`,
  `tape.c:33-81`) using standard ROM timings. The player toggles EAR at
  each edge against the cycle-exact T-state clock
  (`tape_player_ear`, `tape.c:700`).

Supported formats: `.tap` (raw), `.tzx` (full block repertoire incl.
turbo, CSW `0x18` RLE+zlib, generalized-data `0x19`), and sampled
`.wav` (DC removal + Schmitt trigger, 4–192 kHz, mono/stereo).

**Multi-load intelligence** (`tape.c:691`): at block boundaries the
player pauses unless the CPU is *hot*-polling the EAR port (≥32 reads
per 10,000-T window, excluding the ROM keyboard scanner at
`0x028E-0x02BE`). This makes "stop the tape / press any key" schemes and
rewind prompts work without UI. `--tape-b` + `tape_swap()` model a
second cassette side.

`SAVE` is trapped at `0x04C2` (SA-BYTES) and captured byte-exact to
`.tap` (`--save-tape`).

### 8.2 Disk (`src/fdc.c`, `fdc.h`) — HC-2000

A polled uPD765/i8272 emulation. The interface puts the FDC at
`0x85`/`0x87` with a control latch at `0x05/0x07` (TC, drive select,
reset). Implemented commands: SPECIFY, SENSE DRIVE/INTERRUPT STATUS,
RECALIBRATE, SEEK, READ ID, READ/WRITE DATA (multi-sector, TC/EOT
terminated), FORMAT TRACK; unknown commands return the invalid ST0.
Seeks are instantaneous; transfers are byte-polled through MSR
RQM/DIO. Raw `.img`/`.dsk` images in the four period geometries
(640K/720K 80-track, 320K/360K 40-track) are recognized by file size;
dirty images are written back at exit (`fdc_free`).

Together with the HC-2000 memory map (§5.1) this boots **CP/M 2.2 from
the system tracks to the `A>` prompt** (`--boot-cpm` or `RANDOMIZE USR
14446`), with `DIR` listing real disk contents.

---

## 9. State: snapshots and recordings

### 9.1 Snapshots (`src/snapshot.c`)

Read/write for the full interchange zoo:

- `.sna` (48K), `.z80` v1/v2/v3 (48K and 128K hardware mode 3, pages
  3–10; v1 RLE-compressed), `.szx` (zx-state v1.4: CRTR/Z80R/SPCR/RAMP
  blocks, with **zlib-compressed RAM pages loaded through the built-in
  `inflate.c`**), raw `.scr` (6912-byte screen import/export).
- HALT state survives saving by pointing PC back at the HALT opcode
  (`snap_pc`, `snapshot.c:56`), since none of the formats have a halted
  flag.
- 48K snapshots load into 128K as a USR0-style bank set; the 48K machine
  refuses a 128K file with a hint.

### 9.2 RZX (`src/rzx.c`, `rzx.h`)

A **record/replay pair** for deterministic whole-session regression.
Frames are bounded by the `fetches` counter (the core bumps it on every
R increment, `z80.c:116`), and port reads are fed from the recording
during playback (`rzx_in`). So a captured session — keystrokes and all —
replays bit-exactly. Recording embeds a `.z80` snapshot at start;
protected/encrypted input blocks are rejected.

---

## 10. Input (`src/keys.c`)

`keys_apply(frame)` materializes the 8-row keyboard matrix into
`m->keyrows[]` each frame from a list of `KeyEvent` ranges. Three input
modes build those events:

- `keys_type(text, frame)` — types a BASIC string (keyword keys + symbol
  composition, with hold/gap timing) starting at a frame; this is what
  `--type` and `--autoload` use.
- `keys_raw(frame, "CAPS+SYM")` — a raw key chord by name.
- `keys_joy(f0,f1,"UDLRF")` — joystick, routed by `--joy-type` to
  Kempston (`0x1F`), Sinclair (matrix keys 6-0) or cursor (5,6,7,8,0).

Joysticks: Kempston is gated behind `--kempston` because an *absent*
interface must leave `0x1F` floating (Phase-1 bus rules), which
Arkanoid's beam-sync depends on.

---

## 11. Debugger and disassembler

### 11.1 Disassembler (`src/disasm.c`)

Full-coverage, decoded **algorithmically by the x/y/z/p/q bit fields**
rather than a 256-entry table, so all prefixes (CB/ED/DD/FD/DDCB/FDCB),
undocumented result-copy forms, ED no-ops and dead prefixes are handled
uniformly. Dead prefixes render as `DEFB` so the byte stream is always
honest. Locked by `tests/dtest.c` (124 cases).

### 11.2 Monitor (`src/debug.c`, `debug.h`)

A real debugger layered on the bus hooks:

- PC **breakpoints**, memory read/write **watchpoints**, I/O **port
  watchpoints** (each up to `DBG_MAX_BP`=32);
- single-step, hex dump, registers with frame-relative T-state;
- per-instruction **trace-to-file** (`trace_line`);
- scriptable via `--debug "regs;step 3;cont"` then stdin (auto-continues
  when both are exhausted so headless runs never hang).

The `debug_note_*` hooks arm a stop that fires before the next
instruction via `debug_step_hook` in the frame loop — clean separation
from the timing-critical core.

---

## 12. The SDL frontend (`src/sdl.c`)

The standout engineering choice: SDL2 is **dlopen()'d at runtime** and a
minimal slice of its ABI (struct layouts, constants —
`MYSDL_*`, `MyAudioSpec`) is declared in-tree, so the build needs **no
SDL headers or link libraries**. `-ldl` is the only non-libc dependency,
and only for pre-2.34 glibc. `--sdl` errors politely if the `.so` is
missing. (The same late-binding trick is reused for Windows via
`LoadLibraryA`.)

The frontend: resizable window rendering the beam-painted frames,
**audio-clocked 50 Hz pacing** (queue beeper samples/frame, sleep while
>4 frames buffered; 20 ms tick fallback), full host-keyboard→matrix map
(Shift=CAPS, Ctrl/LAlt=SYM, Backspace=CAPS+0, Esc=BREAK, arrows→
cursors/Kempston), first game controller → Kempston, plus turbo/pause/
quit, quick save/load (one `.szx` slot), fullscreen, and drag-and-drop
file loading. Headless stays the default so tests never need a display.

---

## 13. CLI driver (`src/main.c`)

`main()` is a straightforward ~520-line option parser and orchestrator:

1. Parse options into locals + configure the machine (model, ROMs, disks,
   tape, debugger, joysticks, audio, RZX).
2. `machine_init()` + model-specific setup (`machine_set_128/_if1/_boot`).
3. `machine_load_file()` dispatches the input by extension
   (`.tap/.tzx/.wav/.sna/.z80/.szx/.scr/.rzx`).
4. Either `sdl_run()` (interactive) or a fixed-frame headless loop.
5. On exit: flush WAV, save snapshots/scr, render screenshot/text, write
   back dirty disks, free RZX/tape.

ROM path resolution (`find_data`, `main.c:74`) prefers the relative path
(in-tree runs) and falls back to `HC91_DATADIR`
(`/usr/share/hc91emu`) for installed packages.

---

## 14. Build system (`Makefile`)

- `CC=gcc`, `CFLAGS=-std=c99 -O2 -Wall -Wextra` — strict, no warnings.
- `MACHINE_OBJS` lists the 15 emulator TUs; `hc91emu` links them with
  `-ldl`.
- `all` also builds every test binary; each test links only the objects
  it needs (e.g. `ttest`/`ctest`/`sst` link just `z80.o`, `dtest` links
  `disasm.o`) — a nice property of the bus-callback design.
- `windows` cross-compiles a mingw build; `manual` rebuilds the PDF +
  man page; `deb`/Windows packaging lives in `tools/`.
- The audit build was **clean with zero warnings**, and `ttest`/`ctest`/
  `dtest` all pass (45 timing cases, 50 contention patterns, 124
  disassembly cases).

---

## 15. Testing and validation strategy

The project's accuracy claims rest on a layered suite
(`tests/run_tests.sh`, ~535 lines):

| Layer | What | How |
|-------|------|-----|
| CPU unit | T-state totals, contention M-cycles, disassembly | `ttest`/`ctest`/`dtest` — pure, no ROMs |
| CPU exerciser | documented + undocumented flags | `zexrun` runs `zexdoc.com`/`zexall.com` (CP/M BDOS trapped at `0x0005`) |
| CPU vectors | full per-instruction bus traces | `sst` replays 162 SingleStepTests/z80 files (162,000 cases) |
| In-machine | Rak's z80full/ccf/memptr | loaded as tapes, screen asserts "all tests passed" |
| Integration | boot, BASIC, games, audio, tape, disk, CP/M, SDL | golden PNGs + screen OCR + WAV pitch analysis |

Key testing ideas: generated `.tap` files (`bordertap.c`, `multitap.c`,
…) produce *known* beam effects to lock the renderer; `--text` OCR avoids
image-diff fragility; `--fb-dump` + `fbcheck.c` do pixel-exact checks;
RZX gives bit-exact whole-session regression.

**CI** (`.github/workflows/ci.yml`) runs only the **download-free**
subset (no ROMs, no copyrighted data, no third-party hosts): build +
`ttest/ctest/dtest/zexdoc`, repeated under **ASan + UBSan**. The heavier
fetch-based tests run locally via `make test`.

---

## 16. Notable engineering decisions

1. **Bus-callback CPU core** (`z80.c`) — the single most important
   abstraction. It makes the core reusable (`zexrun`), testable in
   isolation, and decouples timing accuracy from machine specifics.
2. **Incremental, per-M-cycle T-state accounting** with contention
   evaluated at access time — the foundation that makes contention,
   floating bus and beam video all cycle-exact simultaneously.
3. **Zero external dependencies**, including a hand-written DEFLATE
   inflater, PNG encoder and SDL ABI slice. Keeps the build trivial and
   the binary self-contained.
4. **`dlopen` SDL** instead of a build-time dependency — headless stays
   the default, tests need no display, and the single `make` target
   builds everything.
5. **One aggregate `Machine` struct, no globals** — clean ownership, easy
   to reset, snapshot and reason about.
6. **Drift-free frame loop** (exact 69,888-T boundaries, overshoot carried
   forward) keeps phase locked indefinitely.
7. **Test tape generation + OCR assertion** instead of brittle image
   diffs — deterministic and human-readable.
8. **Going beyond MAME**: MAME's `hc91` driver is a plain ROM-swap; this
   emulator implements the actual CP/M paging (port `0x7E`), the HC-128's
   genuine banking+AY (confirmed by disassembling its ROM), and the
   HC-2000's full disk interface + CP/M boot — documented against Alex
   Badea's FUSE hc2000 semantics.

---

## 17. Intellectual property

Source under the **MIT License** (`LICENSE`). The MIT grant covers the
emulator only. **ROMs and period software are never committed** —
`tools/get_roms.sh` fetches ROM dumps on demand (sha256-pinned) and
`tools/get_library.sh` builds a local game library; both are copyrighted
by their owners and excluded from the repository (`.gitignore`) and from
the CI, which never touches the network.

---

*Derived from a source audit; module sizes and line counts reflect the
audited tree (~7,700 lines of C99 across `src/`).*
