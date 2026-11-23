#!/bin/bash
# The engine under Veranda, deterministic (make veranda-gate; M2, M3 part 1).
#
#   tools/gate_veranda.sh        (VSD=0: skip the SD card image)
#
# MODTRACE.cvt (examples/veranda/modtrace.c): the CHUNK_TRACE/CHUNK_HASH
# library linked with cfg/veranda-mod.cfg (zero page at $A9, banks from
# BankAlloc, the module through FOpen/FRead):
# 1. the bytes loaded into the banks equal the file's after its header
# 2. polled: each module's "chunks hash" equals tests/golden/chunk_hashes.txt
#    (the stand-alone harness's), on the host folder and an SD card image
# 3. from the interrupt (-DVIA_IRQ: AFLOW through intFirstVector,
#    examples/veranda/vr_audio.s, mod_irq_batch): the same hashes
# 4. the sequencer's first 200 ticks (-DTRACE_TICKS=200) equal host_seq's
#    on three modules
# Needs the Veranda SDK in build/vsdk (make vsdk VERANDA=<tree>). Audio
# runs with sound on through SDL's dummy driver (under -sound none the
# FIFO never drains, so AFLOW never fires).
set -e
cd "$(dirname "$0")/.."
export CC65_HOME=${CC65_HOME:-$HOME/.local/share/cc65}
EMU=${X16EMU:-$HOME/Projects/x16-emulator/build/x16emu}
ROM=${X16ROM:-$HOME/Projects/x16-rom/build/x16/rom.bin}
VSDK=${VSDK:-build/vsdk}
OUT=build/gate-veranda
PY="python3 -I -B"
ok() { echo "ok    $*"; }
fail() { echo "FAIL  $*"; exit 1; }
[ -f "$VSDK/bin/VERANDA.PRG" ] || fail "no Veranda SDK in $VSDK (make vsdk VERANDA=...)"
rm -rf "$OUT"
mkdir -p "$OUT"
make -s build/host_seq >/dev/null

MODULES=(tests/mods/sampleloop.mod tests/mods/fourvoice.mod tests/mods/real/Checknob.mod
	"tests/mods/real/JESTER - Stardust Memories.mod" "tests/mods/real/JOGEIR LILJEDAHL - G-Comp.mod"
	modp/Cracktro/DIZZYCRS.MOD "modp/Demos/Mod.kaffe Og Kage.mod")
golden() { sed -n "s/^$(printf '%s' "$1" | sed 's/[.[\*^$/]/\\&/g') | //p" tests/golden/chunk_hashes.txt; }

build() {       # build <name> <TRACE_MODE>
	make -s veranda-trace TRACE_MODE="$2" VSDK="$VSDK" >"$OUT/$1.log" 2>&1 || { cat "$OUT/$1.log"; fail "MODTRACE ($1)"; }
	cp build/veranda/MODTRACE.cvt "$OUT/$1.cvt"
}
# run <cvt> <module> <dir> [sd]: until "MODTRACE done|ticks done|error"
run() {
	local d=$3
	mkdir -p "$d"
	if [ -n "$4" ]; then
		mkdir -p "$d/card"
		cp "$1" "$d/card/AUTOSTART.cvt"
		cp "$2" "$d/card/music.mod"
		cp "$VSDK/bin/VERANDA.SYS" "$d/card/"
		$PY "$VSDK/tools/vcard.py" make --mb 64 "$d/sd.img" "$d/card" >/dev/null || fail "vcard make"
		cp "$VSDK/bin/VERANDA.PRG" "$d/"
	else
		cp "$1" "$d/AUTOSTART.cvt"
		cp "$2" "$d/music.mod"
		cp "$VSDK/bin/VERANDA.PRG" "$VSDK/bin/VERANDA.SYS" "$d/"
	fi
	(cd "$d" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy exec "$EMU" -rom "$ROM" -ram 2048 \
		-prg VERANDA.PRG -run -warp -echo ${4:+-sdcard sd.img} > console.txt 2>&1) &
	local pid=$! i=0
	while [ $i -lt 1200 ]; do
		grep -aq "MODTRACE done\|MODTRACE ticks done\|MODTRACE error" "$d/console.txt" 2>/dev/null && break
		kill -0 $pid 2>/dev/null || break
		sleep 0.25
		i=$((i + 1))
	done
	kill $pid 2>/dev/null || true
	wait $pid 2>/dev/null || true
}
line() { grep -ao "MODTRACE $2 .*" "$1/console.txt" | head -1 | sed "s/^MODTRACE $2 //"; }

hashes() {      # hashes <cvt> <label> [sd]
	local n=0 m d want got sum
	for m in "${MODULES[@]}"; do
		n=$((n + 1))
		d=$OUT/$2/$n
		run "$1" "$m" "$d" $3
		want=$(golden "$m")
		got=$(line "$d" done | cut -d' ' -f1,2)
		[ -n "$got" ] || { tail -3 "$d/console.txt"; fail "$2: $(basename "$m"): no result"; }
		[ "$got" = "$want" ] || fail "$2: $(basename "$m"): $got, the golden $want"
		sum=$($PY -c "import sys; d=open(sys.argv[1],'rb').read()[1084:]; print(sum(d))" "$m")
		[ -z "$(line "$d" sum)" ] || [ "$(line "$d" sum | cut -d' ' -f1)" = "$sum" ] \
			|| fail "$2: $(basename "$m"): the banks hold other bytes than the file"
		printf "      %-6s %-36s %s (%s)\n" "$2" "$(basename "$m")" "$got" "$(line "$d" done | cut -d' ' -f3-)"
	done
}

build polled ""
hashes "$OUT/polled.cvt" host
ok "polled under Veranda: the loaded bytes equal the files, all 7 chunk hashes equal the golden ones (host folder)"
if [ "${VSD:-1}" != 0 ]; then
	hashes "$OUT/polled.cvt" sd sd
	ok "the same from an SD card image"
fi
build irq -DVIA_IRQ
hashes "$OUT/irq.cvt" irq
ok "from the AFLOW interrupt (intFirstVector, 1024-frame batches): all 7 equal the golden ones"

build ticks -DTRACE_TICKS=200
for m in tests/mods/volume.mod tests/mods/real/Checknob.mod modp/Cracktro/DIZZYCRS.MOD; do
	d=$OUT/ticks/$(basename "$m" | tr ' ' _)
	run "$OUT/ticks.cvt" "$m" "$d"
	grep -ao "T [0-9].*" "$d/console.txt" | sed 's/^T //' > "$d/veranda.txt"
	./build/host_seq "$m" 200 > "$d/host.txt"
	[ "$(wc -l < "$d/veranda.txt")" = 200 ] || fail "$(basename "$m"): $(wc -l < "$d/veranda.txt") trace lines"
	cmp -s "$d/veranda.txt" "$d/host.txt" || { diff "$d/veranda.txt" "$d/host.txt" | head -4; fail "$(basename "$m"): the sequencer's trace differs"; }
done
ok "the sequencer's first 200 ticks equal host_seq's (volume, Checknob, DIZZYCRS)"
echo "GATE veranda-engine: PASS"
