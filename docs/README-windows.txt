HC-91 Emulator for Windows (x86-64)
===================================

An emulator for the I.C.E. Felix HC family of ZX Spectrum-compatible
computers: HC-85 / HC-90 / HC-91 / HC-128 (128K + AY sound) and
HC-2000 (floppy disks + CP/M 2.2).

Quick start (from a Command Prompt or PowerShell in this folder):

    hc91emu --sdl game.tap --autoload
        play a tape interactively: window, 50 Hz, live sound
        (Shift = CAPS SHIFT, Ctrl = SYMBOL SHIFT, Tab = turbo,
         F5 = pause, F6/F7/F8 = tape stop/rewind/swap side, F10 = quit)

    hc91emu --frames 250 --text
        headless: boot to BASIC and print the screen as text

    hc91emu --machine hc2000 --boot-cpm --disk cpm22-hc.img --sdl
        boot CP/M 2.2 from a disk image on the HC-2000

Notes:
  * hc91emu.exe is a console program; run it from a terminal to see
    its output and the tape/debugger messages.
  * SDL2.dll (included) is only needed for --sdl; the headless mode
    runs without it. SDL2 is (c) the SDL authors, zlib license - see
    README-SDL.txt.
  * The roms\ folder must stay next to the exe (or pass --rom).
  * The full manual is in manual.pdf; every option is also described
    in the main README.md.
