#!/bin/bash
# Real-time playback under Veranda with the system busy (make veranda-rt; M3).
#
#   tools/gate_veranda_rt.sh          (VSD=0: skip the SD card image;
#                                      RT_SECONDS: music per run, default 180)
#
# MODSTRESS.cvt (examples/veranda/test/modstress.c) plays DIZZYCRS and
# Kaffe Og Kage from VERA's AFLOW interrupt (intFirstVector) for
# RT_SECONDS each while a script opens the "file" menu, picks "about" and
# presses the dialog box's OK every ten seconds, and the main loop copies
# half the screen (CopyRect), writes a PutCards screen, lists the folder and
# reads 64 KB, in turns. PASS: 0 underruns (chunks that found the FIFO
# empty), at least one dialog box a 15 s, and every kind of work done. The
# batches' cycles a frame (the engine plus the frame interrupts nested in
# its batches) and the slowdown of CopyRect and PutCards are logged.
# Sound on through SDL's dummy driver: under -sound none the FIFO never
# drains and AFLOW never fires.
set -e
cd "$(dirname "$0")/.."
export CC65_HOME=${CC65_HOME:-$HOME/.local/share/cc65}
EMU=${X16EMU:-$HOME/Projects/x16-emulator/build/x16emu}
ROM=${X16ROM:-$HOME/Projects/x16-rom/build/x16/rom.bin}
VSDK=${VSDK:-build/vsdk}
SECS=${RT_SECONDS:-180}
OUT=build/gate-veranda-rt
PY="python3 -I -B"
ok() { echo "ok    $*"; }
fail() { echo "FAIL  $*"; exit 1; }
[ -f "$VSDK/bin/VERANDA.PRG" ] || fail "no Veranda SDK in $VSDK (make vsdk VERANDA=...)"
rm -rf "$OUT"
mkdir -p "$OUT"
make -s veranda-stress STRESS_FLAGS="-DSECONDS=$SECS" VSDK="$VSDK" >"$OUT/build.log" 2>&1 || { cat "$OUT/build.log"; fail "MODSTRESS"; }
head -c 65536 /dev/urandom > "$OUT/data.bin"

run() {         # run <module> <dir> [sd]
	local d=$2
	mkdir -p "$d"
	if [ -n "$3" ]; then
		mkdir -p "$d/card"
		cp build/veranda/MODSTRESS.cvt "$d/card/AUTOSTART.cvt"
		cp "$1" "$d/card/music.mod"
		cp "$OUT/data.bin" "$VSDK/bin/VERANDA.SYS" "$d/card/"
		$PY "$VSDK/tools/vcard.py" make --mb 64 "$d/sd.img" "$d/card" >/dev/null || fail "vcard make"
		cp "$VSDK/bin/VERANDA.PRG" "$d/"
	else
		cp build/veranda/MODSTRESS.cvt "$d/AUTOSTART.cvt"
		cp "$1" "$d/music.mod"
		cp "$OUT/data.bin" "$VSDK/bin/VERANDA.PRG" "$VSDK/bin/VERANDA.SYS" "$d/"
	fi
	(cd "$d" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy exec "$EMU" -rom "$ROM" -ram 2048 \
		-prg VERANDA.PRG -run -warp -echo ${3:+-sdcard sd.img} > console.txt 2>&1) &
	local pid=$! i=0
	while [ $i -lt 4800 ]; do
		grep -aq "MODSTRESS DONE\|MODSTRESS error" "$d/console.txt" 2>/dev/null && break
		kill -0 $pid 2>/dev/null || break
		sleep 0.25
		i=$((i + 1))
	done
	kill $pid 2>/dev/null || true
	wait $pid 2>/dev/null || true
}
v() { sed -n "s/^.*MODSTRESS $2 \([0-9]*\).*/\1/p" "$1/console.txt" | tail -1; }
check() {       # check <dir> <label>
	local d=$1
	grep -aq "MODSTRESS DONE" "$d/console.txt" || { tail -3 "$d/console.txt"; fail "$2: did not finish"; }
	[ "$(v "$d" underruns)" = 0 ] || fail "$2: $(v "$d" underruns) underruns"
	[ "$(v "$d" dialogs)" -ge $((SECS / 15)) ] || fail "$2: only $(v "$d" dialogs) dialog boxes"
	for k in copies cardscreens listings reads64k; do
		[ "$(v "$d" $k)" -gt 0 ] || fail "$2: no $k"
	done
	$PY - "$d/console.txt" "$2" <<'PYEOF'
import re, sys
t = open(sys.argv[1], 'rb').read().decode('latin-1')
g = lambda k: int(re.findall(r'MODSTRESS %s (\d+)' % k, t)[-1])
fr = g('batches') * 1024
print('      %s: %d s, 0 underruns, %d batches; %d dialog boxes, %d CopyRects, %d PutCards screens, %d listings, %d x 64 KB read'
      % (sys.argv[2], g('elapsed'), g('batches'), g('dialogs'), g('copies'), g('cardscreens'), g('listings'), g('reads64k')))
print('        the batches: %.1f cycles a frame (%.1f %% of 499.3 at 16,022 Hz); CopyRect x%.2f, PutCards screen x%.2f'
      % (g('batch_cycles') / fr, 100 * g('batch_cycles') / fr / 499.3,
         g('copy_playing') / g('copy_quiet'), g('cards_playing') / g('cards_quiet')))
PYEOF
}
for m in modp/Cracktro/DIZZYCRS.MOD "modp/Demos/Mod.kaffe Og Kage.mod"; do
	n=$(basename "$m" | tr ' ' _)
	run "$m" "$OUT/host-$n"
	check "$OUT/host-$n" "host, $(basename "$m")"
	if [ "${VSD:-1}" != 0 ]; then
		run "$m" "$OUT/sd-$n" sd
		check "$OUT/sd-$n" "SD card, $(basename "$m")"
	fi
done
ok "real time under Veranda with menus, dialog boxes, CopyRect, PutCards, listings and reads: 0 underruns"
echo "GATE veranda-rt: PASS"
