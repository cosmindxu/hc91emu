# STELLAR DRIFT — Roadmap (Phase 12: the seventh zone)

The biggest remaining "future" idea: a **7th zone**. It was flagged risky in
earlier roadmaps because the game is tightly table-driven and several tables
plus clamp sites must all agree, or zone 6 silently reads the wrong data. This
phase does it methodically. Single tape, CI green on 48K + 128K.

Status: ☐ todo · ◐ partial · ✅ done

---

## A. NEXUS CORE (zone 7)

- ✅ **A seventh zone, `NEXUS CORE`** — a new final destination after VOID
  NEXUS, with a hot white/red "core" palette, the fastest spawns, and the
  hardest wave script (`wave6`). It is part of the normal cycle: VOID NEXUS is
  no longer the end (its card now reads "THE OUTER CORE - PUSH ON"); clearing
  NEXUS CORE's boss is the new victory. The boss reuses a silhouette
  (`world & 3`) and gains a name (`NEXUS CORE`) and taunt. _Impact: high ·
  Effort: high (broad, synchronized change)._
  - Synchronized edits (all done): `NZONES` 6→7; one row added to each of
    `worlds_tab`, `zone_names`, `zone_cards`, `boss_taunts`, `boss_names`, and
    `wave_tab` (+`wave6`); the four world-indexed clamps (boss taunt, wave,
    card, boss-name) bumped `cp 6`→`cp 7` / `ld a,5`→`ld a,6`. Non-zone clamps
    (ships, bombs, lives, combo) and the music clamp (zone 6 reuses the final
    theme) were deliberately left alone. Victory (`cp NZONES-1`) and the
    `next_world` wrap (`cp NZONES`) derive from `NZONES` automatically.

---

## Suggested milestones

1. ✅ **"One more zone"** — NEXUS CORE (§A). Done and CI-covered.

## Validation

- Reaching NEXUS CORE (K-skip through the cycle) renders the zone name, card
  and the new palette, plays its wave script, and the GAME OVER screen names
  it — proving all seven tables are in sync and zone 6 is reachable without
  crashing (covered by CI).
- Full regression suite stays green on 48K + 128K (zones 0–5 unaffected,
  sprite depack byte-identical, victory screen via VTEST).
- The victory *trigger* is `cp NZONES-1` (now `cp 6`) — the same mechanism
  that already fired at the old final zone, just one index higher; the victory
  screen/flow itself is exercised by the VTEST CI build. Killing the final
  boss to see the ending in-engine needs aim that isn't reliably scriptable
  headless, so that last hop is covered by construction + the VTEST screen
  test rather than an end-to-end playthrough assertion.

## Status

Phase 12's milestone (NEXUS CORE) is implemented in `src/game.asm` and
headless-tested. Image ~15.1 KB / 17106-byte ceiling (~2.0 KB free).
