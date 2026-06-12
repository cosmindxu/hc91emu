#!/bin/bash
# Build the local classic-software library under software/library/,
# classified by genre, from the World of Spectrum file archive
# (worldofspectrum.net/pub). Period software is copyrighted: nothing
# under software/ is committed (see .gitignore) — this script makes the
# library reproducible instead. Re-runs only fetch what is missing.
#
# usage: tools/get_library.sh
set -u
cd "$(dirname "$0")/.."
BASE="https://worldofspectrum.net/pub/sinclair/games"
LIB="software/library"
ok=0; fail=0; have=0
failures=""

fetch() { # fetch <genre> <name> <url-candidate...>
  local genre="$1" out="$2"; shift 2
  local dest="$LIB/$genre" url tmp inner ext
  mkdir -p "$dest"
  if ls "$dest/$out".* >/dev/null 2>&1; then
    have=$((have+1))
    return 0
  fi
  for url in "$@"; do
    tmp=$(mktemp)
    if curl -sfL --retry 2 --max-time 120 "$BASE/$url" -o "$tmp"; then
      inner=$(unzip -Z1 "$tmp" 2>/dev/null \
              | grep -iE '\.(tap|tzx|z80|sna)$' | head -1)
      if [ -n "$inner" ]; then
        ext=$(echo "${inner##*.}" | tr '[:upper:]' '[:lower:]')
        if unzip -p "$tmp" "$inner" > "$dest/$out.$ext"; then
          rm -f "$tmp"
          echo "  OK   $genre/$out.$ext"
          ok=$((ok+1))
          return 0
        fi
      fi
    fi
    rm -f "$tmp"
  done
  echo "  FAIL $genre/$out" >&2
  failures="$failures $genre/$out"
  fail=$((fail+1))
  return 1
}

echo "fetching library into $LIB ..."

# NB: Ultimate Play The Game titles (Jetpac, Pssst, Sabre Wulf, Atic
# Atac, Knight Lore, Alien 8) and R-Type are distribution-denied in the
# WoS archive and are NOT fetched; equally classic substitutes are used.
# *_128 entries carry AY music — load them with --machine hc128.

# ---- platform ----
fetch platform manic_miner       m/ManicMiner.tap.zip
fetch platform jet_set_willy     j/JetSetWilly.tap.zip
fetch platform chuckie_egg       c/ChuckieEgg.tap.zip
fetch platform dynamite_dan      d/DynamiteDan.tap.zip
fetch platform monty_on_the_run  m/MontyOnTheRun.tap.zip
fetch platform technician_ted    t/TechnicianTed.tzx.zip

# ---- arcade ----
fetch arcade arkanoid            a/Arkanoid.tap.zip
fetch arcade bomb_jack           b/BombJack.tap.zip
fetch arcade skool_daze          s/SkoolDaze.tap.zip
fetch arcade thrust              t/Thrust.tap.zip
fetch arcade deathchase          d/Deathchase.tap.zip
fetch arcade starquake           s/Starquake.tap.zip
fetch arcade batty               b/Batty.tap.zip

# ---- isometric ----
fetch isometric head_over_heels  h/HeadOverHeels.tap.zip
fetch isometric batman           b/Batman.tap.zip
fetch isometric ant_attack       a/AntAttack.tap.zip
fetch isometric fairlight        f/Fairlight48.tap.zip
fetch isometric sweevos_world    s/SweevosWorld.tap.zip

# ---- shooter ----
fetch shooter exolon             e/Exolon.tap.zip
fetch shooter zynaps             z/Zynaps.tap.zip
fetch shooter cybernoid          c/Cybernoid48.tap.zip
fetch shooter cybernoid_128      c/Cybernoid128.tap.zip
fetch shooter uridium            u/Uridium.tap.zip
fetch shooter light_force        l/LightForce.tap.zip
fetch shooter quazatron          q/Quazatron.tap.zip
fetch shooter harrier_attack     h/HarrierAttack.tap.zip
fetch shooter chronos            c/Chronos.tap.zip
fetch shooter p47_thunderbolt    p/P-47Thunderbolt.tap.zip

# ---- puzzle ----
fetch puzzle boulder_dash        b/BoulderDash.tap.zip
fetch puzzle tetris              t/Tetris.tzx.zip
fetch puzzle tetris_128          t/Tetris128.tap.zip

# ---- adventure ----
fetch adventure the_hobbit       h/HobbitTheV1.2.tap.zip
fetch adventure lords_of_midnight l/LordsOfMidnightThe.tap.zip

# ---- sports ----
fetch sports match_day_2         m/MatchDayII.tap.zip m/MatchDay2.tap.zip
fetch sports daley_decathlon     d/DaleyThompsonsDecathlon.tap.zip
fetch sports chequered_flag      c/ChequeredFlag.tap.zip

# ---- two-sided originals (separate cassette sides for the in-game
# tape-swap flow: --tape-b / F8) ----
if ! ls "$LIB/shooter/p47_side_a.tzx" >/dev/null 2>&1; then
  tmp=$(mktemp)
  if curl -sfL --retry 2 --max-time 120 \
       "$BASE/p/P-47Thunderbolt.tzx.zip" -o "$tmp"; then
    unzip -p "$tmp" "P-47 Thunderbolt - Side A.tzx" \
        > "$LIB/shooter/p47_side_a.tzx" &&
    unzip -p "$tmp" "P-47 Thunderbolt - Side B.tzx" \
        > "$LIB/shooter/p47_side_b.tzx" &&
    echo "  OK   shooter/p47_side_a.tzx + p47_side_b.tzx" &&
    ok=$((ok+2))
  fi
  rm -f "$tmp"
else
  have=$((have+1))
fi

echo
echo "library: $ok fetched, $have already present, $fail failed"
[ -n "$failures" ] && echo "failed:$failures"
exit 0
