#!/bin/bash
# Package a Windows x86-64 release zip: cross-build the exe (make
# windows; set CROSSCC for a non-default mingw toolchain), fetch the
# official SDL2.dll runtime (zlib-licensed, redistributable), and
# bundle the ROMs, manual and readmes into dist/hc91emu-win64.zip.
set -e
cd "$(dirname "$0")/.."

make windows

DIST=dist/hc91emu-win64
rm -rf "$DIST"
mkdir -p "$DIST/roms"

# official SDL2 Windows runtime (only needed for --sdl)
if [ ! -f dist/SDL2.dll ]; then
  echo "fetching the SDL2 Windows runtime..."
  url=$(curl -sL --max-time 60 \
        "https://api.github.com/repos/libsdl-org/SDL/releases" \
        | grep -oE '"browser_download_url": *"[^"]*SDL2-2\.[0-9.]*-win32-x64\.zip"' \
        | head -1 | cut -d'"' -f4)
  [ -n "$url" ] || { echo "error: cannot locate the SDL2 runtime zip" >&2; exit 1; }
  curl -sfL --max-time 300 "$url" -o dist/sdl2.zip
  unzip -o -d dist dist/sdl2.zip SDL2.dll README-SDL.txt >/dev/null
  rm -f dist/sdl2.zip
fi

cp build/win/hc91emu.exe "$DIST/"
cp dist/SDL2.dll "$DIST/"
cp dist/README-SDL.txt "$DIST/" 2>/dev/null || true
cp roms/*.rom "$DIST/roms/"
cp docs/manual.pdf "$DIST/"
cp docs/README-windows.txt "$DIST/README-windows.txt"
cp README.md "$DIST/"

( cd dist && rm -f hc91emu-win64.zip && zip -qr hc91emu-win64.zip hc91emu-win64 )
echo "release: dist/hc91emu-win64.zip"
unzip -l dist/hc91emu-win64.zip | tail -4
