#!/bin/bash
# Download a representative subset of the SingleStepTests/z80 vectors
# (https://github.com/SingleStepTests/z80, MIT) into tests/vectors/.
# ~160 files x 1000 tests x ~600 KB; not committed (size), so the suite
# section that uses them is skipped unless this has been run.
set -e
cd "$(dirname "$0")/.."
DEST=tests/vectors
BASE="https://raw.githubusercontent.com/SingleStepTests/z80/main/v1"
mkdir -p "$DEST"

FILES=(
  # base opcodes: loads, ALU (register/immediate/(HL)), 16-bit arith,
  # rotates/DAA/SCF/CCF (Q-sensitive), jumps/calls/stack, I/O, HALT
  00 01 02 04 05 06 07 08 09 0a 0f 10 11 16 17 18 19 1f 20 21 22 27 28
  2a 2f 30 31 32 34 35 36 37 38 3a 3e 3f 41 46 70 76 7e 80 86 8e 96 9e
  a6 ae b6 be c1 c3 c4 c5 c9 cd ce d3 d6 d9 db e3 e9 eb f1 f5 f9 fb fe ff
  # CB: rotates/shifts (incl. SLL), BIT/RES/SET incl. (HL) WZ behavior
  "cb 00" "cb 06" "cb 0e" "cb 16" "cb 1e" "cb 26" "cb 2e" "cb 36" "cb 3e"
  "cb 46" "cb 7e" "cb c6" "cb fe"
  # ED: 16-bit ADC/SBC, LD (nn),rr, NEG/RETN/RETI/IM, LD A,I / RRD/RLD,
  # IN/OUT (C), and the full block-op family
  "ed 42" "ed 43" "ed 44" "ed 45" "ed 46" "ed 47" "ed 4a" "ed 4b" "ed 4d"
  "ed 4f" "ed 56" "ed 57" "ed 5e" "ed 5f" "ed 67" "ed 6f" "ed 70" "ed 71"
  "ed 78" "ed 79"
  "ed a0" "ed a1" "ed a2" "ed a3" "ed a8" "ed a9" "ed aa" "ed ab"
  "ed b0" "ed b1" "ed b2" "ed b3" "ed b8" "ed b9" "ed ba" "ed bb"
  # DD/FD: IX/IY arithmetic, IXH/IXL halves, (IX+d) read-modify-write
  "dd 09" "dd 19" "dd 21" "dd 22" "dd 23" "dd 24" "dd 26" "dd 29" "dd 2a"
  "dd 34" "dd 35" "dd 36" "dd 46" "dd 66" "dd 70" "dd 77" "dd 7e" "dd 86"
  "dd 96" "dd be" "dd e1" "dd e3" "dd e5" "dd e9" "dd f9"
  "fd 21" "fd 34" "fd 36" "fd 66" "fd 77" "fd be"
  # DDCB/FDCB: shifts, BIT (WZ-derived X/Y), RES/SET incl. result-copy
  "dd cb __ 00" "dd cb __ 06" "dd cb __ 16" "dd cb __ 26" "dd cb __ 46"
  "dd cb __ 7e" "dd cb __ 86" "dd cb __ c6" "dd cb __ ce"
  "fd cb __ 06" "fd cb __ 4e" "fd cb __ fe"
)

n=0
for f in "${FILES[@]}"; do
  out="$DEST/$f.json"
  [ -s "$out" ] && continue
  url="$BASE/${f// /%20}.json"
  echo "fetch $f.json"
  if ! curl -sfL --retry 3 --max-time 120 "$url" -o "$out"; then
    echo "FAILED: $url" >&2
    rm -f "$out"
    exit 1
  fi
  n=$((n+1))
done
echo "vectors ready: $(ls "$DEST" | wc -l) files ($n new)"
