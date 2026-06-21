#!/bin/bash
# Build STELLAR DRIFT into a loadable .tap (with a loading screen).
set -e
cd "$(dirname "$0")"
EMU=../build/hc91emu

python3 tools/mksprites.py
pasmo src/game.asm build/game.bin

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
