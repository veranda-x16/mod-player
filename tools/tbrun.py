"""
Testbench driver for x16-mod.

Wraps the x16emu -testbench protocol with the things this project actually
needs: load a linked PRG by poking it in, address code by symbol name, run it,
read results back, and report cycle counts from the emulator's guest-readable
counter at $9FB8-$9FBB.

Two traps this module exists to hide (see docs/environment.md):
  * -prg does NOT load in testbench mode (it is a BASIC paste), so PRGs are
    poked in with STM instead. Measured ~105 KB/s, which is fast enough.
  * The PCM FIFO never drains in testbench mode (no audio device), so a test
    that polls FIFO-full will deadlock. Render to RAM here; use the windowed
    emulator for real playback.
"""

import os
import re
import subprocess

EMU = os.environ.get("X16EMU", os.path.expanduser("~/Projects/x16-emulator/build/x16emu"))
ROM = os.environ.get("X16ROM", os.path.expanduser("~/Projects/x16-rom/build/x16/rom.bin"))


class TestbenchError(RuntimeError):
    pass


class Testbench:
    def __init__(self, extra_args=None, timeout=60):
        args = [EMU, "-testbench", "-rom", ROM]
        if extra_args:
            args += extra_args
        self.timeout = timeout
        self.p = subprocess.Popen(
            args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            bufsize=0,
        )
        self.labels = {}
        self._wait_ready()

    # -- plumbing ---------------------------------------------------------
    def _line(self):
        ln = self.p.stdout.readline()
        if not ln:
            raise TestbenchError("emulator closed the pipe")
        return ln.decode("utf-8", "replace").strip()

    def _cmd(self, s):
        self.p.stdin.write((s + "\n").encode())
        self.p.stdin.flush()

    def _wait_ready(self):
        while True:
            r = self._line()
            if r.startswith("RDY"):
                return
            if r.startswith("ERR"):
                raise TestbenchError(r)

    def _value(self):
        while True:
            r = self._line()
            if r.startswith("ERR"):
                raise TestbenchError(r)
            if r:
                return int(r, 16)

    # -- state ------------------------------------------------------------
    def set_ram_bank(self, b):
        self._cmd("RAM %02X" % b)
        self._wait_ready()

    def poke(self, addr, val):
        self._cmd("STM %04X %02X" % (addr, val & 0xFF))
        self._wait_ready()

    def peek(self, addr):
        self._cmd("RQM %04X" % addr)
        return self._value()

    def poke_bytes(self, addr, data):
        for i, b in enumerate(data):
            self.poke(addr + i, b)

    def peek_bytes(self, addr, length):
        return bytes(self.peek(addr + i) for i in range(length))

    def run(self, addr):
        if isinstance(addr, str):
            addr = self.sym(addr)
        self._cmd("RUN %04X" % addr)
        self._wait_ready()

    # -- symbols ----------------------------------------------------------
    def load_labels(self, path):
        """Read a ld65 -Ln VICE label file: 'al 00C0DE .name'."""
        with open(path) as f:
            for line in f:
                m = re.match(r"al\s+([0-9A-Fa-f]+)\s+\.(\S+)", line.strip())
                if m:
                    self.labels[m.group(2)] = int(m.group(1), 16)
        return self.labels

    def sym(self, name):
        if name not in self.labels:
            raise KeyError("unknown symbol %r" % name)
        return self.labels[name]

    # -- program loading --------------------------------------------------
    def load_prg(self, path):
        """Poke a PRG into memory at its own load address. Returns that address."""
        with open(path, "rb") as f:
            blob = f.read()
        load_addr = blob[0] | (blob[1] << 8)
        self.poke_bytes(load_addr, blob[2:])
        return load_addr

    # -- cycle counter ----------------------------------------------------
    def cycles(self):
        """Read the 32-bit snapshot the guest left at $9FB8-$9FBB.

        Reading $9FB8 takes the snapshot; $9FB9-$9FBB return the rest of the
        SAME snapshot, so the value is coherent.
        """
        return (self.peek(0x9FB8) | (self.peek(0x9FB9) << 8) |
                (self.peek(0x9FBA) << 16) | (self.peek(0x9FBB) << 24))

    def close(self):
        try:
            self.p.stdin.close()
            self.p.wait(timeout=5)
        except Exception:
            self.p.kill()

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()
