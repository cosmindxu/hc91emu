#!/bin/bash
# Build a Debian/Ubuntu package: dist/hc91emu_<ver>_amd64.deb
# Layout: /usr/bin/hc91emu, ROMs in /usr/share/hc91emu/roms (the binary
# falls back there when not run from a source tree), man page, PDF
# manual, copyright and changelog. Needs only dpkg-deb (no root:
# --root-owner-group).
set -e
umask 022                      # Policy perms: 0755 dirs, 0644 files
cd "$(dirname "$0")/.."

VER=1.0.0
ARCH=$(dpkg --print-architecture)
PKG=dist/deb/hc91emu_${VER}_${ARCH}
MAINT="HC91 Dev <dcosmin22@icloud.com>"

make clean >/dev/null
make >/dev/null

rm -rf "$PKG"
mkdir -p "$PKG/DEBIAN" \
         "$PKG/usr/bin" \
         "$PKG/usr/share/hc91emu/roms" \
         "$PKG/usr/share/man/man1" \
         "$PKG/usr/share/doc/hc91emu"

install -m 755 build/hc91emu "$PKG/usr/bin/hc91emu"
strip "$PKG/usr/bin/hc91emu"
install -m 644 roms/*.rom "$PKG/usr/share/hc91emu/roms/"
gzip -9n < docs/hc91emu.1 > "$PKG/usr/share/man/man1/hc91emu.1.gz"
install -m 644 docs/manual.pdf "$PKG/usr/share/doc/hc91emu/manual.pdf"
install -m 644 README.md "$PKG/usr/share/doc/hc91emu/README.md"

cat > "$PKG/usr/share/doc/hc91emu/copyright" <<EOF
hc91emu - emulator for the I.C.E. Felix HC family

Emulator code: Copyright (c) 2026 $MAINT.
All rights reserved (no public license has been chosen yet).

The ROM images in /usr/share/hc91emu/roms are preservation dumps of
the genuine I.C.E. Felix HC-85/90/91/128/2000 firmware (via
speccy4ever.speccy.org) and of the Sinclair ZX Spectrum 48K ROM
(Copyright Amstrad plc; Amstrad permits redistribution of the Spectrum
ROMs for emulation purposes). They are included for use with this
emulator only.

SDL2 is not included; when present on the system it is loaded at run
time (zlib license, see the libsdl2-2.0-0 package).
EOF

DATE=$(date -R)
{ printf 'hc91emu (%s) unstable; urgency=medium\n\n' "$VER"
  printf '  * Initial packaged release: HC-85/90/91/128/2000 emulation,\n'
  printf '    SDL2 frontend, tape/disk/CP/M, snapshots, RZX, debugger.\n\n'
  printf ' -- %s  %s\n' "$MAINT" "$DATE"
} | gzip -9n > "$PKG/usr/share/doc/hc91emu/changelog.gz"  # native version, no -revision

SIZE=$(du -sk --exclude=DEBIAN "$PKG" | cut -f1)
cat > "$PKG/DEBIAN/control" <<EOF
Package: hc91emu
Version: $VER
Architecture: $ARCH
Maintainer: $MAINT
Installed-Size: $SIZE
Depends: libc6 (>= 2.34)
Recommends: libsdl2-2.0-0
Suggests: groff-base
Section: otherosfs
Priority: optional
Description: emulator for the I.C.E. Felix HC family (ZX Spectrum clones)
 Cycle-exact emulator for the Romanian I.C.E. Felix HC home computers:
 the ZX Spectrum-compatible HC-85, HC-90 and HC-91, the 128K-class
 HC-128 (banked RAM, AY-3-8912 sound) and the HC-2000 (i8272 floppy
 disk interface, boots CP/M 2.2 from disk images).
 .
 Features per-M-cycle memory and I/O contention, beam-accurate video,
 floating bus, exact tape pulse timing with multi-load cassette
 control, .tap/.tzx/.sna/.z80/.szx/.scr/.rzx support, a scriptable
 headless mode for automation (screenshots, screen OCR, WAV capture,
 input scripting), a built-in debugger/disassembler, and an optional
 SDL2 window with live audio (SDL2 is loaded at run time when
 present).
EOF

mkdir -p dist
dpkg-deb --build --root-owner-group "$PKG" \
         "dist/hc91emu_${VER}_${ARCH}.deb"
echo
dpkg-deb --info "dist/hc91emu_${VER}_${ARCH}.deb" | head -12
echo "release: dist/hc91emu_${VER}_${ARCH}.deb"
