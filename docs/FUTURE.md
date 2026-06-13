# HC-91 Emulator — Future directions & nice-to-haves

The emulator is feature-complete for its original goals: the ten
roadmap phases are done (see `ROADMAP.md`), and releases ship for
Linux amd64, Linux arm64 and Windows x86-64 (v1.2.0, 2026-06-13).
Nothing in this document is required — it captures forward-looking,
not-yet-committed ideas and the analysis behind them, so the next
session can pick up without re-deriving it.

---

## 1. iOS / iPhone release — *the biggest opportunity*

A native iPhone/iPad app is **technically very feasible**. The hard
part of any emulator — a correct, portable, cycle-accurate core — is
already done and already proven on arm64 (iOS is arm64, little-endian,
identical to the Linux arm64 build that matches amd64 byte-for-byte).

### Why it ports cleanly

- **The core is pure C99 with no platform lock-in.** Audit of
  `src/*.c` (excluding the SDL frontend) found only two non-portable
  spots: `<unistd.h>` in `debug.c`, and `mkstemp`/temp-dir use in
  `rzx.c` — and `rzx.c` already routes temp paths through a
  `TMPDIR`-aware helper (added for the Windows port), which iOS sets to
  the app sandbox automatically.
- **It's a pure interpreter — no JIT / no dynarec.** That sidesteps
  iOS's ban on executable memory pages, which is exactly what blocks
  many faster emulators from the App Store. A 3.5 MHz Z80 interpreter
  is trivial for any A-series chip; performance, battery and thermals
  are non-issues.
- **The design already matches the iOS app model.** `machine_run_frame()`
  is frame-stepped, mapping directly onto a `CADisplayLink` (vsync)
  callback. `video_render()` fills a flat 320×240 ABGR32 framebuffer —
  a one-call Metal/CoreGraphics texture upload. Audio is already
  44.1 kHz signed-16 PCM.

### What ports as-is vs. what is new work

| | Component | iOS status |
|---|---|---|
| **Reuse** | Z80 core, contention, beam video, tape/TZX/WAV, AY, FDC/CP/M, snapshots, `keys.c` matrix logic | compiles as-is (arm64 C99) |
| **Reuse (4 API seams)** | `machine_run_frame`, `video_render`→fb, `beep_flush`/AY→PCM, `keyrows[]`/`kempston` | the natural library surface |
| **New** | `sdl.c` frontend | replaced by native Swift/Metal/CoreAudio |
| **New** | input sources | on-screen keyboard + touch joystick + `GameController` |
| **New** | file access | document picker / Files / share-sheet → sandbox |

### Implementation steps

1. **Refactor the core into a reentrant C library + a small frontend
   API** — e.g. `hc91_create(rom)`, `hc91_load_file(path)`,
   `hc91_run_frame()`, `hc91_framebuffer() -> const uint32_t*`,
   `hc91_audio_pull(int16_t*, n)`, `hc91_key(row,bit,down)`,
   `hc91_set_kempston(byte)`. Prove it with a headless harness that
   drives the machine through that API only.
   **Doable entirely on Linux, no Mac required**, and it cleans up the
   codebase regardless. This step is the linchpin (it also enables an
   Android port for nearly free — same library + an NDK/Kotlin
   frontend).
2. **Native frontend (Swift + Metal + CoreAudio).** Invert control:
   iOS owns the run loop and calls `hc91_run_frame()` from a
   `CADisplayLink`; blit the framebuffer to an `MTLTexture`; a CoreAudio
   render callback drains a sample ring buffer (adapt the beeper's
   per-frame synth to fill that ring).
3. **Input.** On-screen ZX-Spectrum keyboard overlay + virtual D-pad /
   fire (Kempston); `GameController` framework for MFi/Bluetooth pads
   and hardware keyboards (iPad).
4. **Files.** Import tapes/snapshots via the document picker / Files /
   share-sheet into the sandbox; reuse `machine_load_file`'s
   by-extension dispatch unchanged. ROMs in the app bundle, or
   fetch-on-demand on first run (mirroring `tools/get_roms.sh`).
5. **Xcode project + a macOS GitHub Actions workflow** so CI can build
   and sign it even though day-to-day development is on Linux.

### Limitations & constraints

- **Build environment is the hard blocker.** Apple's toolchain (Xcode,
  code-signing, simulator/device, App Store submission) is
  **macOS-only**. A Linux machine *cannot* build, sign, run or submit
  an iOS app. Steps 2–5 need a Mac or a cloud-macOS CI runner. (Step 1,
  the core library, is fully doable and testable on Linux.)
- **Distribution / cost.** Free Apple-ID sideload works but the profile
  **expires every 7 days** (re-sign weekly) — fine for personal use.
  Proper distribution needs the **Apple Developer Program ($99/yr)** →
  TestFlight or the App Store. In the EU, the DMA (iOS 17.4+) allows
  notarized sideloading / alternative marketplaces.
- **App Store policy.** Guideline 4.7 (updated 2024) explicitly permits
  retro console/computer emulators (Spectrum emulators are already on
  the store), and the no-JIT design is compliant — but review is
  case-by-case and the app is held responsible for all content it
  offers.
- **IP / ROMs — the biggest *non-technical* constraint.** Same rules as
  this repo, amplified by review: **no bundled copyrighted games**
  (users import their own, as `software/` is already kept out of git);
  and the I.C.E. Felix HC system-ROM dumps have murkier licensing than
  the Amstrad-permitted Sinclair 48K ROM. Cleanest answer: **bundle
  only the 48K ROM, fetch the HC ROMs on first run, and have users
  bring their own tapes via Files.**

### Effort

| Step | Where | Effort |
|---|---|---|
| 1. Core → library + API + headless test | Linux (here) | small, ~½–1 day, fully testable |
| 2–4. Swift/Metal/CoreAudio frontend + touch UI | needs a Mac | medium, ~2–4 days for a solid v1 |
| 5. Xcode project + macOS CI | needs a Mac to verify | small |

**Recommended first move:** do step 1 now (Linux-only, improves the
codebase, and unlocks both iOS *and* Android later); defer steps 2–5
until a Mac / macOS CI is available.

---

## 2. Other nice-to-haves

- **Android port.** Falls out almost for free once step 1 above exists:
  the same C library via the NDK, with a Kotlin + Vulkan/`GameActivity`
  (or SDL2) frontend. No App-Store-style ROM/JIT constraints.
- **HC-88 disk hardware.** The last untouched core stretch item: a 2K
  boot ROM, undocumented, with no known surviving software to test
  against — high effort, uncertain payoff. Lowest priority.
- **AY stereo output** (ACB/ABC panning) for the HC-128.
- **Kempston mouse** emulation.
- **A config file** (`~/.config/hc91emu.conf`) for default machine,
  scale, joystick type, etc.

> Done in v1.2.0 (no longer "future"): WAV cassette input, fuzz-hardened
> loaders, the arm64 package, and the multicolour-demo golden.
