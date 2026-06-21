# STELLAR DRIFT — Roadmap (Phase 11: new threats)

With ~2 KB still free, Phase 11 adds a **new enemy archetype** to keep the
later zones fresh, reusing the existing object/rock plumbing. Single tape, CI
green on 48K + 128K.

Status: ☐ todo · ◐ partial · ✅ done

---

## A. Splitter rock

- ✅ **Splitter rock (`T_SPLIT`)** — from zone 3 on, ~1 in 4 rocks spawns as a
  **cyan** splitter; shooting it breaks it into **two fast shards**. It reuses
  the rock sprite frames (drift movement and draw fall through the existing
  rock paths) with a distinct ink so it's fair to read. The split is done
  safely: the kill site only records a pending split (position) in memory, and
  `do_splits` spawns the two fast `T_ROCK` shards once per frame *after* the
  bullet loop, so the bullet loop's pointers are never disturbed. _Impact: med
  · Effort: med._
  - Validated by screenshot (cyan splitters + shards) and a 5000-frame
    heavy-split stability run; CI skips to a splitter zone and confirms play
    stays alive there.

---

## Suggested milestones

1. ✅ **"It fights back harder"** — splitter rock (§A). Done and CI-covered.

## Future directions (not scheduled)

- **Secret 7th zone** — higher risk (the seven per-zone tables must stay in
  sync); deferred.
- **More archetypes** — a shielded craft, or a shard that itself splits once
  more.

## Status

Phase 11's milestone (splitter rock) is implemented in `src/game.asm` and
headless-tested. Image ~15.0 KB / 17106-byte ceiling (~2.1 KB free).
