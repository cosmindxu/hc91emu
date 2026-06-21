# STELLAR DRIFT

A small cave-flyer for the **HC-91 / ZX Spectrum 48K**, written in Z80
assembly. You pilot a spaceship cruising left-to-right through a series of
themed *zones*: **dodge** the asteroids and enemy craft, **shoot** what you
can, and **collect** crystals for score.

![title](docs/title.png)
![gameplay](docs/play.png)

## Controls

| Action | Keyboard | Kempston |
|--------|----------|----------|
| Up     | **Q**    | up       |
| Down   | **A**    | down     |
| Left   | **O**    | left     |
| Right  | **P**    | right    |
| Fire   | **Space**| fire     |

Press **Fire** on the title screen to start, and again after **GAME OVER**.

## Gameplay

- **Asteroids** (●) and **enemy craft** drift in from the right. Touching one
  costs a life; you respawn with a few seconds of (blinking) invulnerability.
- **Crystals** (◆) are worth **10** points — fly into them.
- Your shots destroy asteroids and enemies for **5** points each.
- Every ~15 seconds you advance a **zone**. Each zone re-tints the world and
  spawns hazards faster. Four zones cycle: black → blue → red → magenta.
- You start with **3 ships**. Lose them all and it's game over.

## Building

Needs a Z80 assembler (`pasmo`) and Python 3:

```sh
sudo apt-get install -y pasmo        # one-time
./build.sh                           # -> build/game.tap
```

`build.sh` runs three steps:
1. `tools/mksprites.py` — turns the ASCII-art sprites into `src/sprites.inc`.
2. `pasmo` — assembles `src/game.asm` to a raw binary at `$8000`.
3. `tools/mktap.py` — wraps it in a `.tap` with a BASIC autoloader.

## Running

In the emulator at the repo root:

```sh
# interactive (SDL window; Tab = turbo)
../build/hc91emu --autoload --kempston --sdl game/build/game.tap

# headless screenshot (handy for development)
../build/hc91emu --autoload --turbo --kempston \
    --joy "1300-1308:F" --frames 1600 \
    --screenshot shot.png game/build/game.tap
```

`game/build/game.tap` also loads in any ZX Spectrum 48K emulator or on real
hardware.

## How it works

The whole game is one Z80 source file, `src/game.asm`:

- **`build_addrtab`** precomputes the 192 screen-row addresses (the Spectrum's
  display memory is interleaved), so plotting is a table lookup, not a
  bit-twiddle, every frame.
- **`draw_sprite` / `erase_sprite`** are a masked 16×16 blitter. Each sprite
  row is `(data, mask)`; the routine shifts both right by `x & 7` across three
  bytes and writes `screen = (screen AND mask) OR data`, so sprites move with
  single-pixel precision and let the background show through.
- Everything moving is an entry in a small fixed array (`objs`, `bullets`,
  `stars`); each frame it is erased at its old position, updated, collided, and
  redrawn — the classic flicker-light Spectrum approach.
- Text (HUD, title) is blitted 8×8 from the **ROM font** at `$3C00`.
- Input reads the keyboard half-rows directly and the Kempston port `$1F`
  (rejecting the floating-bus reading when no interface is present).

## Files

```
src/game.asm       the game
src/sprites.inc    generated sprite data (do not edit by hand)
tools/mksprites.py ASCII-art -> sprite data
tools/mktap.py     raw binary -> .tap with BASIC loader
build.sh           build everything
```
