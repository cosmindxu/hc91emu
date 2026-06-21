# STELLAR DRIFT — Roadmap (Phase 10: skill scoring)

With ~2.3 KB free after the Phase 8 audits and the runtime-mask-generation
win, Phase 10 spends a little on a **skill-reward scoring** layer — giving
good players more reason to play clean, and feeding the existing S/A/B/C rank.
Single tape, CI green on 48K + 128K.

Status: ☐ todo · ◐ partial · ✅ done

---

## A. Perfect-zone bonus

- ✅ **Perfect zone** — clearing a zone (killing its boss) **without losing a
  life** awards a +500 bonus and shows a `PERFECT ZONE +500` flourish in place
  of the usual `ZONE CLEAR!`. A `zone_hit` flag is cleared at each zone start
  (`reset_run` / `next_world`) and set on any ship death; `kill_boss` checks it
  for the bonus and popup. The extra score flows straight into the end-of-run
  rank, so a flawless run grades higher. CI kills the first boss untouched
  (BTEST build) and asserts the popup. _Impact: med · Effort: low._

---

## Suggested milestones

1. ✅ **"Play it clean"** — perfect-zone bonus (§A). Done and CI-covered.

## Future directions (not scheduled)

Still-open ideas from earlier phases, each independently shippable on the
remaining headroom:

- **Secret 7th zone** — a hidden destination; higher risk as the six zone
  tables (worlds/names/cards/taunts/boss-sprites/boss-names/waves) must stay
  in sync.
- **New enemy archetype** — e.g. a splitter rock (reuse the rock sprite) that
  breaks into fragments when shot.
- **Combo/grade flourish** — surface the live combo multiplier or a per-zone
  letter grade.

## Status

Phase 10's milestone (perfect-zone bonus) is implemented in `src/game.asm` and
headless-tested. Image ~14.9 KB / 17106-byte ceiling (~2.2 KB free).
