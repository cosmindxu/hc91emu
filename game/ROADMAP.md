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
- ✅ **Momentum / inertia on the ship.** Velocity-based flight: input
  accelerates `vx`/`vy` (capped) and they decay to rest — tight but with
  a little drift.
- ✅ **Enemy movement patterns.** Enemy craft weave on a sine table while
  drifting left; boss oscillates with doubled amplitude.
- ✅ **Enemy bullets / return fire.** Enemies fire aimed tracers; the boss
  fires a 3-way spread. `do_ebullets` moves and collides them.
- ✅ **Power-ups.** Red pods grant, in sequence, spread shot / rapid fire /
  shield / speed-up; volatile ones are lost on death.
- ✅ **End-of-zone bosses.** A 24-wide multi-hit boss ends each zone;
  destroying it scores a bonus and advances the zone.
- ✅ **Difficulty curve.** Spawn period tightens within a zone (floored)
  and starts lower each zone.
- ✅ **Collision fairness.** Ship-vs-hazard uses a tight 9px box (variable
  `col_thr`), plus invulnerability frames after a hit.

## 2. Audio (huge for immersion)

- ✅ Beeper SFX: shoot, explosion, pickup, zone-change sweep.
- ✅ **Richer beeper effects**: arpeggio pickup, rising power-up chime,
  descending enemy-fire blip, noise-burst explosion, metallic hit tick.
- ✅ **In-game beeper engine drone** — a subtle low pulse every 8th frame
  so play is never silent.
- ✅ **128K AY-3-8912 music** — a looping melody plays on the PSG; the
  register writes are harmless on a 48K machine, so no detection is needed
  (beeper effects still play there).
- ✅ **AY noise channel** for explosions on 128K (noise burst on channel C).

## 3. Colour & graphics

- ✅ Side-on ship sprite; per-object attribute colour; per-zone palette
  and border.
- ✅ **Banded / gradient backdrops per zone** — `attr_row[24]` gives each
  zone three sky/mid/ground colour bands, applied per row by the blitter
  so sprites never bleed across a band.
- ✅ **Parallax starfield in 3 depth layers** — speeds 1–3; the near layer
  draws a 2px dash so it reads brighter.
- ✅ **Sprite animation.** Ship exhaust flicker (two frames), spinning
  asteroids, pulsing crystals.
- ✅ **Explosion animation** — a 3-frame expanding burst plays where
  hazards, the player and the boss are destroyed.
- ✅ **Scrolling foreground terrain** — a character-cell cave ceiling and
  floor scroll left with feed-in tiles, over the banded backdrop.
- ✅ **Anti-clash tricks**: each object class has one reserved ink, the
  banded backdrop keeps paper per row, and bullets stay uncoloured.

## 4. Presentation & UX

- ✅ Title screen + game-over flow with state machine.
- ✅ **Loading screen (`SCR`)** — a drawn title screen is prepended to the
  `.tap`/`.tzx` so it paints in as the game loads (see tools/mkscr.py).
- ✅ **Attractive title screen**: title, high-score table, control menu,
  blinking "PRESS FIRE" and a horizontally scrolling credits line.
- ✅ **High-score table with 3-letter initials entry** — qualify on game
  over, cycle letters with up/down + fire, inserted into the sorted table.
- ✅ **Pause** — 'H' toggles a frozen state with a PAUSED HUD overlay.
- ✅ **Control selection menu**: QAOP or Cursor keys (selectable on the
  title), with the Kempston joystick always live.
- ✅ **Lives shown as ship icons**; **score pop-ups** ("+10/+5/+200/PWR")
  beside the score.
- ✅ **HUD polish**: zone name, distance-to-boss bar, active power-up
  letters, score pop-ups.
- ✅ **Screen-shake / border flash** on hits and explosions (`shake`
  counter flashes the border white for a few frames).

## 5. Content & replayability

- ✅ **Named zones with identity** (Asteroid Belt, Nebula, Inferno,
  Verdant Reach) each with its own palette, border and difficulty.
- ✅ **Bonus stages** — a crystals-only "collect-'em-all" run between
  zones (after each boss), shown as BONUS STAGE in the HUD.
- ✅ **Extra life at score thresholds** (+1 ship every 1000 pts) with a
  "1UP!" pop-up and jingle.
- ✅ **Wave/level scripting** — a looping byte-code of (type, y, delay)
  formations drives spawning, getting denser each zone.

## 6. Technical & polish

- ✅ **Interrupt-driven (IM2) game clock** — a 257-byte vector table at
  0xFE00 routes the 50 Hz interrupt to a light ISR; the main loop HALT-syncs
  to it, bypassing the ROM ISR's keyboard scan.
- ✅ **Beam-synced draw order** — the player ship is drawn last each frame
  (after stars, terrain, hazards, bullets and explosions) so it stays on
  top with minimal flicker.
- ✅ **Pre-shifted sprite tables** — all 16x16 sprite shifts are computed
  once at startup into `psbuf`, so drawing is a plain masked copy (no
  per-row shift loop). With HUD redraw caching, a star mask table and
  trimmed entity counts this roughly doubled the frame rate.
- ✅ **Cheat keys** for QA: hold **I** for invincibility, hold **G** to
  grant all power-ups, press **K** to skip to the next zone.
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
