# x16-mod

A ProTracker MOD replay library for the Commander X16 (8 MHz 65C02, VERA PCM,
2 MB banked RAM), plus a player built on it.

- [PLAN.md](PLAN.md) — the plan and its gates
- [docs/decisions.md](docs/decisions.md) — what was measured, and what it changed
- [docs/environment.md](docs/environment.md) — verified toolchain and emulator facts

## Status

| Phase | | State |
|---|---|---|
| 0 | Harness | done |
| 1 | VERA PCM audio path | **PASS** — full theoretical budget available |
| 2 | 4-voice stereo mixer | **PASS** — 194 cyc/frame, 61% headroom @16 kHz, bit-exact |
| 3 | Banked sample cache | **PASS** — 14.19 cyc/byte, 6/6 cases |
| 4 | Replay/mixer boundary | done |
| 5 | MOD loader | **PASS** — matches libopenmpt, loads on hardware |
| 6 | Sequencer + trace oracle | **PASS** — matches micromod over 20,000 ticks |
| 7 | Full ProTracker effects | **PASS** — all 25 modules trace-identical |
| — | Real-module corpus | **PASS** — 484/488 trace-identical (4 are 15-instrument, both reject) |
| — | Real-time playback | **works** — `build/PLAY.PRG`, 0.72 envelope correlation vs micromod's renderer |
| 8 | RAM budget | **done** — volume table reduced to 17 levels, 12.5 KB freed |
| 10 | Initial UI | **works** — position, row, time, 4 level bars, pause/quit |
| 9 | Library API | not yet separated from the example |

## Build and test

```sh
export CC65_HOME=$HOME/.local/share/cc65    # the Makefile sets this too
make test                                   # every gate above
make release                                # ready-to-copy build in release/
make zip                                    # release/x16-mod.zip, for an SD card
make run                                    # boot in the emulator, with modules
make run MODS=/path/to/mods                 # ...from somewhere else
make run-release                            # boot the bare release, no modules
make deploy                                 # copy onto a mounted card
make build/PLAY.PRG                         # the player
python3 tools/sweep_corpus.py 1000          # the 488-module corpus (parallel)
make chunk                                  # render loop unchanged? (golden hashes)
make chunk-update                           # re-record those hashes, deliberately

# to play: copy release/ to an SD card with your .mod files beside it.
# AUTOBOOT.X16 starts the player at power-on; MODPLAY.PRG is the same
# program to launch by hand. The playlist is built by scanning for *.mod.
```

Tests drive the emulator headlessly and measure in exact 65C02 cycles using
its guest-readable counter at `$9FB8-$9FBB`.

The render loop is checked by hashing its chunk decisions rather than by
comparing audio. The emulator's PCM output is cycle-deterministic but its FIFO
drain follows the *host* audio device, so two WAV captures of one unmodified
binary differ in about 0.09% of frames -- a bigger difference than a real
regression need be. `make chunk` instead runs a build with the FIFO gate
compiled out, which makes the loop a pure function of the module, and hashes
every chunk's frame count, voice phases and read positions. An unchanged hash
means bit-identical audio.

The sequencer is checked three ways: the X16 build against a native build of
the same source (catches porting bugs), and that against
[micromod](https://github.com/martincameron/micromod) (catches ProTracker
semantics bugs). `make micromod` fetches the oracle.

## The library, and the Veranda MOD Player

The engine is also a library, `build/lib/x16mod.lib` (`make lib`; the API in
`include/x16mod.h`): a host gives it a read function and banks and feeds the
FIFO either by polling (`mod_update`, as `examples/minimal/` does in 24
lines: `make minimal`) or from VERA's AFLOW interrupt (`mod_irq_batch`).

`examples/veranda/` is the player as an application for
Veranda, the GEOS-like desktop for the X16, in Veranda Look 2 (Veranda's
DECISIONS V74, V77, V79): the kernel's menu bar (the Veranda mark, then MOD
Player with the about box and the desk accessories, File with Open... and
Play all, Play with repeat and shuffle, View; the clock and the speaker),
the SDK's window chrome (a title band on the theme's accent with the
module's name and the close glyph, a status bar, a frame and the dither
shadow) over the desk's colour, Look 2's palette (light or dark), its own
tile in the shell; the playlist (folders and modules by their long names, a
scroll bar, double click to play, the playing one marked), "now playing" in
the system font (title, position, pattern, row, speed, BPM, elapsed time and
the song's length, a progress bar), the transport buttons as GEOS icons,
four VU meters in the colour map, the pattern view; Veranda's Open dialog
(`vr_file`) for modules and folders; opened by a double click on a `.mod`
in Veranda's shell, long names too (`dataLongName`). The music comes from the AFLOW interrupt through Veranda's
`intFirstVector`, so it does not stop for menus, dialog boxes, file
listings or desk accessories; at 16 kHz the engine takes 71-77 % of the
CPU and the rest of the system runs 3-4.7 times slower while it plays, so
the screen is updated in small pieces (a field when its value changes, the
meters through the colour map only).

    make vsdk VERANDA=~/Projects/geos   # the Veranda SDK into build/vsdk
    make veranda                        # build/veranda/MOD Player.cvt
    make veranda-gate                   # the engine under Veranda: chunk hashes
    make veranda-rt                     # real time under a stress script
    make veranda-app-gate               # the application's scripted session, its pictures, a stress run
    make veranda-mouse-gate             # next, previous, a song's end: the real mouse and keys, R49, an SD card

## Licence

MIT (see [LICENSE](LICENSE)).
