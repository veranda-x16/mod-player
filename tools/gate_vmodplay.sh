#!/bin/bash
# The Veranda MOD Player (make veranda-app-gate; M4, M5, the GEOS interface).
#
#   tools/gate_vmodplay.sh        VSD=0: skip the SD card images
#                                 VMP_GOLDEN=1: write the golden pictures
#                                   (tests/golden/vmodplay/*.png; review them)
#                                 VMP_STRESS_SECONDS: the stress run (120)
#
# 1. "MOD Player.cvt" builds (examples/veranda/vmodplay.c and the vmp_*
#    files: vmp.h says what is where): the resident image ends below the C
#    stack ($8600), each overlay (1-4) is at most 8,191 bytes, the test
#    builds fit too; documents = MOD in its info block (the shell opens .mod
#    with it).
# 2. the song's length (examples/veranda/vmp_len.c, built natively) equals
#    micromod's micromod_calculate_song_duration within a second, for every
#    module of tests/mods and modp (skipped without vendor/micromod).
# 3. the test build's session (examples/veranda/test/vmp_test.h) through
#    mouseSource and the keyboard queue: a double click on the folder Mods
#    in the playlist, on its second module; MOD Player > the Calculator (a desk
#    accessory in the system folder: the player is put aside and comes
#    back playing); the pause and next icons (they flash: GEOS icons);
#    View > Pattern view and Playlist; Play > Repeat; File > Open... (Veranda's
#    Open dialog, vr_file, its VRF lines: Backspace to "/", M and Return into
#    Mods, C and Return: Checknob.mod); MOD Player > About, OK; Space, Space,
#    Right, Left, Esc; File > Play all; File > Quit. The menus at Veranda Look
#    2's bar (the kernel's geometry, V74, V79). The console in order
#    (each track with its first order, pattern and its length in seconds),
#    the busy pointer during each load, the arrow at the end, 0 underruns;
#    the desktop it quits to (DESKTOP.cvt, test/chk.c) finds AFLOW off, the
#    FIFO empty, intFirstVector clear, and its main loop runs. On the host
#    folder and an SD card image, with 2 MB and with 512 KB.
# 4. eleven screenshots (the session stopped at its moments with a VRAM
#    dump): check_vera.py (Veranda's) the display registers;
#    tests/check_vmodplay.py the composed picture (vram2png.py --color)
#    against the reviewed golden one, outside the fields that depend on the
#    music's timing (and the bar's clock), the VU meters' form, the pattern
#    view's rows; and (check_vmodplay.py --look, on "playing") Veranda Look
#    2's look: its palette, the kernel's menu bar (the mark, the name, its
#    chrome cards), the window's chrome (vrwin.h: the title band on the
#    accent with the close glyph, the status bar, the frame, the shadow),
#    the desk's colour, the panels' heads. PNGs in build/gate-vmodplay/*.png.
# 5. the stress run (VMP_STRESS, vmp_ui.c): 31 modules, the list scrolled
#    and the view switched every half second / 2 s, a new module every 30
#    s, the meters and fields as usual, for VMP_STRESS_SECONDS of music: 0
#    underruns; the drawing's cycles (without the audio batches inside
#    them) logged.
# 6. opened as a document (M5): a launcher does what the shell does on a
#    double click (V48: GetFile(app, ST_LD_DATA, 0, name, 0), the folder in
#    curDirName); the player goes there and plays the module.
# Sound on through SDL's dummy driver (under -sound none AFLOW never fires).
set -e
cd "$(dirname "$0")/.."
export CC65_HOME=${CC65_HOME:-$HOME/.local/share/cc65}
EMU=${X16EMU:-$HOME/Projects/x16-emulator/build/x16emu}
ROM=${X16ROM:-$HOME/Projects/x16-rom/build/x16/rom.bin}
VSDK=${VSDK:-build/vsdk}
VERANDA=${VERANDA:-$HOME/Projects/geos}
OUT=build/gate-vmodplay
GOLD=tests/golden/vmodplay
SECS=${VMP_STRESS_SECONDS:-120}
PY="python3 -I -B"
TF="--asm-define VR_CYCLES"             # (test builds: the batches' cycles)
ok() { echo "ok    $*"; }
fail() { echo "FAIL  $*"; exit 1; }
[ -f "$VSDK/bin/VERANDA.PRG" ] || fail "no Veranda SDK in $VSDK (make vsdk VERANDA=...)"
VSDK=$(cd "$VSDK" && pwd)
rm -rf "$OUT"
mkdir -p "$OUT"
V=build/veranda
A="$V/MOD Player.cvt"

# 1. the release build
make -s veranda VSDK="$VSDK" VMP_FLAGS= >"$OUT/build.log" 2>&1 || { cat "$OUT/build.log"; fail "the player does not build"; }
end=$(grep -E "^(CODE|RODATA|DATA|BSS|ALIGNDATA|ALIGNBSS) " $V/vmodplay.map | awk '{ print $3 }' | sort | tail -1)
[ $((16#$end)) -lt $((16#8600)) ] || fail "the resident image ends at \$$end, in the C stack"
sizes=""
for o in 1 2 3 4 5; do
	n=$(stat -c %s $V/vmodplay.bin.$o)
	[ "$n" -le 8191 ] || fail "overlay $o: $n bytes"
	sizes="$sizes $n"
done
field() { $PY "$VSDK/tools/vpack.py" --list "$A" | sed -n "s/^$1 *//p"; }
[ "$(field documents)" = MOD ] && [ "$(field class)" = 'VModPlay    V1.2' ] && [ "$(field flags)" = 3 ] \
	&& [ "$(field api)" = 5 ] && [ "$(field tile)" = "purple, 179 ink pixels" ] \
	|| fail "the info block: documents, class, api, its tile or banked overlays"
cp "$A" "$OUT/"
ok "MOD Player.cvt: resident \$0801-\$$end (the C stack from \$8600), overlays 1-5:$sizes bytes, documents = MOD, api 5, its own tile (Look 2's, purple)"

# 2. the song's length against micromod
if [ -f vendor/micromod/micromod-c/micromod.c ]; then
	${CC:-cc} -O2 -DVMP_HOST -Iinclude -Ivendor/micromod/micromod-c -o "$OUT/songlen" tests/vmp_songlen.c \
		examples/veranda/vmp_len.c src/mod_load.c vendor/micromod/micromod-c/micromod.c >"$OUT/songlen.log" 2>&1 \
		|| { cat "$OUT/songlen.log"; fail "tests/vmp_songlen.c"; }
	find tests/mods modp -iname '*.mod' -print0 | sort -z | xargs -0 "$OUT/songlen" >"$OUT/songlen.txt" \
		|| { grep DIFF "$OUT/songlen.txt" | head; fail "the song's length differs from micromod's"; }
	ok "the song's length (vmp_len.c): micromod's within a second, $(tail -1 "$OUT/songlen.txt")"
else
	echo "SKIP  the song's length (no vendor/micromod: make micromod)"
fi

# the desktop it quits to, and a desk accessory (Veranda's Calculator, built here)
make -s veranda-chk VSDK="$VSDK" >"$OUT/chk.log" 2>&1 || { cat "$OUT/chk.log"; fail "the checking desktop"; }
mkdir -p "$OUT/calc"
make -s -C "$VERANDA/apps/calc" SDK="$VSDK" OUT="$(pwd)/$OUT/calc" >"$OUT/calc/build.log" 2>&1 \
	|| { cat "$OUT/calc/build.log"; fail "the Calculator ($VERANDA/apps/calc)"; }

stage() {       # stage <dir>: the player as AUTOSTART.cvt, the Calculator, Mods/, the checking desktop
	mkdir -p "$1/Mods"
	cp "$V/MOD Player.cvt" "$1/AUTOSTART.cvt"
	cp "$V/AudChk.cvt" "$1/DESKTOP.cvt"
	cp "$OUT/calc/Calculator.cvt" "$1/"
	cp modp/Cracktro/DIZZYCRS.MOD tests/mods/real/Checknob.mod "tests/mods/real/JESTER - Stardust Memories.mod" "$1/Mods/"
	# (the Mods folder, Veranda's MODS.CFG, is the top folder: the session
	# starts there and opens Mods itself; without the file the default
	# /MODS would find Mods, the FAT names' case being ignored)
	printf '/\n' > "$1/MODS.CFG"
}
start() {       # start <dir> [sd]: the emulator, in the background (pid in $!)
	local d=$1
	cp "$VSDK/bin/VERANDA.PRG" "$d/"
	if [ -n "$2" ]; then
		cp "$VSDK/bin/VERANDA.SYS" "$d/card/"
		$PY "$VSDK/tools/vcard.py" make --mb 64 "$d/sd.img" "$d/card" >/dev/null || fail "vcard make"
	else
		cp "$VSDK/bin/VERANDA.SYS" "$d/"
	fi
	(cd "$d" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy exec "$EMU" -rom "$ROM" -ram ${RAM:-2048} \
		-prg VERANDA.PRG -run -warp -echo -dump V ${2:+-sdcard sd.img} > console.txt 2>&1) &
}
wait_for() {    # wait_for <dir> <pid> <until> [tries]
	local i=0
	while [ $i -lt ${4:-1200} ]; do
		grep -aq "$3" "$1/console.txt" 2>/dev/null && break
		kill -0 $2 2>/dev/null || break
		sleep 0.25
		i=$((i + 1))
	done
	sleep 0.3
	kill $2 2>/dev/null || true
	wait $2 2>/dev/null || true
}
run() {         # run <dir> <until> [sd]
	start "$1" $3
	wait_for "$1" $! "$2"
}
session() {     # session <dir> <label>: the console in order
	local c=$1/console.txt
	$PY - "$c" "$2" <<'PYEOF' || fail "$2: the session"
import re, sys
t = open(sys.argv[1], 'rb').read().decode('latin-1')
D, C, J = 'DIZZYCRS.MOD', 'Checknob.mod', 'JESTER - Stardust Me'
want = ['VMP RUNNING', 'VMP DOUBLE CLICK', 'VMP FOLDER /Mods', 'VMP DOUBLE CLICK',
        'VMP LOAD POINTER 001', 'VMP TRACK 002 ORDER 000 PATTERN 000 LENGTH 016 ' + D, 'VMP SONG SECONDS 00082',
        'VMP ACCESSORY Calculator', 'CALC RUNNING', 'CALC CLOSED', 'VMP ACCESSORY BACK',
        'VMP ICON PAUSE', 'VMP PAUSE', 'VMP ICON NEXT',
        'VMP LOAD POINTER 001', 'VMP TRACK 003 ORDER 000 PATTERN 002 LENGTH 031 ' + J, 'VMP SONG SECONDS 00240',
        'VMP VIEW PATTERN', 'VMP VIEW LIST', 'VMP REPEAT ON',
        'VMP OPEN BOX', 'VRF SHOW 4 /Mods', 'VRF SHOW 1 /', 'VRF SHOW 4 /Mods', 'VRF OK /Mods',
        'VRF NAME ' + C, 'VMP OPEN MODULE ' + C,
        'VMP LOAD POINTER 001', 'VMP TRACK 001 ORDER 000 PATTERN 001 LENGTH 033 ' + C, 'VMP SONG SECONDS 00253',
        'VMP ABOUT', 'VMP PAUSE', 'VMP RESUME',
        'VMP TRACK 002 ORDER 000 PATTERN 000 LENGTH 016 ' + D, 'VMP TRACK 001 ORDER 000 PATTERN 001 LENGTH 033 ' + C,
        'VMP STOP', 'VMP OPEN PLAY ALL /Mods', 'VMP TRACK 001 ORDER 000 PATTERN 001 LENGTH 033 ' + C,
        'VMP UNDERRUNS 00000', 'VMP POINTER 000', 'VMP QUIT',
        'AUDCHK AFLOW OFF', 'AUDCHK FIFO EMPTY', 'AUDCHK VECTOR CLEAR', 'AUDCHK MAINLOOP']
at = 0
for w in want:
    i = t.find(w, at)
    if i < 0:
        print('FAIL  %s: no "%s" after the earlier lines' % (sys.argv[2], w))
        sys.exit(1)
    at = i + len(w)
loads = len(re.findall(r'VMP TRACK ', t))
busy = len(re.findall(r'VMP LOAD POINTER 001', t))
if busy != loads:
    print('FAIL  %s: %d loads, the busy pointer at %d' % (sys.argv[2], loads, busy))
    sys.exit(1)
print('      %s: %d modules loaded (the busy pointer each time), the Open dialog, the about box, the accessory, the keys; 0 underruns'
      % (sys.argv[2], loads))
m = {k: v for k, v in re.findall(r'VMP (UPDATE|VU|FIELDS|PATTERN ROWS) (\d+ AVG \d+ MAX \d+)', t)}
if 'UPDATE' in m:
    print('      %s: cycles a main loop pass (without the batches): %s; VU meters %s; fields %s; pattern rows %s'
          % (sys.argv[2], m['UPDATE'], m.get('VU'), m.get('FIELDS'), m.get('PATTERN ROWS')))
PYEOF
}

# 3. the session
make -s veranda VSDK="$VSDK" VMP_FLAGS="-DVMP_TEST $TF" >"$OUT/test.log" 2>&1 || { cat "$OUT/test.log"; fail "the test build"; }
H=$OUT/host
stage "$H"
run "$H" "AUDCHK MAINLOOP"
session "$H" "host folder, 2 MB"
M=$OUT/ram512
stage "$M"
RAM=512 run "$M" "AUDCHK MAINLOOP"
session "$M" "host folder, 512 KB"
if [ "${VSD:-1}" != 0 ]; then
	S=$OUT/sd
	stage "$S/card"
	run "$S" "AUDCHK MAINLOOP" sd
	session "$S" "SD card, 2 MB"
	S=$OUT/sd512
	stage "$S/card"
	RAM=512 run "$S" "AUDCHK MAINLOOP" sd
	session "$S" "SD card, 512 KB"
fi
ok "the session through menus, icons, the playlist, the boxes, an accessory and the keys: 0 underruns, the quit leaves the audio off"

# 4. the screenshots: the session stopped at each moment it names (the
# emulators in parallel)
mkdir -p "$GOLD"
pids=""
while read -r name frame; do
	T=$OUT/shot-$name
	make -s veranda VSDK="$VSDK" VMP_FLAGS="-DVMP_TEST $TF -DVMP_STOP=$((10#$frame))" >"$OUT/shot.log" 2>&1 \
		|| { cat "$OUT/shot.log"; fail "the test build (stopped at $frame)"; }
	stage "$T"
	start "$T"
	pids="$pids $!"
	echo "$name $!" >> "$OUT/shots.txt"
done < <(sed -n 's/^.*VMP SHOT \([a-z]*\) \([0-9]*\).*/\1 \2/p' "$H/console.txt")
while read -r name pid; do
	wait_for "$OUT/shot-$name" $pid "Dumped system" 800
done < "$OUT/shots.txt"
n=0
while read -r name pid; do
	T=$OUT/shot-$name
	[ -f "$T/dump.bin" ] || { tail -3 "$T/console.txt"; fail "$name: no dump"; }
	$PY "$VERANDA/tools/check_vera.py" "$T/dump.bin" >/dev/null || fail "$name: the display registers"
	$PY "$VSDK/tools/vram2png.py" --color "$T/dump.bin" "$OUT/$name.png"
	$PY tests/check_vmodplay.py "$ROM" "$VSDK/tools/vram2png.py" "$T/dump.bin" "$name" "$GOLD/$name.png" \
		$([ "${VMP_GOLDEN:-0}" = 1 ] && echo --update) || fail "$name: the screen"
	n=$((n + 1))
done < "$OUT/shots.txt"
[ $n -ge 11 ] || fail "only $n screenshots"
# Veranda Look 2 (its DECISIONS V74, V77, V79): the palette, the kernel's menu
# bar, the window's chrome (vrwin.h), the desk's colour, while a module plays
$PY tests/check_vmodplay.py --look "$VSDK/tools/vram2png.py" "$OUT/shot-playing/dump.bin" || fail "playing: Veranda Look 2's bar and window"
ok "$n moments of the session: as the reviewed pictures, the meters' form, the pattern's rows, the display registers ($OUT/*.png)"

# 5. the stress run
T=$OUT/stress
mkdir -p "$T/Mods"
make -s veranda VSDK="$VSDK" VMP_FLAGS="-DVMP_STRESS=$SECS $TF" >"$OUT/stress.log" 2>&1 || { cat "$OUT/stress.log"; fail "the stress build"; }
cp "$V/MOD Player.cvt" "$T/AUTOSTART.cvt"
cp "$V/AudChk.cvt" "$T/DESKTOP.cvt"
cp modp/Cracktro/DIZZYCRS.MOD "$T/Mods/"
printf '/\n' > "$T/MODS.CFG"          # (the top folder: the run enters Mods itself)
find tests/mods/real -maxdepth 1 -name '*.mod' -print0 | sort -z | head -z -n 30 | xargs -0 cp -t "$T/Mods/"
start "$T"
wait_for "$T" $! "AUDCHK MAINLOOP" 4800
$PY - "$T/console.txt" "$SECS" <<'PYEOF' || fail "the stress run"
import re, sys
t = open(sys.argv[1], 'rb').read().decode('latin-1')
for w in ('VMP STRESS DONE', 'VMP UNDERRUNS 00000', 'AUDCHK MAINLOOP'):
    if w not in t:
        u = re.findall(r'VMP UNDERRUNS (\d+)', t)
        print('FAIL  the stress run: no "%s"%s' % (w, ' (underruns %s)' % u[-1] if u else ''))
        sys.exit(1)
m = dict(re.findall(r'VMP (UPDATE|VU|FIELDS|PATTERN ROWS|PATTERN PANEL|LIST PANEL|LIST SCROLL|BATCH CYCLES|CYCLES) (.*)', t))
ms = lambda s: '%.1f ms' % (int(s) / 8000.0)
def show(k):
    n, avg, mx = re.match(r'(\d+) AVG (\d+) MAX (\d+)', m[k]).groups()
    return '%s x%s avg %s max %s' % (k.lower(), n, ms(avg), ms(mx))
print('      %s s of music, %d modules, 0 underruns; the audio took %.0f %% of the CPU'
      % (sys.argv[2], len(re.findall('VMP TRACK', t)), 100.0 * int(m['BATCH CYCLES']) / int(m['CYCLES'])))
print('      the drawing (8 MHz cycles without the batches): ' + '; '.join(
      show(k) for k in ('VU', 'FIELDS', 'PATTERN ROWS', 'PATTERN PANEL', 'LIST PANEL', 'LIST SCROLL') if k in m))
PYEOF
ok "the stress run: the list scrolled, the views switched, modules loaded, the meters animating: 0 underruns"

# 6. M5: started as a document, as the shell starts it (V48)
L=$OUT/doc
mkdir -p "$L/Mods"
CC="cl65 -t none --cpu 65C02 -O -I $VSDK/include"
$CC -C "$VSDK/cfg/app-c.cfg" -o "$L/launch.bin" examples/veranda/test/launch.c "$VSDK/lib/veranda.lib" >"$L/build.log" 2>&1 \
	|| { cat "$L/build.log"; fail "the launcher"; }
(cd examples/veranda/test && $PY "$VSDK/tools/vpack.py" --load 0x0801 -o "$OLDPWD/$L/AUTOSTART.cvt" launch.hdr "$OLDPWD/$L/launch.bin") >/dev/null
make -s veranda VSDK="$VSDK" VMP_FLAGS="-DVMP_LOG" >"$L/app.log" 2>&1 || { cat "$L/app.log"; fail "the player (VMP_LOG)"; }
cp "$A" "$L/MOD Player.cvt"
cp modp/Cracktro/DIZZYCRS.MOD tests/mods/real/Checknob.mod "$L/Mods/"
run "$L" "VMP TRACK"
make -s veranda VSDK="$VSDK" VMP_FLAGS= >/dev/null 2>&1      # (build/veranda: the release again)
grep -aq "VMP DOCUMENT DIZZYCRS.MOD" "$L/console.txt" && grep -aq "VMP TRACK 002 ORDER 000 PATTERN 000 LENGTH 016 DIZZYCRS.MOD" "$L/console.txt" \
	|| { grep -a "VMP\|LAUNCH" "$L/console.txt"; fail "a module opened as a document does not play"; }
ok "opened as a document (GetFile ST_LD_DATA, Mods/DIZZYCRS.MOD in dataFileName and curDirName): it plays"
echo "screenshots: $(cd "$OUT" && ls *.png | tr '\n' ' ')"
echo "GATE vmodplay: PASS"
