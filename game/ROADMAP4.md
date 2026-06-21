# STELLAR DRIFT — Roadmap (Phase 4: depth & replayability)

Phases 1–3 are complete and the engine now runs at a playable frame rate.
Phase 4 is about **longevity**: systems that reward repeated play, more
variety in the fights, environmental drama, and a reason to keep going after
a death. Everything stays in `src/game.asm`, keeps the black backdrop, and
must keep the headless CI green.

Status: ☐ todo · ◐ partial · ✅ done

---

## A. Power & progression

- ☐ **Weapon levels** — collecting a power-up you already have *levels it up*
  instead of just re-granting it: spread → wider fan, rapid → faster, with the
  level shown beside the letter in the HUD (e.g. `R2`). _Impact: high · Effort:
  med._
- ☐ **Overdrive meter** — a meter fills as you destroy hazards; when full,
  press a key to trigger a few seconds of rapid fire + invulnerability, then it
  empties. Shown as a HUD bar. _Impact: high · Effort: med._
- ☐ **Bomb pickups** — some pods now grant a smart-bomb instead of always a
  power-up, so the bomb count is earned, not fixed. _Impact: med · Effort: low._

## B. Boss & enemy variety

- ☐ **Boss attack patterns** — add a **radial burst** (ring of bullets) to the
  boss's repertoire, telegraphed by a charge-up flash, alternating with the
  existing aimed spread. _Impact: high · Effort: med._
- ☐ **Formation wave** — a squadron that enters in a tight **V** and peels off,
  for set-piece moments between the looping waves. _Impact: med · Effort: med._
- ☐ **Armoured enemy** — a tougher craft that takes several hits and flashes on
  each, reusing the mini-boss hit model. _Impact: med · Effort: low._

## C. World & atmosphere

- ☐ **Laser gates** — pulsing energy barriers in the cave that switch on/off on
  a timer; passing through while lit costs a life, so you time your run.
  _Impact: high · Effort: med._
- ☐ **Meteor-shower event** — a periodic, telegraphed burst of dense asteroids
  ("METEORS!"), a brief spike in intensity mid-zone. _Impact: med · Effort:
  low._

## D. Flow & meta

- ☐ **Continue system** — after GAME OVER, offer one **CONTINUE?** to resume at
  the current zone (score reset or halved), before the high-score flow.
  _Impact: high · Effort: med._
- ☐ **Run stats on GAME OVER** — show hazards destroyed, bombs used and the
  best zone reached this run. _Impact: med · Effort: low._
- ☐ **Dynamic boss music** — switch to a tense, faster AY theme while a boss is
  on screen, back to the zone theme afterwards. _Impact: med · Effort: low._

---

## Suggested milestones

1. **"Get stronger"** — weapon levels + overdrive meter + bomb pickups (§A).
2. **"Tougher fights"** — boss radial pattern + armoured enemy + formation wave
   (§B).
3. **"The cave fights back"** — laser gates + meteor-shower event (§C).
4. **"Keep playing"** — continue system + run stats + dynamic boss music (§D).

## Status

To be ticked off as each item lands and is headless-tested
(`.github/workflows/game.yml`).
