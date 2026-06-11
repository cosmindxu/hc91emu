#!/bin/bash
# HC-91 emulator test suite. Run from the project root (or via make test).
# RUN_ZEXALL=1 adds the slow zexall undocumented-flags exerciser.
set -u
cd "$(dirname "$0")/.."

EMU=build/hc91emu
ZEXRUN=build/zexrun
OUT=tests/out
mkdir -p "$OUT"
pass=0; fail=0

check() { # check <name> <condition-result>
  if [ "$1" = 0 ]; then echo "  PASS: $2"; pass=$((pass+1));
  else echo "  FAIL: $2"; fail=$((fail+1)); fi
}

echo "== 0. Unit tests: instruction timing + contention M-cycles =="
build/ttest; check $? "ttest: official T-state totals (uncontended)"
build/ctest; check $? "ctest: per-M-cycle contention patterns"
build/dtest; check $? "dtest: disassembler (all prefixes, undocumented)"

echo "== 1. Z80 CPU core: zexdoc instruction exerciser =="
$ZEXRUN tests/zexdoc.com > "$OUT/zexdoc.log" 2>&1
errs=$(grep -c "ERROR" "$OUT/zexdoc.log" || true)
oks=$(grep -c "OK" "$OUT/zexdoc.log" || true)
echo "  zexdoc: $oks OK lines, $errs error lines"
[ "$errs" = 0 ] && [ "$oks" -ge 60 ]; check $? "zexdoc all tests OK"

if [ "${RUN_ZEXALL:-0}" = 1 ]; then
  echo "== 1b. Z80 CPU core: zexall (slow) =="
  $ZEXRUN tests/zexall.com > "$OUT/zexall.log" 2>&1
  errs=$(grep -c "ERROR" "$OUT/zexall.log" || true)
  oks=$(grep -c "OK" "$OUT/zexall.log" || true)
  echo "  zexall: $oks OK lines, $errs error lines"
  [ "$errs" = 0 ] && [ "$oks" -ge 60 ]; check $? "zexall all tests OK"
fi

echo "== 2. ROM boot: HC-91 banner =="
$EMU --rom roms/hc91.rom --frames 250 --text --screenshot "$OUT/boot_hc91.png" > "$OUT/boot_hc91.txt" 2>&1
grep -q "HC - 91" "$OUT/boot_hc91.txt"; check $? "HC-91 boot banner shows 'HC - 91'"
grep -q "I.C.E. FELIX" "$OUT/boot_hc91.txt"; check $? "HC-91 boot banner shows 'I.C.E. FELIX'"

echo "== 2b. ROM boot: Sinclair 48K =="
$EMU --rom roms/48.rom --frames 250 --text > "$OUT/boot_48.txt" 2>&1
grep -q "1982 Sinclair Research" "$OUT/boot_48.txt"; check $? "48K ROM boots to Sinclair banner"

echo "== 3. BASIC: PRINT 2+3*4 =="
$EMU --rom roms/hc91.rom --frames 450 --type 'p2+3*4\n@260' --text \
     --screenshot "$OUT/basic.png" > "$OUT/basic.txt" 2>&1
grep -q "^14" "$OUT/basic.txt" || grep -q " 14" "$OUT/basic.txt"; check $? "BASIC PRINT 2+3*4 = 14"
grep -q "0 OK" "$OUT/basic.txt"; check $? "BASIC reports '0 OK'"

echo "== 4. Software (auto-detected in software/) =="
shopt -s nullglob
for f in software/*.tap software/*.z80 software/*.sna; do
  base=$(basename "$f"); name=$(echo "$base" | tr '.' '_')
  # arkanoid.z80 has its own dedicated floating-bus test below
  [ "$base" = "arkanoid.z80" ] && continue
  case "$f" in
    *.tap) args="--autoload --frames 3000";;
    *)     args="--frames 600";;
  esac
  $EMU --rom roms/hc91.rom $args --screenshot "$OUT/sw_$name.png" --text "$f" \
       > "$OUT/sw_$name.txt" 2>&1
  rc=$?
  # success criteria: emulator exited 0 and the screen shows real content:
  # at least 5 lines containing non-space characters (markers contribute 2;
  # an unloaded K-cursor screen has only 3)
  content=$(grep -c '[^ ]' "$OUT/sw_$name.txt" || true)
  [ $rc = 0 ] && [ "$content" -ge 5 ]; check $? "runs $base (screenshot: tests/out/sw_$name.png)"
done

echo "== 5. Beeper audio: BASIC BEEP 1,0 =="
# EXTEND mode (CAPS+SYM) then SYM+Z types the BEEP keyword.
$EMU --rom roms/hc91.rom --frames 480 --keys '260:CAPS+SYM' --keys '272:SYM+Z' \
     --type '1,0\n@284' --wav "$OUT/beep.wav" > /dev/null 2>&1
python3 tests/check_beep.py "$OUT/beep.wav"
check $? "BEEP 1,0 renders ~1s of ~261.6 Hz (middle C)"

if [ -f software/arkanoid.z80 ]; then
  echo "== 6. Floating bus (Arkanoid beam-sync) =="
  # Arkanoid polls an unattached port (floating bus) to sync sprites with
  # the beam; without it the game freezes at round start. Recipe: answer
  # the Kempston prompt, then hold SPACE ~70 frames to start round 1.
  AK="--type n@100"
  for f in $(seq 200 5 265); do AK="$AK --type \\ @$f"; done
  eval $EMU --rom roms/hc91.rom --frames 1600 $AK \
       --screenshot "$OUT/ark_fb_a.png" software/arkanoid.z80 > /dev/null 2>&1
  eval $EMU --rom roms/hc91.rom --frames 2000 $AK \
       --screenshot "$OUT/ark_fb_b.png" software/arkanoid.z80 > /dev/null 2>&1
  eval $EMU --rom roms/hc91.rom --frames 1600 $AK --no-floating-bus \
       --screenshot "$OUT/ark_nofb_a.png" software/arkanoid.z80 > /dev/null 2>&1
  eval $EMU --rom roms/hc91.rom --frames 2000 $AK --no-floating-bus \
       --screenshot "$OUT/ark_nofb_b.png" software/arkanoid.z80 > /dev/null 2>&1
  fa=$(md5sum < "$OUT/ark_fb_a.png");   fb=$(md5sum < "$OUT/ark_fb_b.png")
  na=$(md5sum < "$OUT/ark_nofb_a.png"); nb=$(md5sum < "$OUT/ark_nofb_b.png")
  [ "$fa" != "$fb" ]; check $? "Arkanoid gameplay progresses with floating bus"
  [ "$na" = "$nb" ];  check $? "Arkanoid freezes without floating bus (sanity)"
fi

if [ -f software/z80test/z80ccfscr.tap ]; then
  echo "== 7. CCF/Q + contention: z80ccfscr golden pattern =="
  # z80ccfscr fills the screen via POP AF/CCF — the pattern is a fingerprint
  # of the Q-register and SCF/CCF X/Y behavior (Zilog NMOS). Golden frozen
  # from the core that passes z80ccf 160/160.
  $EMU --rom roms/hc91.rom software/z80test/z80ccfscr.tap --autoload --turbo \
       --frames 2500 --screenshot "$OUT/ccfscr.png" > /dev/null 2>&1
  [ "$(md5sum < "$OUT/ccfscr.png")" = "$(md5sum < tests/ccfscr_golden.png)" ]
  check $? "z80ccfscr pattern matches golden (Q/CCF fingerprint)"
fi

echo "== 7b. Beam renderer: mid-frame border stripes =="
# A tight OUT-(0xFE) loop (~31 T/color) paints diagonal rainbow border
# stripes only if rendering follows the beam.
build/bordertap "$OUT/border.tap"
$EMU --rom roms/hc91.rom "$OUT/border.tap" --autoload --turbo --frames 400 \
     --fb-dump "$OUT/border.fb" --screenshot "$OUT/border.png" > /dev/null 2>&1
build/fbcheck "$OUT/border.fb" | sed 's/^/  /'
[ "${PIPESTATUS[0]}" = 0 ]; check $? "beam render shows border rainbow stripes"

echo "== 7c. Beam renderer: multicolour (per-scanline attr writes) =="
# A HALT-synced loop rewrites one attr cell every ~226 T while the beam
# crosses it; the cell must show >=3 paper colors in its 8 pixel rows.
build/multitap "$OUT/multi.tap"
$EMU --rom roms/hc91.rom "$OUT/multi.tap" --autoload --turbo --frames 400 \
     --fb-dump "$OUT/multi.fb" --screenshot "$OUT/multi.png" > /dev/null 2>&1
build/fbcheck "$OUT/multi.fb" --band 88 33 3 | sed 's/^/  /'
[ "${PIPESTATUS[0]}" = 0 ]; check $? "multicolour: one attr cell shows >=3 colors"

echo "== 9. Snapshot saving: .sna/.z80 round-trip + .scr =="
# Save the running multicolour engine, resume from each snapshot format:
# the engine only paints bands if PC/SP/registers/IFF/IM survive intact.
$EMU --rom roms/hc91.rom "$OUT/multi.tap" --autoload --turbo --frames 400 \
     --save-sna "$OUT/rt.sna" --save-z80 "$OUT/rt.z80" \
     --save-scr "$OUT/rt.scr" > /dev/null 2>&1
$EMU --rom roms/hc91.rom "$OUT/rt.sna" --turbo --frames 100 \
     --fb-dump "$OUT/rt_sna.fb" > /dev/null 2>&1
build/fbcheck "$OUT/rt_sna.fb" --band 88 33 3 > /dev/null
check $? ".sna save/load resumes the multicolour engine"
$EMU --rom roms/hc91.rom "$OUT/rt.z80" --turbo --frames 100 \
     --fb-dump "$OUT/rt_z80.fb" > /dev/null 2>&1
build/fbcheck "$OUT/rt_z80.fb" --band 88 33 3 > /dev/null
check $? ".z80 save/load resumes the multicolour engine"
$EMU --rom roms/hc91.rom "$OUT/rt.scr" --frames 10 --text 2>/dev/null \
  | grep -q "Bytes: multi"
check $? ".scr export/import shows the saved screen"

echo "== 10. Pulse-level tape + TZX + SAVE =="
# (a) the multicolour tap loaded through the REAL ROM loader, no trap:
# pilot/sync/bit pulses on the EAR line with cycle timing.
$EMU --rom roms/hc91.rom "$OUT/multi.tap" --autoload --real-tape --turbo \
     --frames 1500 --fb-dump "$OUT/real.fb" > /dev/null 2>&1
build/fbcheck "$OUT/real.fb" --band 88 33 3 > /dev/null
check $? "real-tape: ROM loads .tap from pulses"
# (b) same content as TZX (turbo 0x11 blocks + tone/pulse/loop/info blocks)
build/tap2tzx "$OUT/multi.tap" "$OUT/multi.tzx"
$EMU --rom roms/hc91.rom "$OUT/multi.tzx" --autoload --real-tape --turbo \
     --frames 1500 --fb-dump "$OUT/tzx.fb" > /dev/null 2>&1
build/fbcheck "$OUT/tzx.fb" --band 88 33 3 > /dev/null
check $? "real-tape: ROM loads .tzx (turbo blocks, tone, loop)"
# (c) the same TZX through the instant trap (data blocks extracted)
$EMU --rom roms/hc91.rom "$OUT/multi.tzx" --autoload --turbo --frames 400 \
     --fb-dump "$OUT/tzxtrap.fb" > /dev/null 2>&1
build/fbcheck "$OUT/tzxtrap.fb" --band 88 33 3 > /dev/null
check $? "trap load also works for .tzx"
# (d) SAVE "x" CODE 16384,10 -> SA-BYTES trap -> byte-exact .tap
$EMU --rom roms/hc91.rom --frames 700 --save-tape "$OUT/saved.tap" \
     --type 's"x"@260' --keys '320:CAPS+SYM' --type 'i16384,10\n@334' \
     --type ' @520' > /dev/null 2>&1
[ "$(xxd -p "$OUT/saved.tap" | tr -d '\n')" = \
  "13000003782020202020202020200a0000400080910c00ff00000000000000000000ff" ]
check $? "SAVE captures byte-exact header+data .tap"

echo "== 11. Joysticks (Kempston port, Sinclair/cursor key aliases) =="
# PRINT IN 31 with Kempston attached: R+F held = 17, idle = 0.
JOYIN="--type p@260 --keys 272:CAPS+SYM --keys 284:SYM+I --type 31\\n@296"
$EMU --rom roms/hc91.rom --frames 480 --kempston --joy '300-480:R+F' \
     $JOYIN --text 2>/dev/null | grep -q "^17"
check $? "Kempston: IN 31 reads 17 while R+F held"
$EMU --rom roms/hc91.rom --frames 480 --kempston $JOYIN --text 2>/dev/null \
  | grep -q "^0 "
check $? "Kempston: IN 31 reads 0 when idle"
$EMU --rom roms/hc91.rom --frames 420 --joy-type sinclair --joy '260-400:F' \
     --text 2>/dev/null | grep -q "000"
check $? "Sinclair joystick fire = key 0"
$EMU --rom roms/hc91.rom --frames 420 --joy-type cursor --joy '260-400:U' \
     --text 2>/dev/null | grep -q "777"
check $? "Cursor joystick up = key 7"

echo "== 12. HC-91 CP/M mode: port 0x7E RAM paging =="
# Generated code pages RAM over ROM, round-trips a marker through the
# low bank, reads a ROM byte while paged out, then executes at PC=0.
build/cpmtap "$OUT/cpm.tap"
$EMU --rom roms/hc91.rom "$OUT/cpm.tap" --autoload --turbo --frames 400 \
     --save-scr "$OUT/cpm.scr" --fb-dump "$OUT/cpm.fb" > /dev/null 2>&1
[ "$(xxd -l 2 -p "$OUT/cpm.scr")" = "420d" ]
check $? "0x7E paging: CP/M RAM read/write + ROM page-out readback"
[ "$(xxd -l 4 -p "$OUT/cpm.fb")" = "d70000ff" ]
check $? "0x7E paging: code executes at PC=0 in paged RAM"
# The genuine ROM bootstrap (RANDOMIZE USR 14446) must land at 0 and park.
$EMU --rom roms/hc91.rom --frames 500 --type 't@260' --keys '272:CAPS+SYM' \
     --type 'l14446\n@284' --trace-frames 2>&1 >/dev/null \
  | tail -1 | grep -q "PC=0001"
check $? "genuine ROM CP/M stub (USR 14446) lands in paged RAM"

echo "== 13. Debugger: breakpoints, stepping, watchpoints, trace =="
# (a) breakpoint at the ROM init entry (boot does DI;XOR;LD DE;JP = 28 T),
# scripted regs/dis/mem/step session.
$EMU --rom roms/hc91.rom --frames 3 --break 11CB \
     --debug "regs;dis pc 4;mem 0000 16;step 3;regs;cont" \
     > "$OUT/dbg_break.txt" 2>&1
grep -q 'breakpoint at \$11CB' "$OUT/dbg_break.txt" \
  && grep -q '^PC=11CB AF=0044' "$OUT/dbg_break.txt" \
  && grep -q '^11CC: .*LD A,\$07' "$OUT/dbg_break.txt" \
  && grep -q '^0000: F3 AF 11 FF FF C3 CB 11' "$OUT/dbg_break.txt"
check $? "monitor: breakpoint + regs + dis + mem on ROM boot"
grep -q '^PC=11D0 AF=0744' "$OUT/dbg_break.txt"
check $? "monitor: step 3 lands at \$11D0 with A=07"
# (b) memory write watch: the ROM RAM-test sweep hits 0x5C78; 'q' on the
# third stop ends the run early.
$EMU --rom roms/hc91.rom --frames 200 --watch 5C78 --debug "c;c;q" \
     > "$OUT/dbg_watch.txt" 2>&1
[ "$(grep -c 'watch: write .* to \$5C78' "$OUT/dbg_watch.txt")" = 3 ]
check $? "monitor: write watchpoint fires (and quit stops the run)"
# (c) I/O port watch: boot border OUT (FE),A with A=7.
$EMU --rom roms/hc91.rom --frames 5 --pwatch FE --debug "q" \
     > "$OUT/dbg_pwatch.txt" 2>&1
grep -q 'watch: OUT port \$07FE value \$07' "$OUT/dbg_pwatch.txt"
check $? "monitor: port watchpoint catches boot border OUT"
# (d) per-instruction trace of the boot path.
$EMU --rom roms/hc91.rom --frames 2 --trace "$OUT/dbg_trace.txt" \
     > /dev/null 2>&1
head -1 "$OUT/dbg_trace.txt" | grep -q '^0000: DI' \
  && grep -q '^11CB: LD B,A' "$OUT/dbg_trace.txt" \
  && [ "$(wc -l < "$OUT/dbg_trace.txt")" -gt 5000 ]
check $? "trace: boot instructions logged with registers"

if [ -n "$(ls tests/vectors/*.json 2>/dev/null)" ]; then
  echo "== 14. SingleStepTests/z80 vectors (state, T-states, RAM, I/O) =="
  build/sst tests/vectors/*.json > "$OUT/sst.txt" 2>&1
  rc=$?
  tail -1 "$OUT/sst.txt" | sed 's/^/  /'
  [ $rc = 0 ]; check $? "SingleStepTests vectors all match"
else
  echo "== 14. SingleStepTests vectors: SKIP (run tests/get_vectors.sh) =="
fi

if ldconfig -p 2>/dev/null | grep -q "libSDL2-2\.0\.so\.0"; then
  echo "== 15. SDL2 frontend (dlopen, dummy video/audio drivers) =="
  # Interactive loop headlessly: window+renderer+audio open, 150 paced
  # frames run, then the normal screenshot path shows the boot banner.
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    $EMU --rom roms/hc91.rom --sdl --sdl-frames 250 --text \
    > "$OUT/sdl.txt" 2>"$OUT/sdl_err.txt"
  rc=$?
  grep -q "sdl: exiting after 250 frames" "$OUT/sdl_err.txt" \
    && grep -q "HC - 91" "$OUT/sdl.txt" && [ $rc = 0 ]
  check $? "SDL session runs 250 frames and boots to the banner"
else
  echo "== 15. SDL2 frontend: SKIP (libSDL2 runtime not present) =="
fi

echo "== 16. SZX snapshots + RZX input recording =="
# (a) szx round-trip of the running multicolour engine
$EMU --rom roms/hc91.rom "$OUT/multi.tap" --autoload --turbo --frames 400 \
     --save-szx "$OUT/rt.szx" > /dev/null 2>&1
$EMU --rom roms/hc91.rom "$OUT/rt.szx" --turbo --frames 100 \
     --fb-dump "$OUT/rt_szx.fb" > /dev/null 2>&1
build/fbcheck "$OUT/rt_szx.fb" --band 88 33 3 > /dev/null
check $? ".szx save/load resumes the multicolour engine"
# (b) zlib-compressed RAMP pages (what other emulators write) must load
# through the built-in inflater
python3 - "$OUT/rt.szx" "$OUT/rt_comp.szx" <<'PYEOF'
import sys, zlib, struct
d = open(sys.argv[1],'rb').read()
out = bytearray(d[:8]); off = 8
while off + 8 <= len(d):
    fid = d[off:off+4]; sz = struct.unpack('<I', d[off+4:off+8])[0]
    pay = d[off+8:off+8+sz]
    if fid == b'RAMP':
        pay = struct.pack('<HB', 1, pay[2]) + zlib.compress(bytes(pay[3:]), 9)
    out += fid + struct.pack('<I', len(pay)) + pay
    off += 8 + sz
open(sys.argv[2],'wb').write(bytes(out))
PYEOF
$EMU --rom roms/hc91.rom "$OUT/rt_comp.szx" --turbo --frames 100 \
     --fb-dump "$OUT/rt_csz.fb" > /dev/null 2>&1
build/fbcheck "$OUT/rt_csz.fb" --band 88 33 3 > /dev/null
check $? ".szx with zlib-compressed pages loads (built-in inflate)"
# (c) rzx: record a typed BASIC calculation, then replay it with NO key
# events — the inputs come back from the recorded port reads alone
$EMU --rom roms/hc91.rom --frames 450 --type 'p2+3*4\n@260' \
     --rzx-record "$OUT/calc.rzx" > /dev/null 2>&1
$EMU --rom roms/hc91.rom "$OUT/calc.rzx" --frames 460 --text 2>/dev/null \
  | grep -q "^14"
check $? "RZX replay reproduces the recorded session (PRINT 2+3*4 = 14)"

echo "== 17. HC family: HC-85/HC-90 ROMs + HC-128 (0x7FFD banks, AY) =="
$EMU --machine hc85 --frames 250 --text 2>/dev/null | grep -q "HC - 85"
check $? "HC-85 ROM boots to its banner"
$EMU --machine hc90 --frames 250 --text 2>/dev/null | grep -q "HC - 90"
check $? "HC-90 ROM boots to its banner"
# HC-128 = HC-91-derived ROM + 128K RAM via 0x7FFD + AY at FFFD/BFFD
# (both confirmed by code in the genuine ROM). BASIC recipe: CLEAR the
# stack below the banked region, write different values to banks 0/1 at
# 0xC000, switch back and PEEK (= 11); then program a 1007.6 Hz AY tone
# on channel A through OUTs and save snapshots while it plays.
$EMU --machine hc128 --frames 2140 --wav "$OUT/ay128.wav" \
     --save-szx "$OUT/rt128.szx" --save-z80 "$OUT/rt128.z80" \
     --type 'x32767\n@260' --type ' @392' \
     --keys '404:CAPS+SYM' --keys '416:SYM+O' --type '32765,16:@428' \
     --type 'o49152,11:@548' \
     --keys '680:CAPS+SYM' --keys '692:SYM+O' --type '32765,17:@704' \
     --type 'o49152,22:@824' \
     --keys '956:CAPS+SYM' --keys '968:SYM+O' --type '32765,16:@980' \
     --type 'p@1100' --keys '1112:CAPS+SYM' --type 'o49152\n@1124' \
     --type ' @1248' \
     --keys '1260:CAPS+SYM' --keys '1272:SYM+O' --type '65533,0:@1284' \
     --keys '1392:CAPS+SYM' --keys '1404:SYM+O' --type '49149,110:@1416' \
     --keys '1548:CAPS+SYM' --keys '1560:SYM+O' --type '65533,7:@1572' \
     --keys '1680:CAPS+SYM' --keys '1692:SYM+O' --type '49149,62:@1704' \
     --keys '1824:CAPS+SYM' --keys '1836:SYM+O' --type '65533,8:@1848' \
     --keys '1956:CAPS+SYM' --keys '1968:SYM+O' --type '49149,15\n@1980' \
     --text > "$OUT/hc128.txt" 2>/dev/null
grep -qE "^11 " "$OUT/hc128.txt"
check $? "HC-128: 0x7FFD pages distinct banks at 0xC000"
python3 tests/check_beep.py "$OUT/ay128.wav" 1007.6 1 14 > /dev/null
check $? "HC-128: AY channel A tone measures ~1007.6 Hz"
$EMU --machine hc128 "$OUT/rt128.szx" --frames 100 --wav "$OUT/rl128.wav" \
     --text 2>/dev/null | grep -qE "^11 "
check $? "HC-128 .szx (8 pages + AY block) round-trips"
python3 tests/check_beep.py "$OUT/rl128.wav" 1007.6 10 22 > /dev/null
check $? "HC-128 .szx reload: AY tone resumes"
$EMU --machine hc128 "$OUT/rt128.z80" --frames 100 --wav "$OUT/rl128b.wav" \
     --text 2>/dev/null | grep -qE "^11 "
check $? "HC-128 .z80 (v2 mode 3, banks 0-7) round-trips"
python3 tests/check_beep.py "$OUT/rl128b.wav" 1007.6 10 22 > /dev/null
check $? "HC-128 .z80 reload: AY tone resumes"
$EMU "$OUT/rt128.z80" --frames 5 2>&1 | grep -q "128K snapshot"
check $? "48K machine refuses 128K snapshots with a hint"

if [ "${RUN_Z80TEST:-0}" = 1 ]; then
  echo "== 8. Rak's z80test in-emulator (slow: ~2 min each) =="
  YS=""
  for f in $(seq 2200 1800 29200); do YS="$YS --type y@$f"; done
  for t in z80full z80ccf; do
    eval $EMU --rom roms/hc91.rom software/z80test/$t.tap --autoload --turbo \
         --frames 38000 --text $YS > "$OUT/$t.txt" 2>&1
    grep -q "all tests passed" "$OUT/$t.txt"; check $? "$t: all 160 tests passed"
  done
fi

echo
echo "RESULT: $pass passed, $fail failed"
[ "$fail" = 0 ]
