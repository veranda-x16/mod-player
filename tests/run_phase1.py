#!/usr/bin/env python3
"""
Phase 1 gate: what does keeping the VERA PCM FIFO fed actually cost?

Runs under -testbench, where the FIFO never drains and the cycle counter is
exact. Every loop writes to AUDIO_DATA unconditionally, so nothing here can
deadlock on a full FIFO. Sustained-playback / underrun testing is a separate
rig (tests/run_phase1_realtime.sh) because it needs a real audio device.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
from tbrun import Testbench  # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..")
PRG = os.path.join(ROOT, "build", "t_phase1.prg")
LBL = os.path.join(ROOT, "build", "t_phase1.lbl")

CPU_HZ = 8_000_000
VERA_RATE_UNIT = 25_000_000 / 512 / 128        # 381.4697 Hz per AUDIO_RATE step

RATES = [
    ("8 kHz",  21), ("12 kHz", 32), ("16 kHz", 42),
    ("18 kHz", 48), ("21 kHz", 56), ("24 kHz", 64), ("32 kHz", 84),
]

BENCHES = [
    ("sta AUDIO_DATA only",  "bench_sta_only",        256),
    ("copy loop (lda/sta)",  "bench_fill_simple",     256),
    ("8x unrolled",          "bench_fill_unrolled",   256),
    ("16x unrolled",         "bench_fill_unrolled16", 256),
]


def main():
    tb = Testbench()
    tb.load_labels(LBL)
    tb.load_prg(PRG)

    tb.run("bench_null")
    overhead = tb.cycles()
    print("cycle-counter bracket overhead: %d cycles (subtracted below)\n" % overhead)

    print("FIFO fill cost, 256 bytes per run")
    print("%-24s %10s %12s" % ("variant", "cycles", "per byte"))
    print("-" * 48)
    results = {}
    for label, sym, nbytes in BENCHES:
        tb.run(sym)
        cyc = tb.cycles() - overhead
        results[sym] = cyc / nbytes
        print("%-24s %10d %12.2f" % (label, cyc, cyc / nbytes))

    best = min(results.values())
    print("\nBest fill cost: %.2f cycles/byte -> %.2f cycles per stereo frame\n"
          % (best, best * 2))

    print("Budget per stereo frame at 8 MHz, and what the fill alone consumes")
    print("%-10s %10s %12s %10s %10s" %
          ("rate", "Hz", "cycles/frame", "fill", "left"))
    print("-" * 56)
    for name, rv in RATES:
        hz = rv * VERA_RATE_UNIT
        budget = CPU_HZ / hz
        fill = best * 2
        print("%-10s %10.0f %12.1f %9.1f%% %9.1f%%"
              % (name, hz, budget, 100 * fill / budget, 100 * (1 - fill / budget)))

    tb.close()


if __name__ == "__main__":
    main()
