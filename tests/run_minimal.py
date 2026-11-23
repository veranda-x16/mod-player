#!/usr/bin/env python3
"""
examples/minimal against the library alone: it loads a module and plays it,
polled, and writes a '.' to the emulator's stdout for every batch rendered.
Real time with the dummy audio driver (it drains the FIFO; -sound none would
not), -warp: the drain follows emulated time. PASS: at least 100 batches,
i.e. about 6.4 s of audio delivered.
"""
import os, shutil, subprocess, sys, tempfile, time

EMU = os.environ.get("X16EMU", os.path.expanduser("~/Projects/x16-emulator/build/x16emu"))
ROM = os.environ.get("X16ROM", os.path.expanduser("~/Projects/x16-rom/build/x16/rom.bin"))
d = tempfile.mkdtemp()
try:
    shutil.copy("build/MINIMAL.PRG", os.path.join(d, "M.PRG"))
    shutil.copy("tests/mods/fourvoice.mod", os.path.join(d, "music.mod"))
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    out = os.path.join(d, "out.txt")
    with open(out, "wb") as f:
        p = subprocess.Popen([EMU, "-rom", ROM, "-prg", "M.PRG", "-run", "-warp"],
                             cwd=d, stdout=f, stderr=subprocess.STDOUT, env=env)
        deadline = time.time() + 120
        n = 0
        while time.time() < deadline and p.poll() is None:
            time.sleep(1)
            n = open(out, "rb").read().count(b".")
            if n >= 100:
                break
        p.kill(); p.wait()
    print("  minimal: %d batches" % n)
    sys.exit(0 if n >= 100 else 1)
finally:
    shutil.rmtree(d, ignore_errors=True)
