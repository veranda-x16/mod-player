# x16-mod -- ProTracker MOD player for the Commander X16.
#
# Two hard-won environment facts (docs/environment.md):
#   * CC65_HOME must be set or ld65 cannot find its own config files.
#   * Pure-assembly targets need -C cfg/x16mod.cfg; cx16.cfg wants the C
#     runtime's segments and dies with "Start address of memory area 'BSS'
#     is not constant".

export CC65_HOME ?= $(HOME)/.local/share/cc65
CL65      ?= cl65
EMU       ?= $(HOME)/Projects/x16-emulator/build/x16emu
ROM       ?= $(HOME)/Projects/x16-rom/build/x16/rom.bin
PYTHON    ?= python3

ASMFLAGS   = -t cx16 -C cfg/x16mod.cfg --cpu 65c02 --asm-include-dir src
BUILD      = build

# Output rate the generated tables are built for (AUDIO_RATE register value).
# 42 = 16021.7 Hz, the Phase 2 baseline. 64 = 24414.1 Hz, the stretch goal.
RATE      ?= 42

.PHONY: all clean tools phase1 phase2 test dirs

all: dirs $(BUILD)/t_phase1.prg

dirs:
	@mkdir -p $(BUILD)

# --- test programs ----------------------------------------------------------
# Each test PRG exports several entry symbols; tools/tbrun.py pokes it in and
# RUNs them individually. The -Ln label file is what makes that possible.

$(BUILD)/t_phase1.prg: tests/t_phase1.s src/vera_audio.s src/x16.inc src/cycles.inc | dirs
	$(CL65) $(ASMFLAGS) -o $@ -Ln $(BUILD)/t_phase1.lbl -l $(BUILD)/t_phase1.lst \
	    tests/t_phase1.s src/vera_audio.s

# --- generated tables -------------------------------------------------------
src/tables.s: tools/gen_tables.py
	$(PYTHON) tools/gen_tables.py --rate $(RATE) --out $@

# --- phases -----------------------------------------------------------------
phase1: $(BUILD)/t_phase1.prg
	$(PYTHON) tests/run_phase1.py

# --- reference oracle -------------------------------------------------------
# Optional: the full upstream tree, whose songs/ directory gives the trace
# comparison a handful of real modules to chew on. Not required to build or
# test -- vendor/micromod-c holds everything the oracle needs.
vendor/micromod/.fetched:
	@mkdir -p vendor
	git clone --depth 1 https://github.com/martincameron/micromod.git vendor/micromod
	@touch $@

micromod: vendor/micromod/.fetched

# Removes every generated artefact. A failed cl65 link leaves a partial output
# behind that make then treats as up to date, and a changed -D flag does not
# dirty a target at all -- so when something behaves like a stale binary, this
# is the reset.
clean:
	rm -rf $(BUILD) $(RELEASE)
	rm -f src/tables.s
	rm -f tests/mods/*.mod

distclean: clean
	rm -rf vendor/micromod

$(BUILD)/t_rt.prg: tests/t_rt.s src/vera_audio.s src/x16.inc src/cycles.inc | dirs
	$(CL65) $(ASMFLAGS) -u __EXEHDR__ -o $@ -Ln $(BUILD)/t_rt.lbl \
	    tests/t_rt.s src/vera_audio.s

$(BUILD)/t_phase2.prg: tests/t_phase2.s src/mixer.s src/tables.s src/x16.inc src/cycles.inc | dirs
	$(CL65) $(ASMFLAGS) -o $@ -Ln $(BUILD)/t_phase2.lbl tests/t_phase2.s src/mixer.s src/tables.s

$(BUILD)/t_phase2b.prg: tests/t_phase2.s src/mixer.s src/tables.s src/x16.inc src/cycles.inc | dirs
	$(CL65) $(ASMFLAGS) --asm-define MIX_TO_BUF -o $@ \
	    -Ln $(BUILD)/t_phase2b.lbl tests/t_phase2.s src/mixer.s src/tables.s

phase2: $(BUILD)/t_phase2.prg $(BUILD)/t_phase2b.prg
	$(PYTHON) tests/run_phase2.py

$(BUILD)/t_cache.prg: tests/t_cache.s src/cache.s src/mixer.s src/tables.s | dirs
	$(CL65) $(ASMFLAGS) -o $@ -Ln $(BUILD)/t_cache.lbl \
	    tests/t_cache.s src/cache.s src/mixer.s src/tables.s

phase3: $(BUILD)/t_cache.prg
	$(PYTHON) tests/run_cache.py

$(BUILD)/t_api.prg: tests/t_api.s src/mixer_api.s src/mixer.s src/cache.s src/tables.s | dirs
	$(CL65) $(ASMFLAGS) -o $@ -Ln $(BUILD)/t_api.lbl \
	    tests/t_api.s src/mixer_api.s src/mixer.s src/cache.s src/tables.s

# MODPLAY.PRG -- C build, so it uses cc65's own cx16 config, not ours.
$(BUILD)/MODPLAY.PRG: examples/modplay/main.c src/mod_x16.c src/mod_load.c src/mod_bankload.c | dirs
	$(CL65) -t cx16 -O -Iinclude -o $@ -Ln $(BUILD)/modplay.lbl \
	    examples/modplay/main.c src/mod_x16.c src/mod_load.c src/mod_bankload.c \
	    src/mod_strerror.c src/kread.s

# --- the whole suite ---------------------------------------------------------
test: $(BUILD)/host_mixer $(BUILD)/host_modinfo $(BUILD)/host_seq \
      $(BUILD)/oracle_micromod mods \
      $(BUILD)/t_phase1.prg $(BUILD)/t_phase2.prg $(BUILD)/t_phase2b.prg \
      $(BUILD)/t_cache.prg $(BUILD)/T_CHUNK.PRG
	@echo "=== Phase 1: audio path ==="   && $(PYTHON) tests/run_phase1.py
	@echo "=== Phase 2: mixer ==="        && $(PYTHON) tests/run_phase2.py
	@echo "=== Phase 3: banked cache ===" && $(PYTHON) tests/run_cache.py
	@echo "=== Phase 5: loader ==="       && $(PYTHON) tests/run_phase5.py
	@echo "=== Phase 6+7: sequencer + effects ===" && $(PYTHON) tools/cmp_trace.py 20000
	@echo "=== Phase 6+7: X16 vs host ===" && ./tests/run_x16_trace.sh
	@echo "=== Render loop: chunk decisions ===" && $(MAKE) -s chunk
	@echo "=== The library alone: examples/minimal ===" && $(MAKE) -s minimal

mods:
	@$(PYTHON) tools/mkmod.py >/dev/null

$(BUILD)/host_mixer: host/host_mixer.c | dirs
	$(CC) -O2 -Wall -Wextra -o $@ $<

$(BUILD)/host_modinfo: host/host_modinfo.c src/mod_load.c src/mod_strerror.c | dirs
	$(CC) -O2 -Wall -Wextra -Iinclude -o $@ $^

CC ?= gcc

# X16 trace build: same sequencer source as host_seq, dumping the same format.
# vera_audio.s is needed even though the trace build makes no sound:
# mixer_api.s exports C wrappers around audio_init/start/stop.
$(BUILD)/TRACE.PRG: examples/modplay/main.c src/mod_x16.c src/mod_load.c \
                    src/mod_bankload.c src/mod_strerror.c src/mod_play.c src/mod_hooks_x16.c src/kread.s \
                    src/mixer_api.s src/mixer.s src/cache.s src/vera_audio.s \
                    src/tables.s | dirs
	rm -f $@
	$(CL65) -t cx16 -C cfg/x16mod-c.cfg -O -Iinclude -DTRACE_TICKS=$(TRACE_TICKS) -o $@ \
	    -Ln $(BUILD)/trace.lbl $^
TRACE_TICKS ?= 200

# Render-loop regression build: the FIFO gate compiled out, so the loop is a
# pure function of the module and its chunk decisions can be hashed. See
# tests/t_chunk.c for why a WAV capture cannot answer this question.
$(BUILD)/T_CHUNK.PRG: tests/t_chunk.c tests/t_chunk_cyc.s src/mod_x16.c src/kread.s \
                      $(BUILD)/lib-trace/x16mod.lib | dirs
	rm -f $@
	$(CL65) -t cx16 -C cfg/x16mod-c.cfg -O -Iinclude --asm-include-dir src \
	    -DCHUNK_TRACE -DCHUNK_HASH -DCHUNK_LIMIT=$(CHUNK_LIMIT) -o $@ \
	    tests/t_chunk.c tests/t_chunk_cyc.s src/mod_x16.c src/kread.s \
	    $(BUILD)/lib-trace/x16mod.lib
CHUNK_LIMIT ?= 6000

.PHONY: chunk chunk-update
chunk: $(BUILD)/T_CHUNK.PRG
	@$(PYTHON) tests/run_chunk.py
chunk-update: $(BUILD)/T_CHUNK.PRG
	@$(PYTHON) tests/run_chunk.py --update

$(BUILD)/host_seq: host/host_seq.c src/mod_play.c src/mod_load.c | dirs
	$(CC) -O2 -Wall -Wextra -Iinclude -o $@ $^

# Includes micromod.c directly to reach its file-static sequencer state.
# Built from the vendored copy in vendor/micromod-c (BSD-3, see its
# PROVENANCE.md), so the test suite works without network access.
# -w because it is not our code to lint.
$(BUILD)/oracle_micromod: host/oracle_micromod.c vendor/micromod-c/micromod.c | dirs
	$(CC) -O2 -w -Ivendor/micromod-c -o $@ host/oracle_micromod.c

MODPLAY_SRC = examples/modplay/play.c src/playlist.c src/gui.c src/mouse.s src/mod_x16.c \
              src/mod_strerror.c src/kread.s src/vtext.s

BUILD_ID ?= $(shell date +%H%M)

$(BUILD)/PLAY.PRG: $(MODPLAY_SRC) $(BUILD)/lib/x16mod.lib | dirs
	rm -f $@
	$(CL65) -t cx16 -C cfg/x16mod-c.cfg -O -Iinclude -DBUILD_ID='"$(BUILD_ID)"' \
	    $(EXTRA_CFLAGS) -o $@ -Ln $(BUILD)/play.lbl $^

# --- the library (include/x16mod.h) -------------------------
# The target-neutral engine as one archive: built once with -t none (no
# character-set translation, no target strings), linked by the stand-alone
# player (-t cx16) and by Veranda applications alike. lib-trace/ is the same
# sources with the chunk hash compiled in (tests/t_chunk.c, and the Veranda
# MODTRACE build), so both prove the same objects.
LIB_SRC  = src/mod_load.c src/mod_bankload.c src/mod_play.c src/mod_hooks_x16.c \
           src/mod_player.c src/mod_lib.c src/mixer_api.s src/mixer.s src/cache.s \
           src/vera_audio.s src/tables.s src/vu.s src/mod_irq.s
LIBFLAGS = -t none --cpu 65C02 -O -Iinclude --asm-include-dir src

define lib_rule
$(BUILD)/$(1)/x16mod.lib: $(LIB_SRC) include/x16mod.h include/mod_play.h include/mod_player.h src/x16.inc
	@mkdir -p $(BUILD)/$(1)
	@rm -f $(BUILD)/$(1)/*.o $$@
	@for f in $(LIB_SRC); do \
	  o=$(BUILD)/$(1)/$$$$(basename $$$$f).o; \
	  $(CL65) $(LIBFLAGS) $(2) -c -o $$$$o $$$$f || exit 1; \
	done
	ar65 a $$@ $(BUILD)/$(1)/*.o
endef
$(eval $(call lib_rule,lib,))
$(eval $(call lib_rule,lib-trace,-DCHUNK_TRACE -DCHUNK_HASH -DCHUNK_LIMIT=$(CHUNK_LIMIT)))

# examples/minimal: the library alone, polled (tests/run_minimal.py)
$(BUILD)/MINIMAL.PRG: examples/minimal/main.c src/mod_x16.c src/kread.s $(BUILD)/lib/x16mod.lib | dirs
	$(CL65) -t cx16 -C cfg/x16mod-c.cfg -O -Iinclude -o $@ $^
minimal: $(BUILD)/MINIMAL.PRG
	@$(PYTHON) tests/run_minimal.py

.PHONY: lib minimal
lib: $(BUILD)/lib/x16mod.lib $(BUILD)/lib-trace/x16mod.lib

# --- release ----------------------------------------------------------------
# Everything needed on an SD card (or a hostfs directory) in one place.
#
# AUTOBOOT.X16 is simply a copy of the player: the ROM's BOOT command loads and
# runs a PRG of that name from device 8 at startup, and cl65's cx16 target
# already prefixes the binary with the BASIC RUN stub, so no launcher is needed.

RELEASE ?= release

.PHONY: release release-safe run-release clean-release

# release       -- MACPTR bulk reads: ~4x faster loading (measured 0.99s vs
#                  4.00s for a 129KB module). This is the default.
# release-safe  -- byte-at-a-time reads, for a device that cannot handle
#                  MACPTR. Roughly four times slower but maximally cautious.
release-safe:
	rm -f $(BUILD)/PLAY.PRG
	$(MAKE) -s $(BUILD)/PLAY.PRG EXTRA_CFLAGS=-DSLOW_LOAD
	$(MAKE) -s release
	@echo "built with SLOW_LOAD (byte-at-a-time reads)"

release: $(BUILD)/PLAY.PRG
	@mkdir -p $(RELEASE)
	cp $(BUILD)/PLAY.PRG $(RELEASE)/MODPLAY.PRG
	cp $(BUILD)/PLAY.PRG $(RELEASE)/AUTOBOOT.X16
	@printf '%s\n' \
	  'x16-mod -- ProTracker MOD player for the Commander X16' \
	  '' \
	  'Copy the contents of this directory to an SD card (or point the' \
	  'emulator at it) together with any .mod files you want to play.' \
	  '' \
	  '  AUTOBOOT.X16   runs the player automatically at power-on' \
	  '  MODPLAY.PRG    the same program, to start by hand:' \
	  '                 LOAD"MODPLAY.PRG",8,1 then RUN' \
	  '' \
	  'The player scans the current directory at startup and lists the' \
	  'modules and folders it finds. Modules need not sit beside the' \
	  'player: it starts either way, and folders can be browsed to reach' \
	  'them. Playback advances to the next module automatically.' \
	  '' \
	  'Mouse:    click a folder to enter it, a module to play it.' \
	  '          |< and >| step tracks, the middle button pauses.' \
	  'Keyboard: up/down move the list one entry, left/right jump a' \
	  '          whole column -- the quick way through a big folder.' \
	  '          RETURN opens the entry, n next, p previous,' \
	  '          SPACE pause, q quit.' \
	  '' \
	  'Supported: 4-channel 31-instrument ProTracker modules' \
	  '(M.K. M!K! M&K! N.T. FLT4 4CHN). Output is 16 kHz 8-bit stereo.' \
	  > $(RELEASE)/README.TXT
	@echo "--- $(RELEASE) ---"
	@ls -l $(RELEASE)

# Boot the release exactly as hardware would: no -prg, let AUTOBOOT.X16 run.
run-release: release
	cd $(RELEASE) && $(EMU) -rom $(ROM) -run

clean-release:
	rm -rf $(RELEASE)

# --- run --------------------------------------------------------------------
# Boot the release in the emulator exactly as the hardware does: no -prg, so
# AUTOBOOT.X16 starts itself from the hostfs root. A directory of modules is
# staged alongside it, because a player with nothing to list tells you very
# little. Point MODS anywhere: make run MODS=/path/to/mods
#
# -ram 2048 matters: with the default 512 KB most real modules report "not
# enough banked RAM" and the list fills up with entries marked too big.

MODS   ?= modp
RUNDIR  = $(BUILD)/run

.PHONY: run
run: release
	@rm -rf $(RUNDIR) && mkdir -p $(RUNDIR)
	@if [ -d "$(MODS)" ]; then \
	   cp -R "$(MODS)"/. $(RUNDIR)/ 2>/dev/null || true; \
	   echo "modules staged from $(MODS)"; \
	 else \
	   echo "no '$(MODS)' directory -- starting with an empty list"; \
	 fi
	@cp $(RELEASE)/AUTOBOOT.X16 $(RELEASE)/MODPLAY.PRG $(RELEASE)/README.TXT $(RUNDIR)/
	cd $(RUNDIR) && $(EMU) -rom $(ROM) -ram 2048 -run

# --- zip --------------------------------------------------------------------
# One archive to hand to somebody else: unzip it onto an SD card and the
# machine boots into the player. Deliberately a fixed name rather than one
# stamped with BUILD_ID, so a forum attachment keeps a stable link.

ZIPFILE = $(RELEASE)/x16-mod.zip

.PHONY: zip
zip: release
	@rm -f $(ZIPFILE)
	@cd $(RELEASE) && zip -q x16-mod.zip AUTOBOOT.X16 MODPLAY.PRG README.TXT
	@echo "--- $(ZIPFILE) ---"
	@unzip -l $(ZIPFILE)

# --- deploy -----------------------------------------------------------------
# Copy a release onto a mounted card.
#
# DEST can be given explicitly; otherwise the card is located by searching the
# usual mount points for a directory called modp. That way the target survives
# the card being unplugged and remounted under a different name.
#
# Note: AUTOBOOT.X16 only starts automatically from the card ROOT. In a
# subdirectory it is inert -- launch MODPLAY.PRG by hand, or copy AUTOBOOT.X16
# to the root yourself if you want the machine to boot straight into it.

DEST ?=

.PHONY: deploy
deploy: release
	@d="$(DEST)"; \
	if [ -z "$$d" ]; then \
	  d=$$(find /run/media /media /mnt -maxdepth 4 -type d -iname modp 2>/dev/null | head -1); \
	fi; \
	if [ -z "$$d" ] || [ ! -d "$$d" ]; then \
	  echo "No card found. Insert it and retry, or: make deploy DEST=/path/to/modp"; \
	  echo "Currently mounted:"; ls /run/media/* /media/* 2>/dev/null | head; \
	  exit 1; \
	fi; \
	cp $(RELEASE)/MODPLAY.PRG $(RELEASE)/README.TXT "$$d/"; \
	root=$$(df --output=target "$$d" 2>/dev/null | tail -1); \
	if [ -n "$$root" ] && [ -d "$$root" ] && [ "$$root" != "/" ]; then \
	  cp $(RELEASE)/MODPLAY.PRG "$$root/MODPLAY.PRG"; \
	  cp $(RELEASE)/AUTOBOOT.X16 "$$root/AUTOBOOT.X16"; \
	  echo "card root: $$root  (AUTOBOOT.X16 + MODPLAY.PRG)"; \
	else \
	  echo "could not identify the card root; AUTOBOOT.X16 not installed"; \
	fi; \
	sync; \
	echo "deployed build $(BUILD_ID) to $$d"; \
	ls -l "$$d"/MODPLAY.PRG

# --- Veranda ------------------------------------------------------------------
# The player as a Veranda application (examples/veranda; cfg/veranda-mod.cfg).
# The SDK comes from a Veranda tree (VERANDA) into build/vsdk: make vsdk.
VERANDA ?= $(HOME)/Projects/geos
VSDK    ?= $(BUILD)/vsdk
VDIR     = $(BUILD)/veranda
VCC      = $(CL65) -t none --cpu 65C02 -O -Iinclude -Iexamples/veranda -I $(VSDK)/include
VPACK    = python3 -I -B $(abspath $(VSDK))/tools/vpack.py
VGLUE    = examples/veranda/vr_file.c examples/veranda/vr_audio.s

.PHONY: vsdk veranda veranda-trace veranda-gate veranda-stress veranda-rt veranda-app-gate veranda-mouse-gate
vsdk:
	$(VERANDA)/tools/mksdk.sh $(abspath $(VSDK))

# MODTRACE.cvt (TRACE_MODE: "" the polled loop, -DVIA_IRQ the interrupt,
# -DTRACE_TICKS=200 the sequencer's trace); always rebuilt, as the flags vary
veranda-trace: $(BUILD)/lib-trace/x16mod.lib
	@mkdir -p $(VDIR)
	$(VCC) -DCHUNK_LIMIT=$(CHUNK_LIMIT) $(TRACE_MODE) -C cfg/veranda-mod.cfg -m $(VDIR)/modtrace.map \
	    -o $(VDIR)/modtrace.bin examples/veranda/modtrace.c $(VGLUE) \
	    $(BUILD)/lib-trace/x16mod.lib $(VSDK)/lib/veranda.lib
	cd examples/veranda && $(VPACK) --load 0x0801 -o $(abspath $(VDIR))/MODTRACE.cvt modtrace.hdr $(abspath $(VDIR))/modtrace.bin $(abspath $(VDIR))/modtrace.bin.1

# The engine under Veranda (M2): chunk hashes polled and from the interrupt,
# host folder and SD card, the sequencer's trace (tools/gate_veranda.sh)
veranda-gate: $(BUILD)/lib-trace/x16mod.lib
	tools/gate_veranda.sh

# MODSTRESS.cvt (examples/veranda/test/modstress.c; M3): real time under a
# stress script; STRESS_FLAGS e.g. -DSECONDS=180
veranda-stress: $(BUILD)/lib/x16mod.lib
	@mkdir -p $(VDIR)
	$(VCC) $(STRESS_FLAGS) --asm-define VR_CYCLES --asm-include-dir $(VSDK)/include -C cfg/veranda-mod.cfg \
	    -m $(VDIR)/modstress.map -o $(VDIR)/modstress.bin examples/veranda/test/modstress.c \
	    examples/veranda/test/vr_play.s $(VGLUE) $(BUILD)/lib/x16mod.lib $(VSDK)/lib/veranda.lib
	cd examples/veranda/test && $(VPACK) --load 0x0801 -o $(abspath $(VDIR))/MODSTRESS.cvt stress.hdr $(abspath $(VDIR))/modstress.bin $(abspath $(VDIR))/modstress.bin.1

# Real time under a stress script (M3; tools/gate_veranda_rt.sh)
veranda-rt:
	tools/gate_veranda_rt.sh

# The Veranda MOD Player (examples/veranda/vmodplay.c and vmp_*: vmp.h says
# what is where; M4): build/veranda/MOD Player.cvt. VMP_FLAGS: -DVMP_TEST (the
# test session, test/vmp_test.h), -DVMP_STOP=n (a screenshot at its frame n),
# -DVMP_STRESS=s (the stress run), with --asm-define VR_CYCLES (the meters)
VMP_SRC = examples/veranda/vmodplay.c examples/veranda/vmp_ui.c examples/veranda/vmp_pat.c examples/veranda/vmp_len.c \
          examples/veranda/vmp_dlg.c examples/veranda/vmp_pics.c examples/veranda/vmp_draw.c examples/veranda/vmp_dlgbox.s $(VGLUE)
# Veranda Look 2 (its DECISIONS V79) from the SDK's sources: the window's
# chrome (vrwin.c) in overlay 2, the Open dialog (vrchoose.c) in overlay 5
# (main RAM has no room; vmodplay.c's win_* and choose() map them)
VMP_SDK = $(VDIR)/vrwin.o $(VDIR)/vrchoose.o
$(VDIR)/vrwin.o: $(VSDK)/src/vrwin.c $(VSDK)/include/vrwin.h
	@mkdir -p $(VDIR)
	$(VCC) --code-name OVERLAY2 --rodata-name OVL2RO -c -o $@ $<
$(VDIR)/vrchoose.o: $(VSDK)/src/vrchoose.c $(VSDK)/include/vrchoose.h
	@mkdir -p $(VDIR)
	$(VCC) --code-name OVERLAY5 --rodata-name OVL5RO -c -o $@ $<
veranda: $(BUILD)/lib/x16mod.lib $(VMP_SDK)
	@mkdir -p $(VDIR)
	$(VCC) $(VMP_FLAGS) --asm-include-dir $(VSDK)/include -C cfg/veranda-mod.cfg -m $(VDIR)/vmodplay.map \
	    -o $(VDIR)/vmodplay.bin $(VMP_SRC) $(if $(findstring VMP_TEST,$(VMP_FLAGS)),examples/veranda/test/vr_play.s) \
	    $(VMP_SDK) $(BUILD)/lib/x16mod.lib $(VSDK)/lib/veranda.lib
	cd examples/veranda && $(VPACK) --load 0x0801 -o "$(abspath $(VDIR))/MOD Player.cvt" app.hdr $(abspath $(VDIR))/vmodplay.bin $(abspath $(VDIR))/vmodplay.bin.1 $(abspath $(VDIR))/vmodplay.bin.2 \
	    $(abspath $(VDIR))/vmodplay.bin.3 $(abspath $(VDIR))/vmodplay.bin.4 $(abspath $(VDIR))/vmodplay.bin.5
	@grep -E "^(CODE|RODATA|DATA|BSS|ALIGNDATA|ALIGNBSS) " $(VDIR)/vmodplay.map | tail -1 | \
	    awk '{ printf "  the resident image ends at $$%s (the C stack from $$8600)\n", $$3 }'
	@ls -l $(VDIR)/vmodplay.bin.[1-5] | awk '{ printf "  overlay %s: %d bytes (8,191 at most)\n", $$NF, $$5 }'
# the desktop the player's gate quits to (DESKTOP.cvt): AFLOW, the FIFO, the vector
veranda-chk:
	@mkdir -p $(VDIR)
	$(VCC) -C $(VSDK)/cfg/app-c.cfg -o $(VDIR)/chk.bin examples/veranda/test/chk.c $(VSDK)/lib/veranda.lib
	cd examples/veranda/test && $(VPACK) --load 0x0801 -o $(abspath $(VDIR))/AudChk.cvt chk.hdr $(abspath $(VDIR))/chk.bin

# The player's gate (M4, M5; tools/gate_vmodplay.sh)
veranda-app-gate:
	tools/gate_vmodplay.sh

# The player through the real mouse and keyboard path, R49, booted from an SD
# card as on an X16: next, previous, a song's end (tools/gate_vmp_mouse.sh)
veranda-mouse-gate:
	tools/gate_vmp_mouse.sh
