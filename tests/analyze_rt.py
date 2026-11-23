#!/usr/bin/env python3
"""
Analyse the Phase 1 load sweep (tests/t_rt.s output).

Each line is: <rate> <burn> <cycles> <underruns>, all hex, where <cycles>
covers BLOCKS*512 stereo frames. For each rate the largest per-frame load that
still produced zero underruns is the measured cycle budget -- the number
Phase 2's mixer has to fit inside.
"""
import sys

BLOCKS = 255
FRAMES_PER_BLOCK = 512
FRAMES = BLOCKS * FRAMES_PER_BLOCK
CPU_HZ = 8_000_000
UNIT = 25_000_000 / 512 / 128

rows = []
for line in open(sys.argv[1] if len(sys.argv) > 1 else "build/rt.log"):
    f = line.split()
    if len(f) != 4:
        continue
    try:
        rate, burn, cyc, under = (int(x, 16) for x in f)
    except ValueError:
        continue
    rows.append((rate, burn, cyc / FRAMES, under))

if not rows:
    sys.exit("no parseable rows")

rates = sorted({r[0] for r in rows})
print("%-8s %9s %11s %13s %11s %8s" %
      ("rate", "Hz", "theoretical", "max measured", "underruns", "match"))
print("-" * 66)
for rate in rates:
    hz = rate * UNIT
    theo = CPU_HZ / hz
    good = [r for r in rows if r[0] == rate and r[3] == 0]
    bad = [r for r in rows if r[0] == rate and r[3] > 0]
    if good:
        best = max(good, key=lambda r: r[2])
        first_bad = min(bad, key=lambda r: r[2])[2] if bad else float("inf")
        note = "ok" if best[2] <= theo * 1.02 else "OVER"
        print("%-8s %9.0f %11.1f %13.1f %11s %8s" %
              ("$%02X" % rate, hz, theo, best[2],
               "from %.0f" % first_bad if bad else "none", note))
    else:
        print("%-8s %9.0f %11.1f %13s %11s %8s" %
              ("$%02X" % rate, hz, theo, "-", "always", "FAIL"))

print()
print("Rows per rate (cycles/frame -> underruns):")
for rate in rates:
    rs = sorted([r for r in rows if r[0] == rate], key=lambda r: r[2])
    print("  $%02X: %s" % (rate, "  ".join("%.0f:%d" % (r[2], r[3]) for r in rs)))
