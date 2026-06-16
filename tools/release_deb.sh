#!/bin/bash
# Build a Debian/Ubuntu package: dist/hc91emu_<ver>_<arch>.deb
# Layout: /usr/bin/hc91emu, ROMs in /usr/share/hc91emu/roms (the binary
# falls back there when not run from a source tree), man page, PDF
# manual, desktop launcher + icon + MIME types, copyright and changelog.
# Needs only dpkg-deb (no root: --root-owner-group).
#
#   tools/release_deb.sh [arch]      arch = amd64 (default) | arm64
#
# Cross-building arm64 uses aarch64-linux-gnu-gcc by default (override
# with ARM64_CC / ARM64_STRIP, e.g. the Arm GNU toolchain's
# aarch64-none-linux-gnu-*).
set -e
umask 022                      # Policy perms: 0755 dirs, 0644 files
cd "$(dirname "$0")/.."

VER=1.2.0
ARCH="${1:-$(dpkg --print-architecture)}"
MAINT="HC91 Dev <dcosmin22@icloud.com>"
# Hardened release flags (Debian hardening: PIE, stack protector,
# fortified libc calls, read-only relocations).
CFLAGS="-std=c99 -O2 -Wall -Wextra -fstack-protector-strong \
        -D_FORTIFY_SOURCE=2 -fPIE"
LDFLAGS="-pie -Wl,-z,relro,-z,now"
EMUSRC="z80 machine video tape snapshot keys png wav disasm debug sdl \
        inflate ay fdc rzx main"

case "$ARCH" in
  amd64) CC=gcc; STRIP=strip ;;
  arm64) CC="${ARM64_CC:-aarch64-linux-gnu-gcc}"
         STRIP="${ARM64_STRIP:-aarch64-linux-gnu-strip}" ;;
  *) echo "error: unsupported arch '$ARCH' (amd64|arm64)" >&2; exit 1 ;;
esac
command -v "$CC" >/dev/null || {
  echo "error: compiler '$CC' not found (cross toolchain for $ARCH?)" >&2
  exit 1
}
PKG=dist/deb/hc91emu_${VER}_${ARCH}

bash tools/get_roms.sh --verify >/dev/null || {
  echo "error: ROM dumps missing/bad - run tools/get_roms.sh first" >&2
  exit 1
}

# Build the packaged binary with the target compiler, and the icon
# generator with the host compiler (it runs here at package time).
rm -rf dist/deb/obj-$ARCH
mkdir -p dist/deb/obj-$ARCH
for s in $EMUSRC; do
  $CC $CFLAGS -c src/$s.c -o dist/deb/obj-$ARCH/$s.o
done
$CC $CFLAGS $LDFLAGS -o dist/deb/hc91emu-$ARCH \
    $(for s in $EMUSRC; do echo dist/deb/obj-$ARCH/$s.o; done) -ldl
make build/mkicon >/dev/null

rm -rf "$PKG"
mkdir -p "$PKG/DEBIAN" \
         "$PKG/usr/bin" \
         "$PKG/usr/share/hc91emu/roms" \
         "$PKG/usr/share/man/man1" \
         "$PKG/usr/share/doc/hc91emu" \
         "$PKG/usr/share/applications" \
         "$PKG/usr/share/mime/packages" \
         "$PKG/usr/share/icons/hicolor/256x256/apps" \
         "$PKG/usr/share/icons/hicolor/128x128/apps" \
         "$PKG/usr/share/icons/hicolor/48x48/apps"

install -m 755 dist/deb/hc91emu-$ARCH "$PKG/usr/bin/hc91emu"
$STRIP "$PKG/usr/bin/hc91emu"
install -m 644 roms/*.rom "$PKG/usr/share/hc91emu/roms/"
gzip -9n < docs/hc91emu.1 > "$PKG/usr/share/man/man1/hc91emu.1.gz"
install -m 644 docs/manual.pdf "$PKG/usr/share/doc/hc91emu/manual.pdf"
install -m 644 README.md "$PKG/usr/share/doc/hc91emu/README.md"

# Desktop integration: launcher, icon (rendered from the machine's own
# ROM font by tools/mkicon.c), MIME types for the loadable files. The
# dpkg triggers of shared-mime-info/desktop-file-utils refresh the
# databases on install.
build/mkicon roms/hc91.rom \
  "$PKG/usr/share/icons/hicolor/256x256/apps/hc91emu.png" 256 \
  "$PKG/usr/share/icons/hicolor/128x128/apps/hc91emu.png" 128 \
  "$PKG/usr/share/icons/hicolor/48x48/apps/hc91emu.png"   48  >/dev/null

cat > "$PKG/usr/share/applications/hc91emu.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=HC-91 Emulator
GenericName=I.C.E. Felix HC / ZX Spectrum emulator
Comment=Emulator for the I.C.E. Felix HC-85/90/91/128/2000 home computers
Exec=hc91emu --sdl %f
TryExec=hc91emu
Icon=hc91emu
Terminal=false
Categories=Game;Emulator;
MimeType=application/x-hc91-tape;application/x-hc91-snapshot;
Keywords=spectrum;sinclair;felix;retro;emulator;tape;
EOF

cat > "$PKG/usr/share/mime/packages/hc91emu.xml" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
  <mime-type type="application/x-hc91-tape">
    <comment>ZX Spectrum tape image</comment>
    <glob pattern="*.tap"/>
    <glob pattern="*.tzx"/>
  </mime-type>
  <mime-type type="application/x-hc91-snapshot">
    <comment>ZX Spectrum snapshot</comment>
    <glob pattern="*.sna"/>
    <glob pattern="*.z80"/>
    <glob pattern="*.szx"/>
  </mime-type>
</mime-info>
EOF

cat > "$PKG/usr/share/doc/hc91emu/copyright" <<EOF
hc91emu - emulator for the I.C.E. Felix HC family

Emulator code: Copyright (c) 2026 $MAINT.
Licensed under the MIT License (see the project LICENSE file). The MIT
grant covers the emulator code only, not the ROM images described below.

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
{ printf 'hc91emu (1.2.0) unstable; urgency=medium\n\n'
  printf '  * WAV cassette input: load digitized recordings of real\n'
  printf '    tapes (DC removal + Schmitt trigger; silences become\n'
  printf '    multi-load boundaries).\n'
  printf '  * arm64 package (cross-built); the file loaders are now\n'
  printf '    fuzz-hardened (tap/tzx/wav/snapshot/disk).\n\n'
  printf ' -- %s  %s\n\n' "$MAINT" "$DATE"
  printf 'hc91emu (1.1.0) unstable; urgency=medium\n\n'
  printf '  * SDL: quick state save/load (F2/F4), drag-and-drop loading,\n'
  printf '    fullscreen (F11), --scale N window multiplier.\n'
  printf '  * Desktop integration: launcher, icon, MIME types for the\n'
  printf '    tape and snapshot formats.\n'
  printf '  * tools/get_roms.sh fetches the ROM dumps on demand.\n\n'
  printf ' -- %s  Fri, 12 Jun 2026 18:31:31 +0000\n\n' "$MAINT"
  printf 'hc91emu (1.0.0) unstable; urgency=medium\n\n'
  printf '  * Initial packaged release: HC-85/90/91/128/2000 emulation,\n'
  printf '    SDL2 frontend, tape/disk/CP/M, snapshots, RZX, debugger.\n\n'
  printf ' -- %s  Fri, 12 Jun 2026 15:21:36 +0000\n' "$MAINT"
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
 control, .tap/.tzx/.wav/.sna/.z80/.szx/.scr/.rzx support, a scriptable
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
