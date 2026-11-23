# Background music on Veranda (form b): design

Status: designed, not implemented. Form (a), the Veranda MOD Player, is
done (`make veranda`; Veranda DECISIONS V52). This is what keeping the
music playing after the player quits would take, with the numbers measured
for form (a). It replaces section 3.2 of the original study where the two
differ: since that study Veranda's resident core has grown (crash recovery,
V50; feedback, V51) and has **352 bytes free**, not ~700, so the study's
450 resident bytes no longer fit.

## What it is

"Keep playing when I quit" in the player's play menu: the player hands the
module over to a **service** that lives in kept RAM banks and is driven by
VERA's AFLOW interrupt, then quits to the shell. Music goes on under the
shell, Paint, Veranda Calc, Veranda Term. A small desk accessory, **MOD
Control**, shows the title and time and has play/pause, next, previous,
stop and the volume. Opening the player again takes the service back.

## Gated on 2 MB

* Veranda's RAM baseline is 512 KB. There, after the kernel's banks and
  their crash-recovery copy, `BankAlloc` has 48 banks. The service needs
  its 3 code banks, the module's banks (a 150 KB module: 19) and the
  foreground needs its own (an accessory: 22; Veranda Calc: about 20).
  That does not fit.
* So the player offers the menu item on every machine, and on one with
  fewer than 256 banks (`kMemBanks`) it answers with a dialog box:
  "Background music needs 2 MB of RAM." Nothing else changes: form (a)
  works on 512 KB (gated: the session plays a 154 KB module there).
* The kernel calls refuse too (`SvcStart` returns `INSUFF_SPACE`), so no
  other program can start it on a small machine.

## The pieces

| # | What | Where | Size |
|---|---|---|---|
| K5 | **Kept banks.** `BankKeep(n)`: `BankAlloc` whose run is also marked in a `kept` bitmap (bank S). `BanksInit` (every program's load) frees all but kept runs; `DaRun`/`RstrAppl`'s bank table image keeps them; `BankUnkeep(first)` and `ToBASIC` free them. New calls at the end of `api/veranda.api` | bank S | ~70 bytes |
| K6 | **The service hook, resident.** `svcBank` (1 byte) and a test in `IrqEntry` before `intFirstVector`: if `svcBank` and `VERA_ISR & VERA_IEN & AFLOW`: save `ram_bank`, map `svcBank`, `jsr $A000` (the service's batch entry), restore, `rti` unless VSYNC is pending. As V51 does for bank G2, the bank is mapped directly, not through a far call. | resident | ~40 bytes |
| K6b | **The bounce.** The service's code, caches and volume table run at `$A000` in its own bank, so it cannot map a sample bank over itself. A resident routine copies 64 bytes from (bank, address) to a 64-byte resident buffer and maps the service back; the refill copies from there into the cache page. Two copies a byte: about +10 cycles a refilled byte, +15-40 cycles an output frame on the 15-58 that refills cost now (*guess*, to be measured by `make chunk`'s cycle build) | resident | ~110 bytes (routine 46, buffer 64) |
| K6c | **Mailbox.** 16 bytes: status out (order, pattern, row, seconds, underruns, a song-end count, the title's bank and address), commands in (pause, stop, next, previous, volume) | resident | 16 bytes |
| K7 | **The engine in three banks** (x16-mod, a "banked" build of the same sources): S1 at `$A000`: mixer hot loop, the four cache pages, the volume table, the chunk loop and the cache layer (~7.6 KB); S2: sequencer, hooks, render loop and their own copy of cc65's runtime (~7 KB); S3: step table, mixer API (2.8 KB). Calls between them through a bank-to-bank stub in S1 (each bank has the stub at the same address, so it can switch under itself). Its zero page (75 bytes: cc65's 26, the engine's 49) is swapped with whatever the foreground has there at each batch (~1,400 cycles a batch, 0.4 % of the CPU) | x16-mod | 3 kept banks |
| K8 | **Leave it alone.** `AudioOff`/`AudioReset` (V52) skip when `svcBank` is set; `DaRun`, `AppReset`, `RstrAppl` and the panic box's `CrashInit` do not touch it; `ToBASIC` stops it (AFLOW off, `svcBank` 0, kept banks freed). A program that wants VERA's PCM for itself while the service plays gets `SvcStop` first (the player does that when it starts) | bank S | ~25 bytes |
| K9 | **The next module.** The interrupt cannot open files (CMDR-DOS is not re-entrant, kernel calls are banned there). A system main-loop hook, `sysMainVec`, called by `MainLoop` beside `appMain`, lets the service load the next module of its list between the foreground's callbacks (the foreground freezes 1-4 s at a track change). Version 1 without it: repeat the module, or stop at its end; next/previous only from MOD Control or the player | bank X, resident 3 | ~30 bytes |
| — | **MOD Control** (desk accessory, `type = deskacc`, sequential, ~4 KB): reads the mailbox, writes commands; must not open a record file while the application has one (V31) | x16-mod | — |

Resident total: ~170 bytes of the 352 free (K6 40, K6b 110, K6c 16, K9
3). The rest stays in banks S and X.

## Costs to the user

* Every program runs at 23-29 % of the CPU while the service plays at
  16 kHz (measured for form a: 354-385 cycles of every 499 a frame, the
  frame interrupts nested in the batches included), plus the bounce
  (*guess*: 1-3 %). Everything is 3-4.7 times slower.
* 3 + the module's banks are gone for every program until the music stops.

## Gates (M6)

1. `make chunk`: the banked build's hashes equal `tests/golden/chunk_hashes.txt`
   (the bounce changes no sample).
2. A session on a 2 MB machine: the player starts the service and quits;
   the shell, Paint (draw, save), the Calculator accessory, Veranda Calc
   (recalculate) run while it plays; 0 underruns throughout (the mailbox's
   count); `gate_desk`, `gate_paint` and `gate_shell`'s screens unchanged
   with the service running.
3. `ToBASIC` stops it; the panic box stops nothing but the faulting program.
4. On 512 KB the menu item's dialog says it needs 2 MB, and `SvcStart`
   refuses.
5. The resident and bank sizes logged against the numbers above.

## Effort

8-12 days, most of it K7 (the banked engine) and its gate; the kernel part
(K5, K6, K8) about 3 days.
