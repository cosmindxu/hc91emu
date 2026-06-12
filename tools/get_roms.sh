#!/bin/bash
# Fetch the system ROM dumps into roms/ on demand, so the dumps do not
# have to be redistributed with the source. Every file is verified
# against the sha256 of the dumps this emulator was developed and
# tested with; a wrong download is discarded.
#
#   tools/get_roms.sh            fetch whatever is missing or wrong
#   tools/get_roms.sh --verify   only check what is present (offline)
#
# Sources: the I.C.E. Felix HC family is preserved at
# speccy4ever.speccy.org; the Sinclair 48K ROM (copyright Amstrad plc,
# which permits redistribution for emulation purposes) comes from the
# FUSE repository, with the JSpeccy repository as a fallback.
set -e
cd "$(dirname "$0")/.."
mkdir -p roms

S4E=https://speccy4ever.speccy.org/rom
F48="https://sourceforge.net/p/fuse-emulator/fuse/ci/master/tree/roms/48.rom?format=raw"
J48="https://raw.githubusercontent.com/jsanchezv/JSpeccy/master/src/main/resources/roms/spectrum.rom"

# name  sha256  url  [fallback url]
TABLE="
48.rom      d55daa439b673b0e3f5897f99ac37ecb45f974d1862b4dadb85dec34af99cb42 $F48 $J48
hc91.rom    0902690c7b69871a2791165fb06f699bc676985d867dbaa0d76c1ef89d40a9ca $S4E/HC-91.ROM
hc-85.rom   e3b92f2362b67c8ea5697f1eea89eab6319c5bcb255be3bdaa314f10b602d1dc $S4E/HC-85.ROM
hc-88.rom   73fc1804f9a7c110533141d23e940f1af3a390155d0ccbbdcc254a21358431c4 $S4E/HC-88.ROM
hc-90.rom   b23d93245be833ce9b2e00c7e9f37fad5ae075c1642a01222f5f86fb915a60cf $S4E/HC-90.ROM
hc-128.rom  daa06d5368e64cb36ce6ab211058fb489200884798cfaa57ba00294cb633d4dc $S4E/HC-128.ROM
hc2k1-0.rom fcb2da8daa8e1698b898a13b8baf9556d7fe2c7f2039bd1b7a6033185e0798b8 $S4E/HC2K1-0.ROM
hc2k1-1.rom 4e5a52e98cbe5f9d42bcb2dcdd38cc6095f42dc0fb437be218dacac3da3830fa $S4E/HC2K1-1.ROM
hc2k2-0.rom e3deac5e43d36ff6bc91050c604e8ae94010d7c2c75f1992baad786a39d986fd $S4E/HC2K2-0.ROM
hc2k2-1.rom abc839d7f5c148c1bd6990c6af4ec89174e5bc7adec37c2dd1e1275cc7684e22 $S4E/HC2K2-1.ROM
hc2ki0.rom  40fb91d5a42ca4eb0982966c26144e9136b8c333cc886d864cebcaddd9267ec9 $S4E/HC2KI0.ROM
hc2ki1.rom  d93d799036971fa5960dd6ef3787ba4aa3e97941ab5e5714783df1ec3c8f2475 $S4E/HC2KI1.ROM
"

hash_of() { sha256sum "$1" 2>/dev/null | cut -d' ' -f1; }

ok=0 got=0 bad=0
while read -r name sha u1 u2; do
  [ -n "$name" ] || continue
  f=roms/$name
  if [ "$(hash_of "$f")" = "$sha" ]; then
    ok=$((ok+1))
    continue
  fi
  if [ "${1:-}" = "--verify" ]; then
    echo "missing/bad: $f" >&2
    bad=$((bad+1))
    continue
  fi
  for url in $u1 $u2; do
    echo "fetching $name"
    curl -sfL --max-time 60 "$url" -o "$f.tmp" || continue
    if [ "$(hash_of "$f.tmp")" = "$sha" ]; then
      mv "$f.tmp" "$f"
      break
    fi
    echo "warning: bad checksum from $url" >&2
    rm -f "$f.tmp"
  done
  if [ "$(hash_of "$f")" = "$sha" ]; then
    got=$((got+1))
  else
    echo "error: could not fetch a good $name" >&2
    bad=$((bad+1))
  fi
done <<EOF
$TABLE
EOF
rm -f roms/*.tmp

if [ "$bad" != 0 ]; then
  echo "$((ok+got)) ROMs OK, $bad missing/bad" >&2
  exit 1
fi
echo "$((ok+got)) ROMs OK ($got fetched)"
