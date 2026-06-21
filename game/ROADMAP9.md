# STELLAR DRIFT — Roadmap (Phase 9: more to play)

Phases 1–8 delivered a complete, polished game and then reclaimed image space
(single-tape LZSS sprite compression + audits) so there is now ~1.7 KB free
under the `build.sh` guard. Phase 9 spends a little of that on **replayability
content** — new ways to play the same six zones — keeping one tape, the black
backdrop, and the headless CI green on 48K and 128K.

Status: ☐ todo · ◐ partial · ✅ done

---

## A. Boss Rush

- ✅ **Boss Rush mode** — a title toggle (**key 7**, shown as `7-RUSH`, with a
  `** BOSS RUSH ARMED **` banner) that drops the zone exploration: each zone's
  boss spawns almost immediately and they chain back-to-back (no bonus stages)
  from zone 1 through the Void Nexus, ending in the normal victory + rank
  flow. Reuses the whole boss/timer machinery — `bossrush` just shortens
  `world_timer` (and zeroes `bonus_timer`) at each zone start. CI arms it on
  the title and asserts the first boss appears at once. _Impact: high (a fast,
  pure-combat mode) · Effort: low–med._

---

## Suggested milestones

1. ✅ **"Straight to the fight"** — Boss Rush (§A). Done and CI-covered.

## Future directions (not scheduled)

Ideas worth considering for a later phase, each independently shippable on the
remaining headroom (or after the runtime-mask-generation win frees ~0.5 KB
more):

- **Secret 7th zone** — a hidden "Gauntlet" unlocked by clearing the mission
  on Veteran, with its own palette/boss.
- **New weapon tier** — e.g. a piercing beam or homing missiles added to the
  power-up cycle.
- **New enemy archetype** — a splitter asteroid or a shielded craft, slotted
  into the existing wave scripts.
- **Time-attack scoring** — a per-zone clear-time bonus feeding the end rank.

## Status

Phase 9's milestone (Boss Rush) is implemented in `src/game.asm` and
headless-tested. Image: ~15.4 KB / 17106-byte ceiling (~1.7 KB free). The
future-directions list is intentionally unscheduled — open ideas, not
committed work.
