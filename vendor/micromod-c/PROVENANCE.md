# micromod (vendored)

Two files from Martin Cameron's **micromod**, a ProTracker replay in ANSI C.

| | |
|---|---|
| Upstream | https://github.com/martincameron/micromod |
| Author | Martin Cameron |
| Licence | BSD 3-Clause — see `LICENSE` in this directory |
| Commit | `83b5d9a` |
| Files | `micromod.c`, `micromod.h` — **unmodified, byte for byte** |

## Why it is here

micromod is this project's **test oracle**, not part of the player. Nothing
from it is compiled into `MODPLAY.PRG`.

`host/oracle_micromod.c` includes `micromod.c` directly in order to read its
file-static sequencer state, and dumps a per-tick trace. That trace is compared
against our own sequencer's, which is how every ProTracker effect in this
project was verified. See `docs/decisions.md`, Phases 6 and 7.

Our replay engine (`src/mod_play.c`) is an independent implementation. micromod
was read as a behavioural reference for what ProTracker does; it was not ported.
Where both contain the same tables — finetune, arpeggio, vibrato sine — those
values are determined by the format's own formulas, and `docs/decisions.md`
records deriving each from its formula and then checking it against micromod's.

## Licence notice

`micromod.c` and `micromod.h` carry no per-file copyright header upstream; the
notice lives in the repository's `licence.txt`, copied here verbatim as
`LICENSE`. BSD-3-Clause requires that notice, the conditions and the disclaimer
travel with redistributed source, which is what this directory does.
