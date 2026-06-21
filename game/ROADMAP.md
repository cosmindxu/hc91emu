# STELLAR DRIFT — Roadmap

A plan for turning the current playable prototype into a polished,
nostalgic 8-bit shoot-'em-up. Items are grouped by theme and ordered
roughly by impact-per-effort. Everything here is feasible on a 48K
Spectrum / HC-91 unless noted; a few stretch goals target the 128K /
HC-128 (AY sound, RAM banking) which this emulator already supports.

Status legend: ✅ done · ◐ partial · ☐ todo

---

## 1. Feel & game-play (the core loop)

- ✅ Up/down/left/right flight, auto-fire, dodge + shoot + collect.
- ☐ **Momentum / inertia on the ship.** A little acceleration and drift
  makes piloting feel like a craft, not a cursor. (Add `vx`/`vy` with
  friction.)
- ☐ **Enemy movement patterns.** Right now hazards only drift left. Add
  sine-wave flyers, homing drones, and aimed shots so dodging has
  texture. Table-driven per enemy type.
- ☐ **Enemy bullets / return fire.** Turns "avoid the walls" into a real
  bullet-dodging duel.
- ☐ **Power-ups.** Collect to upgrade: twin shot, rapid fire, shield,
  speed-up — the classic Nemesis/Zynaps progression bar.
- ☐ **End-of-zone bosses.** A big multi-hit sprite gates each zone for a
  memorable spike. Uses the existing blitter with a wider composite.
- ☐ **Difficulty curve & rubber-banding.** Smooth ramp within a zone, not
  just a step at the boundary.
- ☐ **Collision fairness.** Tighten hit-boxes to the visible hull
  (current boxes are generous 13px squares) and add a few frames of
  coyote-time.

## 2. Audio (huge for immersion)

- ✅ Beeper SFX: shoot, explosion, pickup, zone-change sweep.
- ☐ **Richer beeper effects** with pitch envelopes (descending boom for
  explosions, sparkly arpeggio for pickups).
- ☐ **In-game beeper engine drone** between effects so it never feels
  silent (very period-correct — think Cobra/Sabre Wulf).
- ☐ **128K AY-3-8912 music & SFX** (HC-128 / `--machine hc128`). The
  emulator fully emulates the PSG: a title tune and a driving in-game
  loop on three channels would transform the mood. Detect 128K at boot
  and fall back to beeper on 48K.
- ☐ **AY drums/noise channel** for explosions on 128K.

## 3. Colour & graphics

- ✅ Side-on ship sprite; per-object attribute colour; per-zone palette
  and border.
- ☐ **Banded / gradient backdrops per zone** (sky→ground colour bands)
  for depth without attribute clash on the sprites.
- ☐ **Parallax starfield in 2–3 depth layers** with brightness via
  bright-bit, not just speed.
- ☐ **Sprite animation.** Engine-exhaust flicker on the ship, spinning
  asteroids, pulsing crystals — 2–3 frames each.
- ☐ **Explosion animation** (expanding ring sprite) instead of a plain
  erase.
- ☐ **Scrolling foreground terrain** (cave walls top & bottom) to lean
  fully into the cave-flyer fantasy. Hardest item; do as a
  character-cell scroll first.
- ☐ **Anti-clash tricks**: keep moving sprites a single ink, reserve a
  colour per object class, draw bullets in their own cells.

## 4. Presentation & UX

- ◐ Title screen + game-over (text only today).
- ☐ **Loading screen (`SCR`)** — the single most nostalgic thing a
  Spectrum game can have. A drawn title image that paints in as the tape
  loads. (Author a 6912-byte screen; prepend to the tape.)
- ☐ **Attractive title screen**: logo, animated demo/attract mode,
  scrolling credits, "PRESS FIRE" blink.
- ☐ **High-score table with 3-letter initials entry** — the arcade ritual.
  Persist within the session; on 128K, consider saving.
- ☐ **Pause** (and a tasteful paused overlay).
- ☐ **Control selection menu**: Keyboard / Kempston / Sinclair / Cursor,
  plus redefine-keys (a Spectrum staple). The emulator supports all
  joystick types via `--joy-type`.
- ☐ **Lives shown as ship icons** rather than a digit; **score pop-ups**
  at pickups.
- ☐ **HUD polish**: shield/energy bar, zone name (not just number),
  distance-to-next-zone indicator.
- ☐ **Screen-shake / border flash** on hits and explosions (border flash
  is one `OUT` and very period-authentic).

## 5. Content & replayability

- ☐ **Named zones with identity** (Asteroid Belt, Nebula, Inferno,
  Verdant Reach…) each with unique hazards, palette, and music.
- ☐ **Bonus stages** (collect-'em-all crystal runs).
- ☐ **Extra life at score thresholds** + the reward jingle.
- ☐ **Wave/level scripting** so encounters are designed, not purely
  random (a compact byte-code of "spawn type X at height Y after N
  frames").

## 6. Technical & polish

- ☐ **Interrupt-driven (IM2) game clock** for rock-steady 50 Hz and
  glitch-free audio timing.
- ☐ **Double-buffered or beam-synced drawing** to eliminate the last
  flicker (e.g., draw the player last, just behind the raster).
- ☐ **Pre-shifted sprite tables** to speed the blitter if object counts
  grow.
- ☐ **Self-test / cheat keys** behind a build flag for QA.
- ☐ **CI hook**: assemble the game and run a few headless
  `--frames/--screenshot/--wav` smoke tests on every push (the emulator
  makes this trivial — see how the dev shots were captured).
- ☐ **`.tzx` with a custom loader & loading screen** for the authentic
  cassette experience; keep the plain `.tap` for quick loads.

---

## Suggested next three steps

1. **Loading screen + AY title tune (128K).** Biggest nostalgia payoff
   for the effort.
2. **Enemy movement patterns + enemy fire.** Makes the core loop genuinely
   fun and skill-based.
3. **High-score table with initials + power-ups.** The "one more go" hook.
