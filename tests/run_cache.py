#!/usr/bin/env python3
"""
Phase 3 gate: cache_refill and cache_consume correctness.

Checks the cases that actually break a banked sample cache: crossing the
$BFFF window boundary mid-page, looping back mid-page, a one-shot sample
running out mid-page (the remainder must be silence), and an inactive voice
(a full page of silence, which is what keeps the mixer branchless).
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
from tbrun import Testbench  # noqa: E402

WIN, WIN_END = 0xA000, 0xC000


def pattern(bank, addr):
    """Byte stored at (bank, addr) -- distinct per bank so crossings show up."""
    return ((addr & 0xFF) ^ (0xAA if bank == 2 else 0x00)) & 0xFF


def model(bank, addr, ebank, eaddr, lbank, laddr, loop, active):
    if not active:
        return bytes(256), None
    out = bytearray()
    for _ in range(256):
        if (bank, addr) >= (ebank, eaddr):
            if not loop:
                return bytes(out) + bytes(256 - len(out)), None
            bank, addr = lbank, laddr
        out.append(pattern(bank, addr))
        addr += 1
        if addr >= WIN_END:
            addr -= 0x2000
            bank += 1
    return bytes(out), (bank, addr)


CASES = [
    ("straight run, one bank",      1, 0xA000, 2, 0xBFFF, 1, 0xA000, 0, 1),
    ("crosses the window boundary", 1, 0xBF80, 2, 0xBFFF, 1, 0xA000, 0, 1),
    ("loops mid-page (64 bytes)",   1, 0xA000, 1, 0xA040, 1, 0xA000, 1, 1),
    ("loops mid-page (100 bytes)",  1, 0xA010, 1, 0xA074, 1, 0xA010, 1, 1),
    ("one-shot ends mid-page",      1, 0xA000, 1, 0xA040, 1, 0xA000, 0, 1),
    ("inactive voice -> silence",   1, 0xA000, 1, 0xBFFF, 1, 0xA000, 0, 0),

    # A fresh note starts at the SAMPLE start, before loop_start. The page
    # cannot be filled by replicating one loop iteration there -- the content
    # is not yet periodic. Getting this wrong repeats the first bytes of the
    # attack and buzzes on every note-on. Every case above happens to start
    # exactly at loop_start, which is why they missed it.
    ("16-byte loop, trigger at start",  1, 0xA000, 1, 0xA030, 1, 0xA020, 1, 1),
    ("16-byte loop, offset trigger",    1, 0xA008, 1, 0xA030, 1, 0xA020, 1, 1),
    ("32-byte loop, trigger at start",  1, 0xA000, 1, 0xA060, 1, 0xA040, 1, 1),
    ("64-byte loop, long attack",       1, 0xA000, 1, 0xA0C0, 1, 0xA080, 1, 1),
    ("short loop, trigger one before",  1, 0xA01F, 1, 0xA030, 1, 0xA020, 1, 1),
]


def poke_state(tb, lbl, bank, addr, eb, ea, lb, la, loop, active):
    tb.poke(lbl["c_bank"], bank)
    tb.poke(lbl["c_addr"], addr & 0xFF)
    tb.poke(lbl["c_addr"] + 4, addr >> 8)
    tb.poke(lbl["c_ebank"], eb)
    tb.poke(lbl["c_eaddr"], ea & 0xFF)
    tb.poke(lbl["c_eaddr"] + 4, ea >> 8)
    tb.poke(lbl["c_lbank"], lb)
    tb.poke(lbl["c_laddr"], la & 0xFF)
    tb.poke(lbl["c_laddr"] + 4, la >> 8)
    tb.poke(lbl["c_loop"], loop)
    tb.poke(lbl["c_active"], active)


def consume_model(bank, addr, ebank, eaddr, lbank, laddr, loop, n):
    """Where the read position lands after the mixer has used n bytes.

    Byte-at-a-time, in the order the assembly does it: advance, then wrap if
    the position has REACHED the end -- so a consume that lands exactly on
    loop end comes back at loop start, not at the end.
    """
    active = True
    for _ in range(n):
        addr += 1
        if addr >= WIN_END:
            addr -= 0x2000
            bank += 1
        if (bank, addr) >= (ebank, eaddr):
            if not loop:
                return (bank, addr), False
            bank, addr = lbank, laddr
    return (bank, addr), active


# cache_consume has a fast path for a loop that lies inside one 256-byte page:
# it reduces the offset modulo the loop length instead of walking the loop.
# These cases pin both sides of that branch against the byte-at-a-time model.
# Lengths are deliberately NOT powers of two -- those pages are static and are
# never consumed -- and several start before loop_start or in another page, the
# states that must fall through to the general path.
CONSUME_CASES = [
    #  name                             bank  addr    eb  eaddr   lb  laddr   loop n
    ("20-byte loop, at loop start",       1, 0xA020, 1, 0xA034, 1, 0xA020, 1, 224),
    ("20-byte loop, mid-loop",            1, 0xA02F, 1, 0xA034, 1, 0xA020, 1, 224),
    ("20-byte loop, lands on loop end",   1, 0xA033, 1, 0xA034, 1, 0xA020, 1, 1),
    ("20-byte loop, single byte",         1, 0xA020, 1, 0xA034, 1, 0xA020, 1, 1),
    ("20-byte loop, exactly one period",  1, 0xA025, 1, 0xA034, 1, 0xA020, 1, 20),
    ("50-byte loop, mid-loop",            1, 0xA042, 1, 0xA052, 1, 0xA020, 1, 200),
    ("3-byte loop, many periods",         1, 0xA021, 1, 0xA023, 1, 0xA020, 1, 255),
    # before loop_start, same page: must NOT be reduced modulo the loop
    ("attack before a loop in its page",  1, 0xA000, 1, 0xA034, 1, 0xA020, 1, 224),
    ("attack ending one before the loop", 1, 0xA01F, 1, 0xA034, 1, 0xA020, 1, 1),
    # loop straddling a page boundary inside the bank: general path
    ("loop across a page boundary",       1, 0xA0F0, 1, 0xA110, 1, 0xA0F0, 1, 224),
    # loop crossing the window: general path
    ("loop across the window boundary",   1, 0xBFF0, 2, 0xA010, 1, 0xBFF0, 1, 224),
    # one-shot: no loop at all
    ("one-shot, runs out",                1, 0xA000, 1, 0xA040, 1, 0xA000, 0, 200),
    ("one-shot, room to spare",           1, 0xA000, 1, 0xA100, 1, 0xA000, 0, 200),
]


def main():
    tb = Testbench()
    lbl = tb.load_labels("build/t_cache.lbl")
    tb.load_prg("build/t_cache.prg")

    # Seed only the regions the cases touch; a full 8 KB bank would be 8192
    # round trips per bank.
    for bank, lo, hi in ((1, 0xA000, 0xA180), (1, 0xBF00, 0xC000),
                         (2, 0xA000, 0xA180)):
        tb.set_ram_bank(bank)
        for a in range(lo, hi):
            tb.poke(a, pattern(bank, a))
    tb.set_ram_bank(1)

    tb.run("do_init")
    fails = 0
    for (name, bank, addr, eb, ea, lb, la, loop, active) in CASES:
        poke_state(tb, lbl, bank, addr, eb, ea, lb, la, loop, active)
        tb.run("do_refill")
        got = tb.peek_bytes(lbl["cache0"], 256)
        want, endpos = model(bank, addr, eb, ea, lb, la, loop, active)

        if got == want:
            print("  ok    %s" % name)
        else:
            fails += 1
            i = next(k for k in range(256) if got[k] != want[k])
            print("  FAIL  %s -- first diff at %d: got %02X want %02X"
                  % (name, i, got[i], want[i]))
    for (name, bank, addr, eb, ea, lb, la, loop, n) in CONSUME_CASES:
        poke_state(tb, lbl, bank, addr, eb, ea, lb, la, loop, 1)
        tb.poke(lbl["consume_n"], n)
        tb.run("do_consume")
        got = (tb.peek(lbl["c_bank"]),
               tb.peek(lbl["c_addr"]) | (tb.peek(lbl["c_addr"] + 4) << 8))
        got_active = tb.peek(lbl["c_active"])
        (wb, wa), want_active = consume_model(bank, addr, eb, ea, lb, la, loop, n)

        if got == (wb, wa) and bool(got_active) == want_active:
            print("  ok    consume: %s" % name)
        else:
            fails += 1
            print("  FAIL  consume: %s -- got %d:%04X active=%d,"
                  " want %d:%04X active=%d"
                  % (name, got[0], got[1], got_active, wb, wa, want_active))

    tb.close()
    total = len(CASES) + len(CONSUME_CASES)
    print("\n%d/%d cache cases pass" % (total - fails, total))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
