# STELLAR DRIFT — Roadmap (Phase 7: headroom, then the deferred polish)

Phase 6 landed the cheap juice (victory + boss-defeat fanfares, end-of-run
rank) but **stopped at the memory wall**: the loaded image is 17080 / 17106
bytes — 26 bytes free — and the build guard in `build.sh` now fails any build
that would push `psbuf` into the stack. Every remaining wishlist item is
blocked on space, not ideas.

So Phase 7 is two parts: **(A) a one-time tech unlock** that reclaims several
KB of image space, then **(B–D) spend it** on the deferred feel/mastery
features. Everything stays in `src/game.asm`, keeps the black backdrop, and
must keep the headless CI green on both 48K and 128K.

Status: ☐ todo · ◐ partial · ✅ done

---

## A. The unlock — do this first

The runtime scratch already lives low (`0x6000–0x62F2`); the gap from
**`0x62F2` to `0x8000` is ~7.4 KB of unused RAM**. Most of that is reachable
for *static, read-only* data if we load it there at tape time.

- ☐ **Second tape block for static data** — split the read-only data (sprite
  source pixels, the long story/credits strings, the zone/boss tables) into a
  second `CODE` block that loads to a fixed low address (≈`0x6300`, ending
  well under the BASIC loader stack near RAMTOP `0x7FFF` — target ~5 KB so
  ~`0x6300–0x7700`). The high image shrinks by that much, restoring real
  headroom. Touches `tools/mktap.py` (emit + load a 2nd block at a given
  org), `build.sh` (assemble the split / adjust the guard), and the memory
  map. _Impact: high (unblocks everything) · Effort: high._
  - CI: the multi-block tap still autoloads, boots and plays on 48K **and**
    128K; verify load timing under `--turbo`.
- ☐ **Data audit & dedupe** — a smaller, independent win: fold duplicated
  strings/sub-strings and tighten tables for a few hundred bytes with no tape
  changes. Good warm-up if the second block slips. _Impact: low–med · Effort:
  low._

## B. Mastery (spend the headroom)

- ☐ **Veteran restart (NG+)** — once the mission is beaten (`won_flag`), the
  title offers a tougher restart that reuses the existing `difficulty`
  plumbing plus a `veteran` flag (faster spawns / +boss HP / fewer pods).
  _Impact: high · Effort: med._
- ☐ **Rank on GAME OVER** — grade a *failed* run S/A/B/C from score scaled by
  the zone reached, reusing the Phase-6 grader. CI-testable via the run that
  already ends in GAME OVER. _Impact: med · Effort: low._

## C. Spectacle

- ☐ **Boss-defeat screen flash** — a brief bright attribute flash on a boss
  kill (save the attr band, flash white ~2 frames, restore), layered over the
  existing screen-shake. _Impact: med · Effort: low._
- ☐ **Persistent boss name** — show the war-machine's name beside the HP bar
  for the whole fight (reuse the `boss_taunts` strings + the change-only HUD
  repaint), not just the entry taunt. CI-testable by OCR during a boss fight.
  _Impact: med · Effort: med._

## D. Sound

- ☐ **Weapon level-up chirp** — a distinct rising chirp when an existing
  weapon *levels up* vs a fresh pickup: a flag out of `grant_power` + a short
  note table fed to the Phase-6 `play_notes`. _Impact: low · Effort: low._

---

## Suggested milestones

1. **"Make room"** — second load block for static data (§A). Nothing else
   ships until this (or the data audit) frees space.
2. **"Play it again, harder"** — NG+ restart + rank on GAME OVER (§B).
3. **"Make it pop"** — boss-defeat flash + persistent boss name + level-up
   chirp (§C, §D).

## Risks & notes

- **Loader stack vs. low load region** — the autoloader does `CLEAR 32767`,
  so its stack sits just under `0x7FFF`; the second block must end with
  margin below that or it corrupts mid-load. Keep the low static block ≤ ~5 KB
  (ending ≤ ~`0x7700`).
- **Two machines** — re-verify 48K and 128K both autoload the multi-block tap
  and the AY path still captures audio.
- **Guard** — update `build.sh` so the size guard accounts for the relocated
  data (two ceilings now: the high image and the low static block).
- Order matters: §A is the gate. Each of §B–§D is independently shippable
  once headroom exists, in any order.
