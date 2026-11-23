#!/usr/bin/env python3
"""
Run the whole real-module corpus through both players and compare traces.

This is the integration test the synthetic suite cannot be: those modules were
written by the same person who wrote the player and share its assumptions.
Real modules carry the quirks trackers actually emitted.

Reports, per module: agreement, or the first differing tick with context.
Modules both players reject are counted separately -- that is agreement too.
"""
import os
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor

MINE = "./build/host_seq"
ORACLE = "./build/oracle_micromod"
ROOT = "tests/mods/real"


def run(cmd, path, ticks):
    try:
        r = subprocess.run([cmd, path, str(ticks)], capture_output=True,
                           text=True, timeout=120)
    except subprocess.TimeoutExpired:
        return "TIMEOUT"
    if r.returncode != 0:
        return "REJECT"
    return r.stdout


def check(args):
    path, ticks = args
    a = run(MINE, path, ticks)
    b = run(ORACLE, path, ticks)
    if a in ("REJECT", "TIMEOUT") or b in ("REJECT", "TIMEOUT"):
        if a == b:
            return (path, "both-reject", "")
        return (path, "DISAGREE-ACCEPT", "ours=%s oracle=%s" % (a[:12], b[:12]))
    la, lb = a.splitlines(), b.splitlines()
    for i in range(min(len(la), len(lb))):
        if la[i] != lb[i]:
            return (path, "DIVERGE", "tick %d\n      ours   %s\n      oracle %s"
                    % (i, la[i][:110], lb[i][:110]))
    if len(la) != len(lb):
        return (path, "DIVERGE", "line count %d vs %d" % (len(la), len(lb)))
    return (path, "match", "")


def main():
    ticks = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
    show = int(sys.argv[2]) if len(sys.argv) > 2 else 12

    paths = []
    for dirpath, _, files in os.walk(ROOT):
        for f in sorted(files):
            if f.lower().endswith(".mod"):
                paths.append(os.path.join(dirpath, f))
    paths.sort()
    print("sweeping %d modules at %d ticks each\n" % (len(paths), ticks))

    with ProcessPoolExecutor() as ex:
        results = list(ex.map(check, [(p, ticks) for p in paths]))

    buckets = {}
    for path, status, detail in results:
        buckets.setdefault(status, []).append((path, detail))

    for status in ("match", "both-reject", "DIVERGE", "DISAGREE-ACCEPT"):
        rows = buckets.get(status, [])
        print("%-16s %d" % (status, len(rows)))

    for status in ("DIVERGE", "DISAGREE-ACCEPT"):
        rows = buckets.get(status, [])
        if rows:
            print("\n--- %s (showing %d of %d) ---" % (status, min(show, len(rows)), len(rows)))
            for path, detail in rows[:show]:
                print("  %s\n      %s" % (os.path.basename(path), detail))

    bad = len(buckets.get("DIVERGE", [])) + len(buckets.get("DISAGREE-ACCEPT", []))
    print("\ncorpus gate: %s" % ("PASS" if bad == 0 else "%d modules differ" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
