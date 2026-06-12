#!/bin/bash
# Smoke-load every title in software/library/ through the emulator:
# instant ROM-trap load first, falling back to pulse-level --real-tape
# (covers TZX turbo loaders). *_128 titles run on --machine hc128.
# Success = clean exit + the screen shows real content (>= 5 non-blank
# OCR lines — an unloaded K-cursor screen has 3 incl. the markers).
# Screenshots land in tests/out/library/.
set -u
cd "$(dirname "$0")/.."
EMU=build/hc91emu
OUT=tests/out/library
mkdir -p "$OUT"
pass=0; fail=0

for f in software/library/*/*; do
  base=$(basename "$f"); base=${base%.*}
  genre=$(basename "$(dirname "$f")")
  case "$base" in *_side_*) continue;; esac   # halves of two-sided tapes
  mch=""
  case "$base" in *_128) mch="--machine hc128";; esac

  ok=0
  for mode in "--frames 3500" "--real-tape --frames 12000"; do
    $EMU $mch $mode --autoload --turbo "$f" \
         --screenshot "$OUT/${genre}_${base}.png" --text \
         > "$OUT/${genre}_${base}.txt" 2>&1
    rc=$?
    content=$(grep -c '[^ ]' "$OUT/${genre}_${base}.txt" || true)
    if [ $rc = 0 ] && [ "$content" -ge 5 ]; then
      ok=1
      [ "$mode" = "--frames 3500" ] || base="$base (real-tape)"
      break
    fi
  done
  if [ $ok = 1 ]; then
    echo "  PASS $genre/$base"
    pass=$((pass+1))
  else
    echo "  FAIL $genre/$base"
    fail=$((fail+1))
  fi
done

echo
echo "library verification: $pass loaded, $fail failed"
[ "$fail" = 0 ]
