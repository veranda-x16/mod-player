#!/usr/bin/env python3
"""
Repair a WAV the emulator was killed while writing.

x16emu only finalises the RIFF/data sizes on a clean exit, and it has no
guest-triggered quit -- so a timeout-killed capture has a valid 44-byte header
with zero lengths and perfectly good PCM after it.
"""
import struct
import sys

path = sys.argv[1]
data = open(path, "rb").read()
pcm = len(data) - 44
if pcm <= 0:
    sys.exit("no PCM payload in %s" % path)
out = bytearray(data)
struct.pack_into("<I", out, 4, 36 + pcm)      # RIFF chunk size
struct.pack_into("<I", out, 40, pcm)          # data chunk size
open(path, "wb").write(out)
print("%s: %d bytes PCM (%.2f s at 48 kHz stereo)" % (path, pcm, pcm / 4 / 48000))
