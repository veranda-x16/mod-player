# x16-mod — an Amiga ProTracker module player for the Commander X16

A native MOD player for the X16. It streams 4-voice ProTracker modules from
banked RAM straight into VERA's PCM FIFO at 16 kHz, 8-bit stereo, with a
mouse-driven browser so you can point at a folder full of `.mod` files and
just listen.

Everything runs on the 65C02 — no cartridge, no extra hardware.

## What it does

- Plays 4-channel, 31-instrument ProTracker modules: `M.K.`, `M!K!`, `M&K!`,
  `N.T.`, `FLT4`, `4CHN`
- Full ProTracker effect set, including the awkward corners — pattern loop,
  pattern delay, note delay/retrigger, finetune, vibrato and tremolo waveforms
- Browses the SD card: folders, subfolders, and a two-column file list
- Plays a whole folder in sequence, or shuffled, and never repeats a track
  back-to-back in shuffle
- Per-channel display: note, sample, volume, effect, and a level meter
- Loading progress, so a big module doesn't look like a hang

## Requirements

- A Commander X16 with **2 MB of banked RAM** recommended. It runs on 512 KB,
  but most real modules won't fit and are listed as too big.
- An SD card (or the emulator's hostfs).

## Installing

Unzip the archive onto the card:

```
AUTOBOOT.X16    the player — the ROM runs this automatically at power-on
MODPLAY.PRG     the same program, to start by hand
README.TXT
```

Put your `.mod` files next to it, or in folders alongside it — the player
scans the current directory at startup and you can browse from there.

To start it by hand:

```
LOAD"MODPLAY.PRG",8,1
RUN
```

**Note:** `AUTOBOOT.X16` only self-starts from the **root** of the card. In a
subdirectory it's inert — launch `MODPLAY.PRG` instead.

## Controls

**Mouse**

| | |
|---|---|
| click a folder | enter it (`..` goes back up) |
| click a module | load and play it |
| `\|<` / `>\|` | previous / next track |
| `play` | pause and resume |
| `seq` / `rnd` | in-order or shuffled playback |
| `-` / `+` | volume, 0–15 |
| `^` / `v` | page the file list |

**Keyboard**

| key | action |
|---|---|
| up / down | move the cursor one entry |
| left / right | jump a whole column — the fast way through a big folder |
| RETURN | open the entry — enter a folder, or play a module |
| `n` | next track |
| `p` | previous track |
| `r` | toggle shuffle |
| SPACE | pause / resume |
| `+` / `-` | volume |
| `q` | quit |

Press play with nothing loaded and it starts the first playable module in the
current folder, then works through the rest.

## The display

The top line shows the module title, its signature, the pattern count, and
which track you're on. Below that: order position, row, speed, BPM, elapsed
time, and an underrun counter — that last one should stay at 0. If it climbs,
the CPU isn't keeping up with the audio.

Then one line per channel with the note, sample number, volume and effect, and
a level meter showing the actual sample peak on a logarithmic scale — green
through amber to red, with a peak-hold marker.

## Where to get modules

- **The Mod Archive** — <https://modarchive.org> — the largest collection.
  Use the format filter for ProTracker `.mod`; the site also hosts XM, S3M and
  IT files, which this player does not read.
- **Aminet** — <https://aminet.net/tree?path=mods> — Amiga-native, so almost
  everything here is genuine 4-channel material.
- **Modland** — <http://ftp.modland.com/pub/modules/> — organised by composer,
  with a large `Protracker` directory.
- **scene.org** — <https://files.scene.org/browse/music/> — demoscene archives.

If a module is rejected, the reason appears on screen. The usual causes are a
multi-channel file (`6CHN`, `8CHN`, `16CH`) — this player is 4 voices — or a
module too large for the RAM you have.

## Feedback

Bug reports welcome — particularly modules that misbehave. If one plays wrong,
the module name and what you hear is enough to chase it.
