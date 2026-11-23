#!/bin/bash
# The Veranda MOD Player through the X16's real input path (make
# veranda-mouse-gate): next, previous and the end of a song.
#
#   tools/gate_vmp_mouse.sh     MEMU: an x16emu with -mousescript (default
#                               ~/Projects/x16-emulator-mouse/build/x16emu,
#                               Veranda's DECISIONS V59); MROM: default R49's
#                               release image if it is there
#
# gate_vmodplay.sh's session moves the pointer through mouseSource (absolute
# positions from a script in the program); here the pointer comes from PS/2
# packets fed to the emulated SMC (-mousescript: the KERNAL's mouse_scan, the
# kernel following mouse_get relatively), the keys through the KERNAL's
# keyboard queue (the script's "type"), and the player is started as on a
# real X16: an SD card image with /VERANDA (the system, the shell; the
# player in /VERANDA/APPS), /DOCUMENTS and /Tunes, BASIC's DOS"CD:VERANDA", LOAD"VERANDA.PRG", RUN, then a
# double click on the player's icon in the shell. A click: the pointer
# pushed to 0, 0, moved onto the target, a press held 9 frames (150 ms), a
# release, the mouse still. The player is the VMP_LOG build (its console
# lines).
#
# /Tunes: a.mod and c.mod (46 s), b.mod (not a module: it does not load),
# d.mod (8 s). The playlist's order is the folder's sorted order; next and
# previous go on from the module last played or tried -- also after a
# module that does not load (passed over, its error shown), after stop and
# while the playlist shows another folder (it goes back to the music's
# folder) -- and wrap at the ends, as the stand-alone player
# (examples/modplay/play.c: step_track) does; a song's end is a next.
#
# 1. from the shell's icon: a double click on a.mod in the playlist; the
#    next and previous icons, play > next and play > previous, the Right
#    and Left keys (each passing over b.mod both ways); pause, then next;
#    Esc (stop), then Right (d.mod: on from c.mod); next at the end (wraps
#    to a.mod), previous at the start (wraps to d.mod); the playlist moved
#    to the root (a double click on ".."), then next (back in /Tunes: a.mod);
#    Backspace (up a folder: not previous), then Right (c.mod); d.mod again
#    (Right), its end: a.mod on its own, its title shown; play > repeat,
#    previous (d.mod), its end: d.mod again (the song repeats, no load);
#    repeat off, shuffle on, next twice (a module other than the one before
#    each time), previous (back to the one before). 0 underruns.
# 2. opened from the shell (V48): a double click on o1 (46 s) in /DOCUMENTS
#    (the player in /VERANDA/APPS), whose name is 42 characters long ("o1 a
#    module with a long name from 2026.mod": the player finds it by
#    dataLongName, V82; dataFileName holds its short form): it plays it, its
#    folder is the playlist (o1, o2.mod: 8 s); the next icon (o2.mod), the
#    Left key (o1), the Right key (o2.mod), its end: o1 on its own (the list
#    wraps), its title shown.
# 3. a song's end against the stand-alone player's order (play.c).
# 4. /VERANDA/.SYSTEM/MODS.CFG ("/Tunes"): the player starts in /Tunes;
#    shuffle, eight nexts, never the module playing twice in a row.
# 5. MODS.CFG not valid (no "/"): the default /MODS, where it starts.
# (Sessions 1-3 have no MODS.CFG and no /MODS: the player starts in
# /DOCUMENTS, as before; the music is in /Tunes.)
set -e -o pipefail
cd "$(dirname "$0")/.."
export CC65_HOME=${CC65_HOME:-$HOME/.local/share/cc65}
export PATH=$CC65_HOME/bin:$PATH
MEMU=${MEMU:-$HOME/Projects/x16-emulator-mouse/build/x16emu}
R49=$HOME/Downloads/Release.R49.ROM.Image/rom.bin
[ -f "$R49" ] || R49=$HOME/Projects/x16-rom/build/x16/rom.bin
MROM=${MROM:-$R49}
VSDK=${VSDK:-build/vsdk}
VERANDA=${VERANDA:-$HOME/Projects/geos}
OUT=build/gate-vmp-mouse
PY="python3 -I -B"
CL65=${CL65:-cl65}
ok() { echo "ok    $*"; }
fail() { echo "FAIL  $*"; exit 1; }
[ -f "$VSDK/bin/VERANDA.PRG" ] || fail "no Veranda SDK in $VSDK (make vsdk VERANDA=...)"
VSDK=$(cd "$VSDK" && pwd)
if ! { "$MEMU" -help 2>&1 || true; } | grep -q -- -mousescript; then
	echo "SKIP  $MEMU has no -mousescript (x16-emulator's branch mousescript)"
	echo "GATE vmp-mouse: SKIP"
	exit 0
fi
rm -rf "$OUT"
mkdir -p "$OUT"
echo "      ROM: $MROM"

# the player (VMP_LOG), the shell (built from Veranda's sources in a copy)
make -s veranda VSDK="$VSDK" VMP_FLAGS=-DVMP_LOG >"$OUT/build.log" 2>&1 || { cat "$OUT/build.log"; fail "the player (VMP_LOG)"; }
cp "build/veranda/MOD Player.cvt" "$OUT/"
cp -r "$VERANDA/shell" "$OUT/shell"
rm -f "$OUT/shell"/*.cvt "$OUT/shell"/*.bin "$OUT/shell"/*.map
make -s -C "$OUT/shell" SDK="$VSDK" >"$OUT/shell.log" 2>&1 || { cat "$OUT/shell.log"; fail "the shell ($VERANDA/shell)"; }
make -s veranda VSDK="$VSDK" VMP_FLAGS= >/dev/null 2>&1      # (build/veranda: the release again)
ok "the player (VMP_LOG) and the shell build"

C=$OUT/card
# (Veranda's layout, its V71: the player in /VERANDA/APPS, the shell opens
# /DOCUMENTS)
mkdir -p "$C/VERANDA/APPS" "$C/DOCUMENTS" "$C/Tunes"
cp "$VSDK/bin/VERANDA.PRG" "$VSDK/bin/VERANDA.SYS" "$OUT/shell/SHELL.cvt" "$C/VERANDA/"
cp "$OUT/MOD Player.cvt" "$C/VERANDA/APPS/"
cp tests/mods/multipattern.mod "$C/DOCUMENTS/o1 a module with a long name from 2026.mod"     # (42: V82)
cp tests/mods/minimal.mod "$C/DOCUMENTS/o2.mod"
cp tests/mods/multipattern.mod "$C/Tunes/a.mod"
head -c 3000 /dev/zero > "$C/Tunes/b.mod"
cp tests/mods/multipattern.mod "$C/Tunes/c.mod"
cp tests/mods/minimal.mod "$C/Tunes/d.mod"
$PY "$VSDK/tools/vcard.py" make --mb 64 "$OUT/sd.img" "$C" >/dev/null || fail "vcard make"

# the mouse script from a list of steps (stdin):
#   click x y | dbl x y     a click, a double click at x, y (from 0, 0)
#   rel dx dy               a click after a relative move (in an open menu)
#   key text                keys ("type": \X1D right, \X9D left, \X1B Esc, \X14 Backspace)
#   wait n                  n frames
script() {
	$PY -c '
import sys
out = ["0 id 3", "150 type DOS\"CD:VERANDA\"\\nLOAD\"VERANDA.PRG\"\\nRUN\\n"]
f = 900
for line in sys.stdin:
    t = line.split()
    if not t or t[0].startswith("#"):
        continue
    if t[0] in ("click", "dbl"):
        out += ["%d move -2000 -2000" % f, "%d move %s %s" % (f + 5, t[1], t[2])]
        f += 20
        out += ["%d down" % f, "%d up" % (f + 9)]
        f += 15
        if t[0] == "dbl":
            out += ["%d down" % f, "%d up" % (f + 6)]
            f += 10
    elif t[0] == "rel":
        out += ["%d move %s %s" % (f, t[1], t[2])]
        f += 20
        out += ["%d down" % f, "%d up" % (f + 9)]
        f += 15
    elif t[0] == "key":
        out.append("%d type %s" % (f, t[1]))
        f += 10
    elif t[0] == "wait":
        f += int(t[1])
out.append("%d exit" % f)
print("\n".join(out))
'
}
run() {         # run <name>: $OUT/<name>.txt the steps
	local d=$OUT/$1
	mkdir -p "$d"
	cp "$OUT/sd.img" "$d/"
	script < "$OUT/$1.txt" > "$d/mouse.txt"
	(cd "$d" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout -s INT 600 "$MEMU" -rom "$MROM" -ram 2048 \
		-sdcard sd.img -warp -echo -dump RV -mousescript mouse.txt > console.txt 2>&1) || true
	grep -a -q "the mouse script's exit" "$d/console.txt" && [ -f "$d/dump.bin" ] \
		|| { tail -c 600 "$d/console.txt"; fail "$1: the run did not reach its end"; }
	$PY "$VSDK/tools/vram2png.py" --color "$d/dump.bin" "$OUT/$1.png" >/dev/null
}
# check <name> <lines...>: the console's VMP lines in this order (a line
# starts so; * any text); no load (TRACK, ERROR) other than those, and none
# after them unless the last is "-"
check() {
	local n=$1
	shift
	$PY - "$OUT/$n/console.txt" "$n" "$@" <<-'EOF' || fail "$n: the session"
	import re, sys
	t = open(sys.argv[1], 'rb').read().decode('latin-1')
	got = [g for g in re.findall(r'VMP ([^\\\r\n]*?)(?=\\X|VMP |\r|\n|$)', t)
	       if not g.startswith(('SONG SECONDS', 'DOUBLE CLICK'))]
	want = sys.argv[3:]
	rest = want[-1] == '-'
	if rest:
	    want = want[:-1]
	m = lambda g, w: re.match(re.escape(w).replace(r'\*', '.*'), g)
	i = 0
	for w in want:
	    while i < len(got) and not m(got[i], w):
	        if got[i].startswith(('TRACK', 'ERROR')):
	            print('FAIL  %s: "%s" before "%s"' % (sys.argv[2], got[i], w))
	            print('      the console: ' + ' | '.join(got))
	            sys.exit(1)
	        i += 1
	    if i == len(got):
	        print('FAIL  %s: no "%s"' % (sys.argv[2], w))
	        print('      the console: ' + ' | '.join(got))
	        sys.exit(1)
	    i += 1
	extra = [] if rest else [g for g in got[i:] if g.startswith(('TRACK', 'ERROR'))]
	if extra:
	    print('FAIL  %s: then "%s"' % (sys.argv[2], extra[0]))
	    sys.exit(1)
	loads = sum(1 for g in got if g.startswith('TRACK'))
	print('      %s: %d modules loaded, every step as expected' % (sys.argv[2], loads))
	EOF
}
A="TRACK 001 * a.mod"; C3="TRACK 003 * c.mod"; D="TRACK 004 * d.mod"     # (the modules' places in the list)
EB="ERROR b.mod"

# the places (vmp.h): the playlist's rows y 64 + 16 r (+8), the transport
# icons at y 192-215, x (48 + 5 i) * 8 (+16); the menu bar Veranda Look 2's
# (Veranda's DECISIONS V74, V79: the kernel's geometry in the UI font,
# Veranda's tests/look_menu.py): Play's middle at x 156, its items 16 lines
# apart from the first (the pointer is put on it)
PREV="400 204"; PAUSE="480 204"; NEXT="560 204"
ROW0="40 72"; ROW1="60 88"
PLAY_MENU="156 7"

# the shell (Veranda Look 2, its V77: window 0 on /DOCUMENTS, the sidebar's
# places at x 56, y 96 + 16 i; the icons' tiles' middles at x 152 + 72 i,
# y 96, ".." first): the place Apps, the player (the second icon); it
# starts in /DOCUMENTS (the shell's V71): its playlist's ".." to the top,
# Tunes (the second), a.mod
APPICON="click 56 112
wait 300
dbl 224 96"

# 1. the player from its icon in the shell
cat > "$OUT/icons.txt" <<EOF
$APPICON
wait 1500
dbl $ROW0
wait 200
dbl $ROW1
wait 200
dbl $ROW1
wait 300
click $NEXT
wait 300
click $PREV
wait 300
click $PLAY_MENU
wait 30
rel 0 32
wait 300
click $PLAY_MENU
wait 30
rel 0 48
wait 300
key \X1D
wait 300
key \X9D
wait 300
click $PAUSE
wait 100
click $NEXT
wait 300
key \X1B
wait 100
key \X1D
wait 200
click $NEXT
wait 300
click $PREV
wait 200
click $NEXT
wait 300
dbl $ROW0
wait 300
click $NEXT
wait 300
key \X14
wait 300
key \X1D
wait 900
click $PLAY_MENU
wait 30
rel 0 64
wait 60
click $PREV
wait 600
click $NEXT
wait 300
click $PLAY_MENU
wait 30
rel 0 64
wait 60
click $PLAY_MENU
wait 30
rel 0 80
wait 60
click $NEXT
wait 300
click $NEXT
wait 300
click $PREV
wait 300
EOF
run icons
check icons "RUNNING" "FOLDER /" "FOLDER /Tunes" "$A" \
	"ICON NEXT" "$EB" "$C3" "ICON PREVIOUS" "$EB" "$A" \
	"$EB" "$C3" "$EB" "$A" \
	"$EB" "$C3" "$EB" "$A" \
	"ICON PAUSE" "PAUSE" "ICON NEXT" "$EB" "$C3" \
	"STOP" "$D" "ICON NEXT" "$A" "ICON PREVIOUS" "$D" \
	"ICON NEXT" "$A" "FOLDER /" "ICON NEXT" "FOLDER /Tunes" "$EB" "$C3" \
	"FOLDER /" "FOLDER /Tunes" "$D" "SONG END" "$A" "TITLE multi pattern" \
	"REPEAT ON" "ICON PREVIOUS" "$D" "TITLE minimal" "SONG REPEAT" "ICON NEXT" "$A" \
	"REPEAT OFF" "SHUFFLE ON" -
# shuffle (the console after SHUFFLE ON): each next loads a module (passing
# over b.mod), the first another than a.mod; previous goes back to the one
# before
$PY - "$OUT/icons/console.txt" <<-'EOF' || fail "icons: shuffle"
import re, sys
t = open(sys.argv[1], 'rb').read().decode('latin-1')
t = t[t.find('VMP SHUFFLE ON'):]
tr = re.findall(r'VMP TRACK \d+ ORDER \d+ PATTERN \d+ LENGTH \d+ (\S+)', t)
before = re.findall(r'VMP TRACK \d+ ORDER \d+ PATTERN \d+ LENGTH \d+ (\S+)',
                    open(sys.argv[1], 'rb').read().decode('latin-1')[:open(sys.argv[1], 'rb').read().decode('latin-1').find('VMP SHUFFLE ON')])[-1]
if len(tr) != 3 or tr[0] == before or tr[2] != tr[0] or t.count('ICON') != 3:
    print('FAIL  icons: shuffle: %s, then %s' % (before, ', '.join(tr)))
    sys.exit(1)
print('      icons: shuffle: %s, then %s; previous: %s' % (before, ', '.join(tr[:2]), tr[2]))
EOF
ok "from the shell's icon: next and previous by the icons, the play menu and the arrow keys, over a module that does not load, paused, stopped, at both ends, from another folder, Backspace; a song's end goes on (repeat: it stays); shuffle"

# 2. opened from the shell with a module (its folder is the playlist)
cat > "$OUT/document.txt" <<EOF
dbl 224 96
wait 900
click $NEXT
wait 120
key \X9D
wait 120
key \X1D
wait 900
EOF
run document
O1="TRACK 001 * o1 a module with a l"; O2="TRACK 002 * o2.mod"
check document "RUNNING" "$O1" "DOCUMENT o1 a module with a long name from 2026.mod" "ICON NEXT" "$O2" "$O1" "$O2" \
	"SONG END" "$O1" "TITLE multi pattern"
ok "opened from the shell (o1, a name of 42 characters: dataLongName): its folder is the playlist; next, Left, Right; o2.mod's end goes on (wrapping) to o1"
# 3. the same order as the stand-alone player (examples/modplay/play.c,
# which plays through a folder correctly on the X16): PLAY.PRG built with
# its test hooks AUTOPLAY (the first module that loads, at once) and
# VERIFY_ORDER (each module tried, "+name" or "-name", on the emulator's
# stdout), in a host folder holding /Tunes's four files; the Veranda player
# with a.mod double clicked in /Tunes and left alone: the modules it tries
# as the songs end (TRACK, ERROR) are play.c's, in play.c's order.
P=$OUT/playc
mkdir -p "$P"
cp "$C/Tunes"/*.mod "$P/"
$CL65 -t cx16 -C cfg/x16mod-c.cfg -O -Iinclude -DBUILD_ID='"gate"' -DAUTOPLAY -DVERIFY_ORDER -o "$P/PLAY.PRG" \
	examples/modplay/play.c src/playlist.c src/gui.c src/mouse.s src/mod_x16.c src/mod_strerror.c src/kread.s \
	src/vtext.s build/lib/x16mod.lib >"$P/build.log" 2>&1 || { cat "$P/build.log"; fail "PLAY.PRG (VERIFY_ORDER)"; }
rm -f examples/modplay/play.o src/playlist.o src/gui.o src/mouse.o src/mod_x16.o src/mod_strerror.o src/kread.o src/vtext.o
(cd "$P" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout -s INT 40 "$MEMU" -rom "$MROM" -ram 2048 \
	-prg PLAY.PRG -run -warp > console.txt 2>&1) || true
cat > "$OUT/auto.txt" <<EOF
$APPICON
wait 1500
dbl $ROW0
wait 200
dbl $ROW1
wait 200
dbl $ROW1
wait 9600
EOF
run auto
$PY - "$P/console.txt" "$OUT/auto/console.txt" <<-'PYEOF' || fail "the order against play.c"
import re, sys
p = re.findall(r'^([+-])(\S+\.mod)\r?$', open(sys.argv[1], 'rb').read().decode('latin-1'), re.M)
t = open(sys.argv[2], 'rb').read().decode('latin-1')
v = [('-' if k == 'ERROR' else '+', n) for k, n in
     re.findall(r'VMP (TRACK|ERROR) (?:\d+ ORDER \d+ PATTERN \d+ LENGTH \d+ )?(\S+\.mod)', t)]
show = lambda l: ' '.join(a + b for a, b in l)
print('      play.c:   ' + show(p[:len(v) + 2]))
print('      Veranda:  ' + show(v))
if len(v) < 6 or len(p) < len(v) or p[:len(v)] != v:
    print('FAIL  the Veranda player tries the modules in another order than play.c')
    sys.exit(1)
PYEOF
ok "a song's end: the Veranda player goes through /Tunes as play.c does (the same modules, the same order, the one that does not load passed over, wrapping)"
# 4. the Mods folder from MODS.CFG (Veranda's APPFORMAT.md: one line, a
# path from "/", LF; V84): "/Tunes" in /VERANDA/.SYSTEM: the player
# started from its icon lists /Tunes at once; a.mod (the second row),
# shuffle on, next eight times: no module follows itself (b.mod, which
# does not load, passed over)
mkdir -p "$C/VERANDA/.SYSTEM"
printf '/Tunes\n' > "$C/VERANDA/.SYSTEM/MODS.CFG"
$PY "$VSDK/tools/vcard.py" make --mb 64 "$OUT/sd.img" "$C" >/dev/null || fail "vcard make (MODS.CFG)"
{
	echo "$APPICON"
	echo "wait 1500"
	echo "dbl $ROW1"
	echo "wait 300"
	echo "click $PLAY_MENU"
	echo "wait 30"
	echo "rel 0 80"
	echo "wait 60"
	for i in 1 2 3 4 5 6 7 8; do echo "click $NEXT"; echo "wait 300"; done
} > "$OUT/modscfg.txt"
run modscfg
check modscfg "MODS /Tunes" "RUNNING" "$A" "SHUFFLE ON" -
$PY - "$OUT/modscfg/console.txt" <<-'EOF' || fail "modscfg: shuffle"
import re, sys
t = open(sys.argv[1], 'rb').read().decode('latin-1')
t = t[t.find('VMP SHUFFLE ON'):]
tr = ['a.mod'] + re.findall(r'VMP TRACK \d+ ORDER \d+ PATTERN \d+ LENGTH \d+ (\S+)', t)
same = [i for i in range(1, len(tr)) if tr[i] == tr[i - 1]]
if len(tr) != 9 or same:
    print('FAIL  modscfg: shuffle: %s' % ', '.join(tr))
    sys.exit(1)
print('      modscfg: shuffle: %s' % ', '.join(tr))
EOF
ok "the Mods folder from /VERANDA/.SYSTEM/MODS.CFG; shuffle: eight nexts, never the module playing again"
# 5. the default: MODS.CFG not valid ("Tunes", no "/") and a folder /MODS
# (e.mod): the player starts in /MODS; e.mod (the second row) plays
printf 'Tunes\n' > "$C/VERANDA/.SYSTEM/MODS.CFG"
mkdir -p "$C/MODS"
cp tests/mods/minimal.mod "$C/MODS/e.mod"
$PY "$VSDK/tools/vcard.py" make --mb 64 "$OUT/sd.img" "$C" >/dev/null || fail "vcard make (/MODS)"
printf '%s\nwait 1500\ndbl %s\nwait 300\n' "$APPICON" "$ROW1" > "$OUT/modsdef.txt"
run modsdef
check modsdef "MODS /MODS" "RUNNING" "TRACK 001 * e.mod"
ok "MODS.CFG not valid: the default /MODS"
echo "screenshots: $OUT/icons.png $OUT/document.png $OUT/auto.png $OUT/modscfg.png $OUT/modsdef.png"
echo "GATE vmp-mouse: PASS"
