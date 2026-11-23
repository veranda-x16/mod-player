#!/usr/bin/env python3
"""
Phase 5 gate: the MOD loader.

  1. Every field the parser reports must agree with an independent player
     (openmpt123/libopenmpt) on the whole synthetic corpus.
  2. Header + patterns + sample data must account for the file exactly. A
     wrong pattern count is the classic MOD misparse and this catches it.
  3. Malformed files must be REJECTED, not crash or silently misparse.
"""
import os
import re
import subprocess
import sys

MODS = "tests/mods"
INFO = "./build/host_modinfo"


def ours(path):
    r = subprocess.run([INFO, path], capture_output=True, text=True)
    d = {}
    for line in r.stdout.splitlines():
        if ":" in line:
            k, v = line.split(":", 1)
            d[k.strip()] = v.strip()
    return d, r.returncode


def theirs(path):
    r = subprocess.run(["openmpt123", "--info", path],
                       capture_output=True, text=True, timeout=30)
    d = {}
    for line in r.stdout.splitlines():
        m = re.match(r"([A-Za-z]+)\.*\s*:\s*(.*)", line.strip())
        if m:
            d[m.group(1)] = m.group(2).strip()
    return d


def main():
    fails = 0
    mods = sorted(f for f in os.listdir(MODS) if f.endswith(".mod"))
    print("cross-check against libopenmpt (%d modules)" % len(mods))
    print("  %-18s %-14s %8s %8s %10s" % ("module", "title", "patterns", "orders", "size"))
    for f in mods:
        p = os.path.join(MODS, f)
        o, rc = ours(p)
        t = theirs(p)
        problems = []
        if rc != 0:
            problems.append("parser rc=%d" % rc)
        if o.get("Title") != t.get("Title"):
            problems.append("title %r vs %r" % (o.get("Title"), t.get("Title")))
        if o.get("Patterns") != t.get("Patterns"):
            problems.append("patterns %s vs %s" % (o.get("Patterns"), t.get("Patterns")))
        if o.get("Length") != t.get("Orders"):
            problems.append("orders %s vs %s" % (o.get("Length"), t.get("Orders")))
        if "(exact)" not in o.get("Accounted", ""):
            problems.append("size accounting: " + o.get("Accounted", "?"))
        status = "ok" if not problems else "FAIL " + "; ".join(problems)
        print("  %-18s %-14s %8s %8s %10s  %s"
              % (f, o.get("Title", "?"), o.get("Patterns", "?"),
                 o.get("Length", "?"), o.get("FileSize", "?"), status))
        if problems:
            fails += 1

    print("\nmalformed input must be rejected, not crash")
    good = open(os.path.join(MODS, "minimal.mod"), "rb").read()
    cases = [
        ("truncated to 100 bytes", good[:100]),
        ("truncated mid-header",   good[:1083]),
        ("bad signature",          good[:1080] + b"XXXX" + good[1084:]),
        ("song length 0",          good[:950] + bytes([0]) + good[951:]),
        ("song length 200",        good[:950] + bytes([200]) + good[951:]),
        ("empty file",             b""),
    ]
    os.makedirs("build/bad", exist_ok=True)
    for name, blob in cases:
        p = "build/bad/x.mod"
        open(p, "wb").write(blob)
        r = subprocess.run([INFO, p], capture_output=True, text=True, timeout=10)
        rejected = r.returncode == 1 and r.stdout.startswith("error:")
        crashed = r.returncode < 0
        print("  %-24s %s" % (name,
              "ok (%s)" % r.stdout.strip().replace("error: ", "")
              if rejected else ("CRASHED" if crashed else "NOT REJECTED")))
        if not rejected:
            fails += 1

    print("\n%s" % ("Phase 5 loader gate: PASS" if fails == 0
                    else "Phase 5 loader gate: %d FAILURES" % fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
