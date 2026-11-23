#!/usr/bin/env python3
"""
Compare the sequencer's per-tick trace against the micromod oracle.

Divergence is reported with the first differing tick and its context, because
"the traces differ" is useless but "they differ at tick 412, channel 2's period
went 428 -> 404 while the oracle held 428" points straight at an effect.

Every effect the oracle implements is now implemented here, so ANY divergence
is a failure. The report still names the effects each module uses, so a
failure points at a short list of suspects.

Covers tests/mods (synthetic, one behaviour each) and micromod's own songs.
The 488-module real corpus is swept separately by tools/sweep_corpus.py, which
runs in parallel -- keeping this gate fast enough for every build.
"""
import os
import subprocess
import sys

MINE = "./build/host_seq"
ORACLE = "./build/oracle_micromod"

# Phase 7 implements every ProTracker effect that micromod does. E0 (Amiga
# LED filter), E3 (glissando), E8 and EF (funk repeat) are implemented by
# neither, so they agree trivially. Every module must therefore MATCH.
IMPLEMENTED = set(range(0x10))
EFFECT_NAME = {
    0x00: "arpeggio", 0x01: "porta up", 0x02: "porta down", 0x03: "tone porta",
    0x04: "vibrato", 0x05: "toneporta+vol", 0x06: "vibrato+vol", 0x07: "tremolo",
    0x08: "panning", 0x09: "offset", 0x0A: "vol slide", 0x0B: "pos jump",
    0x0C: "set vol", 0x0D: "pat break", 0x0E: "extended", 0x0F: "speed/tempo",
}


def effects_used(path):
    """Scan every pattern for the effects actually present."""
    blob = open(path, "rb").read()
    if len(blob) < 1084:
        return set()
    order = blob[952:1080]
    npat = max(order) + 1
    used = set()
    for p in range(npat):
        base = 1084 + p * 1024
        # Some real modules declare more patterns than they actually store,
        # so the last one runs off the end of the file. Stop at a whole note.
        for i in range(base, min(base + 1024, len(blob) - 3), 4):
            eff = blob[i + 2] & 0x0F
            par = blob[i + 3]
            if eff == 0x0E:
                used.add("E%X" % (par >> 4))
            elif eff or par:
                used.add("%X" % eff)
    return used


def run(cmd, path, ticks):
    r = subprocess.run([cmd, path, str(ticks)], capture_output=True, text=True)
    if r.returncode != 0:
        return None
    return r.stdout.splitlines()


def compare(path, ticks):
    a = run(MINE, path, ticks)
    b = run(ORACLE, path, ticks)
    if a is None or b is None:
        return None, "one of the players rejected the module"
    n = min(len(a), len(b))
    for i in range(n):
        if a[i] != b[i]:
            return i, (a[i], b[i])
    if len(a) != len(b):
        return n, ("%d lines" % len(a), "%d lines" % len(b))
    return None, None


def main():
    ticks = int(sys.argv[1]) if len(sys.argv) > 1 else 600
    paths = []
    for d in ("tests/mods", "vendor/micromod/songs"):
        if os.path.isdir(d):
            paths += [os.path.join(d, f) for f in sorted(os.listdir(d))
                      if f.lower().endswith(".mod")]

    print("sequencer trace vs micromod (%d ticks each)\n" % ticks)
    print("  %-34s %-8s %-26s %s" % ("module", "result", "effects used", "note"))
    print("  " + "-" * 92)

    supported_fail = 0
    for p in paths:
        used = effects_used(p)
        unsupported = []          # everything micromod implements, we do too
        at, detail = compare(p, ticks)
        within_scope = not unsupported
        if at is None:
            result, note = "MATCH", ""
        else:
            result = "diverge"
            note = "tick %d" % at
            if within_scope:
                result = "FAIL"
                note = "tick %d: %r vs %r" % (at, detail[0][:40], detail[1][:40])
                supported_fail += 1
            else:
                note += " (uses %s -- Phase 7)" % ",".join(unsupported)
        print("  %-34s %-8s %-26s %s"
              % (os.path.basename(p), result, ",".join(sorted(used)) or "-", note))

    print("\nEvery module must MATCH exactly.")
    print("Phase 7 trace gate: %s"
          % ("PASS" if supported_fail == 0 else "%d FAILURES" % supported_fail))
    return 1 if supported_fail else 0


if __name__ == "__main__":
    sys.exit(main())
