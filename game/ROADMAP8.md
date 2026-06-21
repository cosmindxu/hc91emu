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

- ☐ **Pack sprite data + depack at boot** — store the block compressed in the
  image; at startup decompress it into the free low-RAM gap (`0x62F2–0x8000`,
  7.4 KB free; the 5.3 KB fits), then point `build_preshift` at the unpacked
  copy. Needs a ~70–150-byte Z80 depacker and a Python packer in `tools/`
  (ZX7 has both freely; or a compact LZSS). **Net headroom ≈ 1.5–2 KB**, one
  tape, loader untouched. _Impact: high · Effort: high._
  - CI: the existing gameplay screenshot must stay **pixel-identical** before
    vs. after (proves the unpacked sprites match), plus 48K + 128K still boot.
  - Bonus: a smaller image also means a **smaller, faster-loading tape**.

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
2. ☐ **"The big squeeze"** — sprite-data compression (§A) for the ~2 KB. The
   one item that needs new tooling and careful verification.
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
