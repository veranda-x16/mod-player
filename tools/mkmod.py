#!/usr/bin/env python3
"""
Generate small synthetic ProTracker modules for the test suite.

Real modules are fine for integration but terrible for debugging: when a trace
diverges you want a file where exactly one thing is happening. These are built
so that a divergence points at a single behaviour.

Layout (31-instrument ProTracker):
    20      song title
    31*30   sample descriptors (22 name, 2 len/words BE, 1 finetune,
            1 volume, 2 loop start/words BE, 2 loop len/words BE)
    1       song length          1       restart byte
    128     order table          4       signature
    then    patterns, 1024 bytes each (64 rows * 4 ch * 4 bytes)
    then    sample data, concatenated in descriptor order

A note is 4 bytes:
    b0 = sample_hi<<4 | period_hi     b1 = period_lo
    b2 = sample_lo<<4 | effect        b3 = effect parameter
"""
import argparse
import os
import struct

# ProTracker period table, finetune 0, octaves 1-3.
PERIODS = [
    856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
    428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
    214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113,
]
NAMES = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"]
NOTE = {NAMES[i % 12] + str(1 + i // 12): p for i, p in enumerate(PERIODS)}


def note(sample=0, period=0, effect=0, param=0):
    return bytes([
        (sample & 0xF0) | ((period >> 8) & 0x0F),
        period & 0xFF,
        ((sample & 0x0F) << 4) | (effect & 0x0F),
        param & 0xFF,
    ])


EMPTY = note()


class Mod:
    def __init__(self, title="test"):
        self.title = title
        self.samples = []          # (name, pcm bytes, volume, loop_start, loop_len)
        self.patterns = []         # each: list of 64 rows, each 4 notes
        self.order = []

    def add_sample(self, name, pcm, volume=64, loop_start=0, loop_len=0):
        if len(pcm) % 2:
            pcm += b"\x00"
        self.samples.append((name, pcm, volume, loop_start, loop_len))
        return len(self.samples)   # 1-based, as MOD numbers them

    def add_pattern(self, rows):
        pat = [[EMPTY] * 4 for _ in range(64)]
        for r, ch, n in rows:
            pat[r][ch] = n
        self.patterns.append(pat)
        return len(self.patterns) - 1

    def build(self):
        out = bytearray()
        out += self.title.encode()[:20].ljust(20, b"\0")
        for i in range(31):
            if i < len(self.samples):
                name, pcm, vol, ls, ll = self.samples[i]
                out += name.encode()[:22].ljust(22, b"\0")
                out += struct.pack(">H", len(pcm) // 2)
                out += bytes([0, vol])
                out += struct.pack(">HH", ls // 2, max(ll // 2, 1))
            else:
                out += b"\0" * 22 + b"\0\0" + bytes([0, 0]) + b"\0\0\0\1"
        out += bytes([len(self.order), 0x7F])
        order = list(self.order) + [0] * (128 - len(self.order))
        out += bytes(order)
        out += b"M.K."
        for pat in self.patterns:
            for row in pat:
                for n in row:
                    out += n
        for _, pcm, _, _, _ in self.samples:
            out += pcm
        return bytes(out)


def ramp(n=64):
    """A sawtooth: audible, and its value identifies its position exactly."""
    return bytes(((i * 256 // n) - 128) & 0xFF for i in range(n))


def square(n=64):
    return bytes((0x40 if i < n // 2 else 0xC0) for i in range(n))


def build_all(outdir):
    os.makedirs(outdir, exist_ok=True)
    made = []

    def emit(name, mod):
        path = os.path.join(outdir, name + ".mod")
        open(path, "wb").write(mod.build())
        made.append((name, len(mod.build())))

    # one note, one channel -- the smallest thing that plays
    m = Mod("minimal")
    s = m.add_sample("saw", ramp(), 64, 0, 64)
    m.add_pattern([(0, 0, note(s, NOTE["C-2"]))])
    m.order = [0]
    emit("minimal", m)

    # four channels at once -- exercises the full mixer
    m = Mod("four voices")
    s1 = m.add_sample("saw", ramp(), 64, 0, 64)
    s2 = m.add_sample("sqr", square(), 48, 0, 64)
    m.add_pattern([(0, 0, note(s1, NOTE["C-2"])), (0, 1, note(s2, NOTE["E-2"])),
                   (0, 2, note(s1, NOTE["G-2"])), (0, 3, note(s2, NOTE["C-3"]))])
    m.order = [0]
    emit("fourvoice", m)

    # Cxx set volume
    m = Mod("set volume")
    s = m.add_sample("saw", ramp(), 64, 0, 64)
    rows = [(r * 8, 0, note(s, NOTE["C-2"], 0x0C, 64 - r * 8)) for r in range(8)]
    m.add_pattern(rows)
    m.order = [0]
    emit("volume", m)

    # Dxx pattern break: two patterns, the first breaks early
    m = Mod("pattern break")
    s = m.add_sample("saw", ramp(), 64, 0, 64)
    p0 = m.add_pattern([(0, 0, note(s, NOTE["C-2"])),
                        (4, 0, note(s, NOTE["C-2"], 0x0D, 0))])
    p1 = m.add_pattern([(0, 0, note(s, NOTE["C-3"]))])
    m.order = [p0, p1]
    emit("patternbreak", m)

    # Fxx speed change
    m = Mod("tempo")
    s = m.add_sample("saw", ramp(), 64, 0, 64)
    m.add_pattern([(0, 0, note(s, NOTE["C-2"], 0x0F, 3)),
                   (8, 0, note(s, NOTE["C-2"], 0x0F, 6)),
                   (16, 0, note(s, NOTE["C-2"], 0x0F, 0x7D))])
    m.order = [0]
    emit("tempo", m)

    # a looping sample, and a one-shot, side by side
    m = Mod("sample loop")
    lp = m.add_sample("loop", ramp(128), 64, 32, 64)
    one = m.add_sample("oneshot", ramp(96), 64, 0, 0)
    m.add_pattern([(0, 0, note(lp, NOTE["C-2"])), (0, 1, note(one, NOTE["C-2"]))])
    m.order = [0]
    emit("sampleloop", m)

    # multiple patterns and a longer order, for loader bookkeeping
    m = Mod("multi pattern")
    s = m.add_sample("saw", ramp(), 64, 0, 64)
    for k in range(4):
        m.add_pattern([(0, 0, note(s, PERIODS[12 + k * 3]))])
    m.order = [0, 1, 2, 3, 2, 1]
    emit("multipattern", m)

    # --- Phase 7: one module per effect, each isolating a single behaviour ---
    # A divergence in any of these points at exactly one effect.

    def fx(name, title, rows, extra_samples=False):
        m = Mod(title)
        s1 = m.add_sample("saw", ramp(128), 64, 0, 128)
        if extra_samples:
            m.add_sample("sqr", square(64), 48, 0, 64)
        m.add_pattern(rows(s1))
        m.order = [0]
        emit(name, m)

    # 0xy arpeggio: base / +4 / +7 semitones, cycling once per tick
    fx("fx_arpeggio", "arpeggio",
       lambda s: [(0, 0, note(s, NOTE["C-2"], 0x00, 0x47)),
                  (8, 0, note(s, NOTE["E-2"], 0x00, 0x37)),
                  (16, 0, note(s, NOTE["C-2"], 0x00, 0x00))])

    # 1xx / 2xx portamento up and down
    fx("fx_porta", "portamento",
       lambda s: [(0, 0, note(s, NOTE["C-2"])),
                  (1, 0, note(0, 0, 0x01, 8)),
                  (8, 0, note(0, 0, 0x02, 12)),
                  (16, 0, note(0, 0, 0x01, 1)),
                  (24, 0, note(0, 0, 0x02, 255))])

    # 3xx tone portamento, then 5xy tone porta + volume slide
    fx("fx_toneporta", "tone porta",
       lambda s: [(0, 0, note(s, NOTE["C-2"])),
                  (4, 0, note(s, NOTE["G-2"], 0x03, 4)),
                  (8, 0, note(0, 0, 0x03, 0)),
                  (16, 0, note(s, NOTE["C-2"], 0x05, 0x20)),
                  (24, 0, note(0, 0, 0x05, 0x02))])

    # 4xy vibrato, then 6xy vibrato + volume slide
    fx("fx_vibrato", "vibrato",
       lambda s: [(0, 0, note(s, NOTE["C-2"], 0x04, 0x48)),
                  (8, 0, note(0, 0, 0x04, 0x00)),
                  (16, 0, note(0, 0, 0x06, 0x0F)),
                  (24, 0, note(s, NOTE["C-2"], 0x04, 0x8F))])

    # 7xy tremolo
    fx("fx_tremolo", "tremolo",
       lambda s: [(0, 0, note(s, NOTE["C-2"], 0x07, 0x48)),
                  (8, 0, note(0, 0, 0x07, 0x00)),
                  (16, 0, note(s, NOTE["C-2"], 0x07, 0x8F))])

    # Axy volume slide, both directions, and clamping at 0 and 64
    fx("fx_volslide", "vol slide",
       lambda s: [(0, 0, note(s, NOTE["C-2"], 0x0C, 32)),
                  (1, 0, note(0, 0, 0x0A, 0x40)),
                  (8, 0, note(0, 0, 0x0A, 0x04)),
                  (16, 0, note(0, 0, 0x0A, 0x0F)),
                  (24, 0, note(0, 0, 0x0A, 0xF0))])

    # 9xx sample offset
    fx("fx_offset", "offset",
       lambda s: [(0, 0, note(s, NOTE["C-2"], 0x09, 0x10)),
                  (8, 0, note(s, NOTE["C-2"], 0x09, 0x40)),
                  (16, 0, note(s, NOTE["C-2"], 0x09, 0xFF))])

    # E1x / E2x fine portamento, EAx / EBx fine volume
    fx("fx_fine", "fine slides",
       lambda s: [(0, 0, note(s, NOTE["C-2"], 0x0E, 0x1F)),
                  (4, 0, note(0, 0, 0x0E, 0x2F)),
                  (8, 0, note(0, 0, 0x0E, 0xA8)),
                  (12, 0, note(0, 0, 0x0E, 0xB4)),
                  (16, 0, note(0, 0, 0x0E, 0xAF)),
                  (20, 0, note(0, 0, 0x0E, 0xBF))])

    # E4x / E7x waveform selection, including the random one
    fx("fx_waveform", "waveforms",
       lambda s: [(0, 0, note(s, NOTE["C-2"], 0x0E, 0x41)),
                  (1, 0, note(0, 0, 0x04, 0x48)),
                  (8, 0, note(0, 0, 0x0E, 0x42)),
                  (9, 0, note(0, 0, 0x04, 0x48)),
                  (16, 0, note(0, 0, 0x0E, 0x43)),
                  (17, 0, note(0, 0, 0x04, 0x48)),
                  (24, 0, note(0, 0, 0x0E, 0x72)),
                  (25, 0, note(0, 0, 0x07, 0x48))])

    # E6x pattern loop: mark at row 2, loop back twice from row 6
    fx("fx_patternloop", "pattern loop",
       lambda s: [(0, 0, note(s, NOTE["C-2"])),
                  (2, 0, note(0, 0, 0x0E, 0x60)),
                  (6, 0, note(0, 0, 0x0E, 0x62)),
                  (10, 0, note(s, NOTE["E-2"]))])

    # E9x retrigger, ECx note cut, EDx note delay
    fx("fx_retrig", "retrig cut delay",
       lambda s: [(0, 0, note(s, NOTE["C-2"], 0x0E, 0x93)),
                  (4, 0, note(s, NOTE["C-2"], 0x0E, 0xC3)),
                  (8, 0, note(s, NOTE["C-2"], 0x0E, 0xD3)),
                  (12, 0, note(s, NOTE["C-2"], 0x0E, 0xD0))])

    # EEx pattern delay
    fx("fx_patdelay", "pattern delay",
       lambda s: [(0, 0, note(s, NOTE["C-2"])),
                  (2, 0, note(0, 0, 0x0E, 0xE2)),
                  (4, 0, note(s, NOTE["E-2"])),
                  (6, 0, note(0, 0, 0x0E, 0xE0))])

    # Bxx position jump across two patterns
    m = Mod("pos jump")
    s1 = m.add_sample("saw", ramp(128), 64, 0, 128)
    p0 = m.add_pattern([(0, 0, note(s1, NOTE["C-2"])),
                        (4, 0, note(0, 0, 0x0B, 1))])
    p1 = m.add_pattern([(0, 0, note(s1, NOTE["G-2"])),
                        (4, 0, note(0, 0, 0x0D, 0))])
    m.order = [p0, p1]
    emit("fx_posjump", m)

    # E5x finetune, across the full signed range
    m = Mod("finetune")
    a = m.add_sample("ft0", ramp(64), 64, 0, 64)
    m.add_pattern([(0, 0, note(a, NOTE["C-2"], 0x0E, 0x50)),
                   (4, 0, note(a, NOTE["C-2"], 0x0E, 0x57)),
                   (8, 0, note(a, NOTE["C-2"], 0x0E, 0x58)),
                   (12, 0, note(a, NOTE["C-2"], 0x0E, 0x5F))])
    m.order = [0]
    emit("fx_finetune", m)

    return made



if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="tests/mods")
    a = ap.parse_args()
    for name, size in build_all(a.out):
        print("  %-16s %6d bytes" % (name + ".mod", size))
