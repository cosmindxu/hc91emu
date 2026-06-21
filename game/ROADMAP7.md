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

- ✗ **Second tape block for static data** — _superseded._ The user chose to
  stay single-tape, so this was **not pursued**; Phase 8 reclaimed the same
  headroom by LZSS-compressing the sprite data into the existing single image
  (no loader change). The original idea was to split the read-only data into a
  second `CODE` block loaded low (~`0x6300`), shrinking the high image — sound
  but riskier (multi-block loader, stack-vs-load timing) than compression.
- ✅ **Data audit & dedupe** — done in Phase 8: no duplicate string literals;
  removed 80 B of proven-dead data (`music_a`, `ceil_tiles`, `floor_tiles`).

## B. Mastery (spend the headroom)

- ✅ **Veteran restart (NG+)** — done in Phase 8: an unlockable 4th skill tier
  (`won_flag`-gated) with ~25 % faster spawns and +25 % boss HP.
- ✅ **Rank on GAME OVER** — done in Phase 8 (shared `score_rank` + a
  `world*2000` depth bonus; covered by CI).

## C. Spectacle

- ✅ **Boss-defeat screen flash** — done in Phase 8 (`do_bflash`, a
  colour-cycling border flash layered over the shake).
- ✅ **Persistent boss name** — done in Phase 8 (`draw_zonename` shows the
  machine name on HUD row 1 for the whole fight; CI-tested via the BTEST build).

## D. Sound

- ✅ **Weapon level-up chirp** — done in Phase 8 (`sfx_levelup`, triggered by
  a `pw_twin+pw_rapid` snapshot around `grant_power`).

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

## Status

Resolved. The headroom unlock (§A) was delivered single-tape in **Phase 8**
(LZSS sprite compression), so the second-tape plan was deliberately not
pursued. All of §B–§D (NG+, rank on GAME OVER, boss flash, persistent boss
name, level-up chirp) plus the data audit are implemented and validated in
Phase 8 — see `ROADMAP8.md`.
