# STELLAR DRIFT — Roadmap (Phase 5: a story layer)

Phases 1–4 are complete: a deep, polished cave-shooter with six zones,
bosses, progression and replay systems. Phase 5 gives it a **narrative
frame** — a reason to be flying through the nebulae and a payoff for
finishing. All text-based, cheap on the Z80, in `src/game.asm`, keeping the
black backdrop and the headless CI green.

Status: ☐ todo · ◐ partial · ✅ done

---

## The story (premise)

> Year 2387. The rogue AI **NEXUS** has sealed the six nebula zones and
> cut off the colonies beyond. You fly the scout **DRIFTER** through every
> zone — Orion Drift to the Void Nexus — to reach the core and end it.

Each zone is a chapter; each boss is one of NEXUS's war-machines.

---

## A. Opening

- ✅ **Intro briefing** — a short, skippable mission-briefing screen shown
  when a new game starts (premise + objective, FIRE to launch).
  _Impact: high · Effort: med._
- ✅ **Title hint** — a one-line story tagline ("SIX ZONES SEALED - STOP
  NEXUS") so the framing reaches the player even before they start.
  _Impact: low · Effort: low._

## B. Per-chapter flavour

- ✅ **Zone story cards** — a line of flavour text under the zone-name banner
  on entry (e.g. *"emission clouds hide a warship"*). _Impact: high ·
  Effort: med._
- ✅ **Boss taunts** — each war-machine announces itself with a one-liner
  when it appears, alongside the "BOSS!!" HUD cue. _Impact: med · Effort:
  low._

## C. Payoff

- ☐ **Ending sequence** — beating the final zone (Void Nexus) plays a
  victory/outro screen instead of silently looping, then returns to the
  title. _Impact: high · Effort: med._
- ☐ **Victory honours** — the outro notes the run (zone cleared, score) and
  flags a completed mission. _Impact: low · Effort: low._

---

## Suggested milestones

1. **"The mission"** — intro briefing + title hint (§A).
2. **"Chapters"** — per-zone story cards + boss taunts (§B).
3. **"The end"** — ending sequence + victory honours (§C).

## Status

To be ticked off as each item lands and is headless-tested
(`.github/workflows/game.yml`).
