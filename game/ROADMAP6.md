# STELLAR DRIFT — Roadmap (Phase 6: juice, sound & mastery)

Phases 1–5 are complete: a polished six-zone cave-shooter with bosses,
power progression, replay systems, and a story frame. The engine is now
effectively **RAM-full** — the loaded image is ~17 KB and sits ~135 bytes
below the runtime scratch (see the memory map at the top of `src/game.asm`),
so Phase 6 is deliberately about **feel, not new subsystems**: cheap,
high-impact polish that makes wins land harder and gives skilled play a
visible payoff. Everything stays in `src/game.asm`, keeps the black
backdrop, and must keep the headless CI green.

Status: ☐ todo · ◐ partial · ✅ done

---

## A. Sound

- ✅ **Victory fanfare** — the ending plays a dedicated ascending jingle
  instead of the generic zone sweep, so finishing the mission *sounds* like
  an event. _Impact: high · Effort: low._
- ✅ **Boss-defeat fanfare** — every boss that falls plays a short triumphant
  motif over the explosion, distinct from a normal kill. _Impact: med ·
  Effort: low._
- ✅ **Weapon level-up chirp** — leveling an existing weapon plays a rising
  chirp distinct from a fresh pickup. _Done in Phase 8 (`sfx_levelup`)._

## B. Mastery / payoff

- ✅ **End-of-run rank** — the victory screen grades the run **S / A / B / C**
  from the final score, giving skilled players a target to beat. Shown on
  the ending and covered by CI (via the `VTEST` fast-path build). _Impact:
  high · Effort: med._
- ✅ **Rank on GAME OVER** — the same grade on a failed run, scaled by zones
  reached. _Done in Phase 8 (shared `score_rank` + depth bonus)._
- ✅ **Veteran restart (NG+)** — once the mission is beaten, the title offers a
  tougher restart that reuses the existing `difficulty` plumbing. _Done in
  Phase 8 (unlockable 4th skill tier)._

## C. Spectacle

- ✅ **Boss-defeat screen flash** — a colour-cycling border flash on a boss
  kill, on top of the existing screen-shake. _Done in Phase 8 (`do_bflash`)._
- ✅ **Persistent boss name** — show the war-machine's name on the HUD for
  the whole fight, not just the entry taunt. _Done in Phase 8 (`draw_zonename`)._

---

## Suggested milestones

1. **"It sounds like winning"** — victory + boss-defeat fanfares (§A).
2. **"How good was that run?"** — end-of-run rank on the ending (§B).
3. **Later (needs headroom)** — NG+, rank on game over, flash, persistent
   boss name. These wait until the image is given more room (e.g. a second
   tape block loading static data into the free low-RAM gap).

## Status

Milestones 1–2 are implemented in `src/game.asm` and headless-tested
(`.github/workflows/game.yml`). The remaining items are intentionally
deferred: the build guard in `build.sh` enforces the ~135-byte ceiling, and
adding them cleanly first needs the low-RAM relocation work called out
above.
