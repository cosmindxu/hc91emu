# STELLAR DRIFT — Roadmap (Phase 8: single-tape headroom — optimise, don't split)

Phase 7 proposed a second tape block; we're **not doing that** — one `CODE`
block, one load. The deferred Phase-6 features are still blocked by space
(26 bytes free, enforced by the `build.sh` guard), so Phase 8 reclaims room a
different way: **make what's already there smaller**. The loader, the tape
layout and the load flow are all unchanged.

What the image is made of today (org `0x8000`, 17080 bytes):
- ~`0x8000–0xADD4` — game code (~11.7 KB).
- ~`0xADD4–0xC286` — **sprite + mask source, ~5.3 KB** (read *once* at startup
  by `build_preshift`, then never again).
- ~`0xBB24…` strings/tables interleaved at the tail.
- `psbuf` and the runtime scratch are already non-emitted (low-RAM BSS).

Status: ☐ todo · ◐ partial · ✅ done

---

## A. The big reclaim — compress the sprite source (single tape)

The sprite/mask block is the fattest static thing in the image and the
easiest to move: it's consumed *only* by `build_preshift` at startup. Measured
compressibility of the live bytes: **48 % with zlib, 20 % are zero** — a
ZX7-class Z80 depacker realistically lands ~55–60 %.

- ✅ **Pack sprite data + depack at boot** — sprite labels are now EQUs into a
  fixed low-RAM block at `SPRBASE` (0x6300); the bytes live LZSS-packed in
  `sprpack.inc` and `unpack_sprites` depacks them at boot before
  `build_preshift`. The three `dw` tables (`ship_tab`, boss table, `sprtab`)
  resolve through the EQUs, so no draw code changed. **Raw 3360 → packed 1611
  (48 %); net −1651 B in the image** (16830 → 15179). One tape, loader
  untouched. _Impact: high · Effort: high._
  - Tooling: a self-written LZSS packer in `mksprites.py` (with a Python
    round-trip self-check) and a ~60-byte Z80 depacker.
  - CI: `check_unpack.py` boots, snapshots RAM and asserts the depacked block
    is **byte-identical** to the source art — stronger than a screenshot diff.
  - Bonus: the smaller image is also a smaller, faster-loading tape.

## B. Code-size wins (low risk, do first)

- ✅ **Fold the score render** — the 5-digit render ran at 5 sites
  (`call sc_digit` ×20). Collapsed to one `score5` (HL=value → `decbuf`)
  routine. **−120 B.** _Impact: med · Effort: low._
- ☐ **Peephole pass** — the usual Z80 shrinks across 11 KB of hand-written
  code (`xor a` for 0, `ld`-pair fusions, fallthrough instead of `jp`, dedupe
  near-identical blocks). _Variable, likely 200–400 B · Impact: med · Effort:
  med._
- ✅ **Dead-code / unused-symbol sweep** — removed the obsolete runtime-shift
  `draw_sprite` blitter (superseded by the pre-shifted `draw_sprite_ps`,
  **−174 B**) and 5 orphaned strings (**−28 B**). _Impact: low–med · Effort:
  low._

## C. Data-size wins

- ◐ **String/table dedupe** — dead strings removed (see §B sweep); sharing
  repeated words/substrings across the remaining ~30 on-screen strings and
  tightening lookup tables is still open. _~100–300 B · Impact: low · Effort:
  low._
- ☐ **Investigate runtime mask generation** — many masks are derivable from
  the sprite data; generating them at boot could drop a large slice of the
  stored block (on top of, or instead of, §A). _High upside but risky (not all
  masks derive cleanly) — investigate before committing. · Effort: high._

## D. Then spend the headroom

With §B's headroom (348 B free) the cheap deferred items already fit; the
rest wait on more space:
1. ☐ **NG+ / veteran restart** (reuses `difficulty` + a `veteran` flag).
2. ✅ **Rank on GAME OVER** — factored the grader into `score_rank` (shared
   with the victory screen) and graded the failed run by score + a depth
   bonus (`world*2000`, saturating). Covered by CI on the terrain-crash run.
3. ✅ **Weapon level-up chirp** — `sfx_levelup` plays a rising chirp when a
   pickup raises a weapon's level (snapshot of `pw_twin+pw_rapid` around
   `grant_power`); a fresh grant still plays the normal pickup sound.
4. ☐ **Boss-defeat screen flash**, **persistent boss name**.

---

## Suggested milestones

1. ✅ **"Trim the obvious"** — score-render fold + dead-code sweep (§B/§C, all
   low-risk, no tooling). **Done: reclaimed 322 B; headroom 26 → 348 B free.**
2. ✅ **"The big squeeze"** — sprite-data compression (§A). **Done: −1651 B;
   headroom now ~1.9 KB free.** Verified byte-identical in CI.
3. ☐ **"Spend it"** — the deferred features (§D), cheapest/highest-impact
   first. With 348 B free, the cheapest (rank on GAME OVER, level-up chirp)
   already fit without §A.

## Risks & notes

- §A is the only item that adds tooling (packer + depacker); keep the depacker
  tiny and prove output equality with a screenshot diff in CI.
- §B/§C are pure refactors — lean on the existing CI (title, gameplay, boss
  reach, terrain crash, story flows, victory+rank, 128K AY) to catch any
  behavioural drift after each change.
- Order is flexible, but always re-run the full headless suite per change; the
  `build.sh` guard will flag the moment a change crosses the ceiling.
