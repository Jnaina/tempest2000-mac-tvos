# Tempest 2000 on the Mac

This folder makes Jeff Minter's **Tempest 2000** (Atari Jaguar, 1994) build and play natively on macOS
straight from the source in this repository, with keyboard (or game controller) control.

## What this is – and isn't

Tempest 2000 is ~24,000 lines of Motorola 68000 assembly plus nine programs for the Jaguar's custom
"Tom" GPU and a DSP, driving the console's blitter, object processor and sound hardware.  None of that
exists on a Mac, so the source is **not** translated into C (that would be a different, new game).
Instead:

1. the original source is assembled here, unmodified, with `rmac`/`rln` into `t2000.abs`, and the
   result is checked to be **byte-for-byte identical to the original 1994 build**;
2. the Jaguar hardware is provided by the open-source **Virtual Jaguar** emulation core
   (libretro edition, GPL-3, vendored in `third_party/`), built as a shared library;
3. `t2k_host.c` is a small native SDL2 program that loads that core, shows its video
   (Metal on macOS, sharp pixels at any window size, 4:3), plays its audio, maps your keyboard to
   the Jaguar controller, and keeps the original 60 Hz timing.

So what you play is the real game code running on an emulated Jaguar, launched as a normal Mac program.

## Build and run

    xcode-select --install     # compiler and make, if you don't have them
    brew install sdl2
    cd mac
    make                       # assembles the game, builds the core and the host
    make run                   # or ./t2k_host

`make single` builds **one self-contained executable, `./t2k`**: the emulator core is linked in and the
game (`t2000.abs`) is embedded, so you can copy that single file anywhere and run it (on a Mac with Homebrew's
SDL2 static library present it needs nothing else; otherwise it needs only the SDL2 library).
High scores still go to `~/Library/Application Support/Tempest2000/`.

`make app` creates `T2K.app` (run `brew install dylibbundler` first if you want it independent
of Homebrew).  The first `make` needs network access only if `../rmac`, `../rln` or the core are missing
(they're normally already included).

## Keyboard controls

| Jaguar | Keys |
|---|---|
| D-pad (move round the web, menu up/down) | Arrow keys, or W A S D |
| **B – Fire** (default) | Space or X |
| **A – Jump** (default) | Z |
| **C – Superzapper** (default) | C or V |
| Option (game options, menu) | Return or O |
| Pause | P or Backspace |
| Keypad 0-9, `*`, `#` | 0-9, `-`, `=` |

Tempest 2000 lets you re-assign A/B/C in *Options → firebutton options*; the names above are the game's
defaults.  Press a fire key at the title screens to start; choose game type and level with left/right
and up/down.

| Emulator | Keys |
|---|---|
| Save / load state | F5 / F7 |
| Reset | F10 |
| Fast-forward (hold) | Tab |
| Mute | M |
| Scanline overlay | F4 |
| Fullscreen | F11 or Alt+Return |
| Quit | Esc |

Game controllers work too (d-pad / left stick, A = jump, B = fire, X = superzapper, Start = Option, Back = Pause).
High scores are saved automatically to `~/Library/Application Support/Tempest2000/`.
Options: `./t2k_host --help` (`--scale N`, `--fullscreen`, `--integer`, `--scanlines`, `--no-vsync`, `--mute`).

## Files

    mac/Makefile                     one-stop build
    mac/t2k_host.c, libretro.h       the SDL2 front end (GPL-3.0-or-later; libretro.h is MIT)
    mac/third_party/virtualjaguar-libretro/   emulation core, pinned to commit dd332ea (v3.6.1), GPL-3.0
    ../rmac, ../rln                  assembler and linker used by the original project

## Notes

* Developed and tested on Linux: the game assembles bit-exactly, boots through the title screens, game
  select, level select and into play (steering the ship round the web works).  I could not run it on a Mac
  from here, so the macOS build of the core and the window/audio behaviour there are untested; everything
  uses portable code and the core's own Makefile supports macOS (Intel and Apple Silicon).
* Speed: Virtual Jaguar has to emulate four processors; on the (old, 2012) machine used for testing it
  ran at ~38 fps.  Apple Silicon should be several times faster; `T2K_DEBUG=1 ./t2k_host` prints the
  frame rate each second.
* The game source is copyrighted by its owners; `t2000.abs` is built locally and should not be redistributed.
