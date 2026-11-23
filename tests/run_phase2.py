#!/usr/bin/env python3
"""
Phase 2 gate: the 4-voice mixer must be fast enough AND exactly right.

  cost        -- cycles/frame from the FIFO build, under -testbench
  correctness -- the MIX_TO_BUF build rendered against host/host_mixer.c,
                 byte for byte, on random vectors

Random vectors deliberately include cache values outside the -64..63 range the
real cache uses, so that the 8-bit wraparound in the mix is exercised and the
C model cannot get away with wider arithmetic.
"""
import os
import random
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
from tbrun import Testbench  # noqa: E402

CPU_HZ = 8_000_000
UNIT = 25_000_000 / 512 / 128
RATES = [("16 kHz", 42), ("18 kHz", 48), ("21 kHz", 56),
         ("24 kHz", 64), ("32 kHz", 84)]
VECTORS = 8
GATE_RATE, GATE_HEADROOM = 42, 40.0


def bench():
    tb = Testbench()
    tb.load_labels("build/t_phase2.lbl")
    tb.load_prg("build/t_phase2.prg")
    tb.run("bench_null")
    oh = tb.cycles()
    tb.run("bench_mix")
    per = (tb.cycles() - oh) / 256.0
    tb.close()
    return per


def correctness():
    tb = Testbench()
    lbl = tb.load_labels("build/t_phase2b.lbl")
    tb.load_prg("build/t_phase2b.prg")
    caches = [lbl["cache0"], lbl["cache1"], lbl["cache2"], lbl["cache3"]]
    s0f, v0f, mixbuf, vols = lbl["s0f"], lbl["v0f"], lbl["mixbuf"], lbl["vols"]

    rng = random.Random(0xC0FFEE)
    failures = 0
    for n in range(VECTORS):
        data = [bytes(rng.randrange(256) for _ in range(256)) for _ in range(4)]
        steps = []
        for _ in range(4):
            f = rng.uniform(0.25, 2.0)          # the real step range
            steps.append(int(f * 65536) & 0xFFFFFF)

        for c, d in zip(caches, data):
            tb.poke_bytes(c, d)
        sb = b"".join(bytes([s & 0xFF, (s >> 8) & 0xFF, (s >> 16) & 0xFF])
                      for s in steps)
        tb.poke_bytes(s0f, sb)
        tb.poke_bytes(v0f, bytes(12))           # phases start at zero
        vb = bytes(rng.randrange(0, 65) for _ in range(4))
        tb.poke_bytes(vols, vb)
        tb.run("set_volumes")
        tb.run("run_mix")
        got = tb.peek_bytes(mixbuf, 256)

        model = subprocess.run(["build/host_mixer"], input=b"".join(data) + sb + vb,
                               capture_output=True, check=True).stdout
        if got != model:
            failures += 1
            bad = next(i for i in range(256) if got[i] != model[i])
            print("  vector %d MISMATCH at byte %d: x16=%02X model=%02X"
                  % (n, bad, got[bad], model[bad]))
        else:
            print("  vector %d ok" % n)
    tb.close()
    return failures


def main():
    print("Phase 2 -- 4-voice stereo mixer\n")
    per = bench()
    print("cost: %.2f cycles per stereo frame\n" % per)
    print("%-8s %10s %10s %10s" % ("rate", "budget", "mixer", "headroom"))
    print("-" * 42)
    headroom_at_gate = None
    for name, rv in RATES:
        budget = CPU_HZ / (rv * UNIT)
        head = 100 * (1 - per / budget)
        if rv == GATE_RATE:
            headroom_at_gate = head
        print("%-8s %10.1f %9.1f%% %9.1f%%" % (name, budget, 100 * per / budget, head))

    print("\ncorrectness vs host/host_mixer.c:")
    failures = correctness()

    print("\n--- gate ---")
    ok_speed = headroom_at_gate >= GATE_HEADROOM
    print("headroom at 16 kHz: %.1f%% (need >= %.0f%%)  %s"
          % (headroom_at_gate, GATE_HEADROOM, "PASS" if ok_speed else "FAIL"))
    print("bit-exact vs C model: %d/%d vectors  %s"
          % (VECTORS - failures, VECTORS, "PASS" if failures == 0 else "FAIL"))
    return 0 if (ok_speed and failures == 0) else 1


if __name__ == "__main__":
    sys.exit(main())
