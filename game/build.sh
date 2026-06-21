#!/bin/bash
# Build STELLAR DRIFT into a loadable .tap (with a loading screen).
set -e
cd "$(dirname "$0")"
EMU=../build/hc91emu

mkdir -p build                  # output dir is gitignored; absent on a fresh checkout

python3 tools/mksprites.py
pasmo --equ VTEST=0 --equ BTEST=0 src/game.asm build/game.bin

# Guard: psbuf (NSPR*768 = 14592 bytes of runtime scratch) sits right after
# the loaded image, and its end must stay below the stack at 0xFDF0 with a
# safe margin. So the image (org 0x8000) must not exceed this ceiling. Fail
# loudly if it does, instead of corrupting the stack at runtime.
#   limit = 0xFDF0 - STACK_MARGIN(542) - psbuf(14592) - org(0x8000) = 17106
LIMIT=17106
SIZE=$(wc -c < build/game.bin)
if [ "$SIZE" -gt "$LIMIT" ]; then
    echo "ERROR: game.bin is $SIZE bytes, over the $LIMIT-byte limit" \
         "(psbuf would overrun the stack at 0xFDF0)." >&2
    exit 1
fi
echo "image: $SIZE / $LIMIT bytes (free: $((LIMIT - SIZE)))"

# Pass 1: a code-only tap we can boot to grab the title as a loading screen.
python3 tools/mktap.py build/game.bin build/game_nl.tap 32768 stellar

# Pass 2: capture the title screen (if the emulator + ROM are present) and
# prepend it as a loading screen.
ROM=../roms/hc91.rom
if [ -x "$EMU" ] && [ -f "$ROM" ]; then
    "$EMU" --rom "$ROM" --autoload --turbo --frames 1340 \
           --save-scr build/loading.scr build/game_nl.tap >/dev/null 2>&1 || true
fi
python3 tools/mktap.py build/game.bin build/game.tap 32768 stellar build/loading.scr

echo "built build/game.tap"
