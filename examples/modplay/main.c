/*
 * MODPLAY.PRG -- load a module and report what is in it.
 *
 * Phase 5 deliverable: this is the "print the header before attempting
 * playback" milestone from the plan. Playback arrives with the sequencer.
 */
#include "x16mod.h"
#include "mod_play.h"

#include <conio.h>
#include <stdio.h>
#include <string.h>

extern mod_err_t mod_load_file(const char *filename, mod_info_t *m,
                              unsigned char *hdrbuf);
extern void mod_hooks_init(const mod_info_t *m);

#ifndef TRACE_TICKS
#define TRACE_TICKS 0
#endif
extern int mod_last_read;
extern unsigned char kf_eof;

static unsigned char hdrbuf[MOD_HEADER_SIZE];
static mod_info_t mod;


int main(void)
{
    /* Lowercase on purpose. cc65 translates ASCII string literals to
       PETSCII for CBM targets: 'M' (0x4D) becomes $CD, the SHIFTED M, which
       CMDR-DOS does not match against a file called MUSIC.MOD. Lowercase 'm'
       (0x6D) maps to $4D, the unshifted M that BASIC's OPEN would send. */
    const char *name = "music.mod";
    mod_err_t e;
    unsigned char i, shown;

    printf("x16-mod loader\n\n");

    e = mod_load_file(name, &mod, hdrbuf);
    if (e != MOD_OK) {
        printf("cannot load %s:\n  %s\n", name, mod_strerror(e));
        return 1;
    }

    printf("title:    %s\n", mod.title);
    printf("format:   %s\n", mod.sig);
    printf("length:   %u\n", mod.song_length);
    printf("patterns: %u\n", mod.num_patterns);
    printf("samples:  %u\n", (unsigned)MOD_MAX_SAMPLES);
    printf("pat bank: %u\n", mod.pattern_bank);

#if TRACE_TICKS
    /* Dump the same per-tick trace host_seq.c emits, so the X16 build can be
       diffed against the native build of the identical sequencer source. */
    {
        unsigned int t;
        unsigned char c;
        mod_hooks_init(&mod);
        mod_seq_init(&mod);
        for (t = 0; t < TRACE_TICKS; t++) {
            printf("%u %d %u %d %d %u %u", t, mod_seq.order_pos,
                   mod_seq_pattern(), mod_seq.row, mod_seq.tick,
                   mod_seq.speed, mod_seq_tick_len(16022));
            for (c = 0; c < MOD_CHANNELS; c++)
                printf(" %u,%u,%u,%d,%d,%d", mod_seq.chan[c].instrument,
                       mod_seq.chan[c].period, mod_seq.chan[c].volume,
                       mod_seq.chan[c].vibrato_add, mod_seq.chan[c].tremolo_add,
                       mod_seq.chan[c].arpeggio_add);
            printf("\n");
            mod_seq_tick();
        }
        return 0;
    }
#endif

    shown = 0;
    for (i = 0; i < MOD_MAX_SAMPLES; i++) {
        if (mod.samples[i].length == 0)
            continue;
        printf("%2u %-12s %5lu v%02u b%u:%04x\n",
               i + 1, mod.samples[i].name,
               (unsigned long)mod.samples[i].length,
               mod.samples[i].volume,
               mod.samples[i].bank, mod.samples[i].addr);
        if (++shown >= 8)
            break;
    }
    return 0;
}
