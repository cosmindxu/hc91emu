#!/bin/bash
# Build STELLAR DRIFT into a loadable .tap.
set -e
cd "$(dirname "$0")"
python3 tools/mksprites.py
pasmo src/game.asm build/game.bin
python3 tools/mktap.py build/game.bin build/game.tap 32768 stellar
echo "built build/game.tap"
