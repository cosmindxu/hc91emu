# STELLAR DRIFT — Roadmap (Phase 3: graphics & UX polish)

Phases 1–2 (`ROADMAP.md`, `ROADMAP2.md`) are complete. Phase 3 is a **game-feel
and presentation pass**: make the cave look like rock, give the big moments
(bosses, deaths, zone clears) the readability and "juice" they deserve, and
tighten the title/pause UX. Everything stays in `src/game.asm`, keeps the
black backdrop, and must keep the headless CI green.

Status: ✅ done (all items)

---

## A. Graphics juice

- ✅ **Textured cave walls** — replace the flat solid-colour wall cells with a
  rocky dither pattern and a brighter inner-edge highlight, so walls read as
  rock with depth.
- ✅ **Sloped wall lips** — taper the inner-most ceiling/floor cell so the
  cave silhouette isn't a hard cell-step.
- ✅ **Boss hit-flash** — flash the boss white for a frame on each hit (today
  it only changes colour at the half-HP phase).
- ✅ **Explosion flash frame** — a white flash kicks off each explosion burst.
- ✅ **Muzzle flash** — a bright spark at the ship's nose when it fires.
- ✅ **Twinkling stars** — the starfield occasionally blinks for life.

## B. Presentation / HUD

- ✅ **Boss HP bar** — during a boss the HUD shows a draining health bar
  instead of just the word "BOSS!!".
- ✅ **Combo on the score pop-up** — the kill pop-up shows the active
  multiplier (e.g. `+5 x3`).
- ✅ **Richer GAME OVER** — show the final score, the zone reached and a
  "NEW HIGH SCORE!" flourish when earned.
- ✅ **ZONE CLEAR flourish** — a banner + bonus call-out when a boss falls,
  before the bonus stage.
- ✅ **Low-health alert** — a soft border pulse and a heartbeat beep when down
  to the last life (honours the photosensitivity option).

## C. Title & pause UX

- ✅ **Title starfield** — sprinkle a star backdrop behind the title text (in
  the margins) so it isn't a bare black screen.
- ✅ **Selection highlight** — bracket the chosen control scheme / skill so
  the current choice is obvious at a glance.
- ✅ **Pause menu** — `H` opens Resume / Restart / Quit-to-title instead of a
  bare frozen screen.

---

## Status

All Phase-3 items are implemented in src/game.asm and headless-tested
(.github/workflows/game.yml). This phase also fixed a severe performance
regression (the cave repaint), lifting the in-game frame rate from ~4fps
to ~15fps, and a boss soft-lock when the object field was full.
