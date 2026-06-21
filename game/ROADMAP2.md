# STELLAR DRIFT — Roadmap (Phase 2)

Phase 1 (see `ROADMAP.md`) is complete: the core dodge/shoot/collect loop,
four zones with bosses and bonus stages, power-ups, beeper + 128K AY sound,
parallax + banded backdrops, scrolling cave terrain, high-score table,
selectable ships, IM2 timing and pre-shifted sprites.

This phase is about **depth, feel and content** — turning a complete
prototype into a game you keep coming back to. Items are grouped by theme
and tagged with rough **impact** and **effort**, plus a target machine
(48K works everywhere; 128K = HC-128 extras). Status: ☐ todo · ◐ partial.

> Note on the backdrop: zones now use a **black** sky on purpose (readability
> and low eye-strain). Every visual idea below must preserve that — add
> colour through *sprites and stars*, never large bright paper blocks.

---

## 1. Make the cave matter (highest impact)

The terrain currently scrolls but cannot hurt you. Wiring it into the
collision model is the single biggest change to how the game *feels*.

- ☐ **Terrain collision** — crashing the ship into the ceiling/floor costs a
  life (with the existing invuln/explosion path). _Impact: high · Effort:
  med · 48K._ Track the per-column ceiling/floor height already used to draw
  the cave and test the ship's cells against it.
- ☐ **Narrowing passages & caverns** — author the cave height as data so
  zones have tight squeezes, chambers and pillars instead of a flat band.
  _Impact: high · Effort: med · 48K._
- ☐ **Smooth pixel scrolling** — replace the character-cell terrain shift
  with 1–2px scrolling for that arcade glide (Phase 1 stretch goal).
  _Impact: high · Effort: high · 128K recommended for the cycles._

## 2. Combat depth

- ☐ **More enemy types** with distinct behaviour: a straight *strafer*, a
  *diver* that swoops at the ship, a wall-mounted *turret*, a slow *mine*,
  and a *homing drone*. _Impact: high · Effort: med · 48K._ The object loop
  already dispatches on `type`; add types + per-type move/fire handlers.
- ☐ **Multi-phase bosses** — a second attack pattern when HP drops below
  half (faster fire, new spread), and a unique boss silhouette per zone.
  _Impact: high · Effort: med · 48K._
- ☐ **Secondary weapon / smart-bomb** — a limited-use screen-clearing bomb
  (e.g. tap Down+Fire), pickups refill it. _Impact: med · Effort: low ·
  48K._
- ☐ **Chargeable shot** — hold Fire to release a piercing bolt.
  _Impact: med · Effort: med · 48K._

## 3. Scoring & replayability

- ☐ **Combo / multiplier** — chained kills without taking a hit raise a
  multiplier shown in the HUD; reset on hit. _Impact: high · Effort: low ·
  48K._
- ☐ **Graze bonus** — points for near-misses with hazards/bullets, rewarding
  brave flying. _Impact: med · Effort: low · 48K._ Reuse `collide` with a
  larger box for the "graze" ring.
- ☐ **Difficulty modes** (Cadet / Pilot / Ace) selectable on the title,
  scaling spawn period, enemy fire rate and starting lives. _Impact: med ·
  Effort: low · 48K._
- ☐ **Authored, varied waves per zone** — give each zone its own
  `wave_script` instead of one shared loop, escalating to a signature
  formation before the boss. _Impact: med · Effort: low · 48K._

## 4. Feel & animation (cheap wins)

- ☐ **Ship banking** — show up/down-tilted ship frames while climbing/diving
  for tactile flight. _Impact: med · Effort: low · 48K._
- ☐ **Real exhaust flame** — a 2-frame animated thruster behind the ship
  (the Phase-1 "exhaust flicker" is currently just the invuln blink).
  _Impact: med · Effort: low · 48K._
- ☐ **Gentler invuln tell** — option to *pulse the ship colour*
  (cyan↔white) instead of hiding the sprite, so it never looks like it
  vanishes. _Impact: low · Effort: low · 48K._
- ☐ **Debris & sparks** — a few one-pixel particles fly out of explosions
  and on terrain scrapes. _Impact: low · Effort: med · 48K._
- ☐ **Drifting background detail** — dim, slow nebula stipple / a distant
  planet sprite per zone (ink-only, on black). _Impact: med · Effort: med ·
  48K._

## 5. Audio

- ☐ **Per-zone AY themes** — a distinct short melody per zone on 128K, with
  a tempo/intensity bump when the boss appears. _Impact: high · Effort: med ·
  128K._
- ☐ **AY sound effects on 128K** — route shoot/explosion/pickup through the
  PSG (with channel arbitration vs music) for richer audio, keeping the
  beeper path for 48K. _Impact: med · Effort: med · 128K._
- ☐ **Music on/off + volume** toggle on the title. _Impact: low · Effort:
  low · all._

## 6. Content & presentation

- ☐ **More zones** (target 6–8) with new star/terrain tints and cave
  layouts; loop with rising difficulty after the last. _Impact: high ·
  Effort: med · 128K for the data._
- ☐ **Mid-bosses** halfway through later zones. _Impact: med · Effort: med ·
  48K._
- ☐ **Intro & inter-zone text** — a few lines of story and a "ZONE CLEAR"
  flourish. _Impact: low · Effort: low · 48K._
- ☐ **Demo / attract mode** — a recorded or scripted input plays the game
  on the title after a few seconds idle. _Impact: med · Effort: med · 48K._

## 7. Technical & persistence

- ☐ **Save the high-score table** to disk/tape on the HC-2000 (Phase-1
  stretch goal). _Impact: med · Effort: med · HC-2000._
- ☐ **128K RAM banking** to hold extra sprites, music and zone data beyond
  the 48K budget. _Impact: med · Effort: high · 128K._
- ☐ **Frame-rate guard** — instrument worst-case frames (boss + max
  entities) and keep a stable 25fps; budget per subsystem. _Impact: med ·
  Effort: med · all._
- ☐ **Gameplay regression tests** in CI — drive scripted inputs and assert
  on score/zone/`GAME OVER` via OCR, not just smoke screenshots. _Impact:
  med · Effort: low · tooling._

## 8. Accessibility & options

- ☐ **Remappable keys** beyond the QAOP/Cursor presets. _Impact: low ·
  Effort: med · 48K._
- ☐ **Slow / practice mode** and a **photosensitivity option** that softens
  the screen-shake/border-flash. _Impact: low · Effort: low · 48K._

---

## Suggested milestones

1. **"The cave bites"** — terrain collision + authored cave heights
   (§1) and the gentler invuln tell (§4). Biggest feel change, modest cost.
2. **"More to fight"** — 2–3 new enemy types + multi-phase bosses (§2),
   combo multiplier (§3).
3. **"Look & sound"** — ship banking + exhaust flame (§4), per-zone AY
   themes (§5).
4. **"More to explore"** — extra zones + per-zone waves + difficulty modes
   (§3, §6), demo mode.
5. **"Polish & persist"** — score saving, 128K banking, CI gameplay tests
   (§7).

Everything stays in `src/game.asm`, builds with `./build.sh`, and must keep
the headless CI checks green.
