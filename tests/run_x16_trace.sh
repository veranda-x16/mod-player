#!/bin/sh
# Compare the X16 build's sequencer trace against a native build of the SAME
# source. A difference here is a porting bug (cc65 codegen, integer width,
# banked pattern reads); ProTracker-semantics bugs are caught separately by
# tools/cmp_trace.py against micromod.
set -e
cd "$(dirname "$0")/.."
MOD=${1:-tests/mods/volume.mod}
TICKS=${2:-200}
EMU=${X16EMU:-$HOME/Projects/x16-emulator/build/x16emu}
ROM=${X16ROM:-$HOME/Projects/x16-rom/build/x16/rom.bin}

# TRACE_TICKS is a compile-time define, and make does not consider a target
# dirty when a flag changes -- it would happily reuse a binary built for a
# different tick count. Force the rebuild.
rm -f build/TRACE.PRG
make -s build/TRACE.PRG TRACE_TICKS="$TICKS" >/dev/null

mkdir -p build/fsroot
cp build/TRACE.PRG build/fsroot/
rm -f build/fsroot/*.mod build/fsroot/*.MOD
cp "$MOD" build/fsroot/music.mod
rm -f build/fsroot/tr.txt

# The emulator has no guest-triggered quit, so waiting on it always costs the
# full timeout. Poll for the expected line count instead and stop as soon as
# the trace is complete.
# Started WITHOUT a wrapping subshell: backgrounding "( cd d && emu )" makes
# $! the subshell, so the kill below leaves the emulator orphaned -- and it
# spins at warp for as long as the session lasts, stealing a core from every
# later test. So cd, launch, and cd back -- $! is then the emulator itself.
cd build/fsroot
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    "$EMU" -rom "$ROM" -prg TRACE.PRG -run -warp -echo > tr.txt 2>&1 &
emupid=$!
cd ../..
i=0
while [ "$i" -lt 120 ]; do
    # grep -c prints 0 AND exits non-zero on no match, so a "|| echo 0"
    # fallback yields "0\n0". Count through wc instead, which always succeeds.
    n=$(grep -E '^[0-9]+ [0-9-]+ ' build/fsroot/tr.txt 2>/dev/null | wc -l)
    [ "$n" -ge "$TICKS" ] && break
    kill -0 "$emupid" 2>/dev/null || break
    sleep 1
    i=$((i + 1))
done
kill "$emupid" 2>/dev/null || true
wait "$emupid" 2>/dev/null || true

grep -E '^[0-9]+ [0-9-]+ ' build/fsroot/tr.txt > build/x16-trace.txt || true
./build/host_seq "$MOD" "$TICKS" > build/host-trace.txt

n=$(wc -l < build/x16-trace.txt)
if [ "$n" -ne "$TICKS" ]; then
    echo "  FAIL: X16 produced $n trace lines, expected $TICKS"
    exit 1
fi
if diff -q build/x16-trace.txt build/host-trace.txt >/dev/null; then
    echo "  X16 trace == host trace ($TICKS ticks, $(basename "$MOD")): PASS"
else
    echo "  FAIL: X16 and host traces differ"
    diff build/x16-trace.txt build/host-trace.txt | head -6
    exit 1
fi
