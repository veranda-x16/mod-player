/*
 * host_seq.c -- run src/mod_play.c natively and dump a per-tick trace.
 *
 * The same sequencer source runs on the X16, so any disagreement between this
 * trace and the X16's is a porting bug, while a disagreement between this and
 * the micromod oracle is a ProTracker-semantics bug. Different causes,
 * different fixes -- which is why both comparisons are worth running.
 */
#include "mod_play.h"
#include "x16mod.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 16022

static uint8_t *filebuf;
static long     filelen;
static mod_info_t info;

/* Platform hook: patterns sit right after the header in the file image. */
void mod_read_note(uint8_t pattern, uint8_t row, uint8_t chan, uint8_t *out4)
{
    long off = MOD_HEADER_SIZE
             + ((long)pattern * MOD_ROWS + row) * MOD_CHANNELS * 4
             + (long)chan * 4;
    if (off + 4 > filelen) {
        memset(out4, 0, 4);
        return;
    }
    memcpy(out4, filebuf + off, 4);
}

/* Tracing only: no mixer behind this. */
void mod_apply(uint8_t ch) { (void)ch; }

static void emit(long n)
{
    uint8_t c;
    printf("%ld %d %u %d %d %u %u",
           n, mod_seq.order_pos, mod_seq_pattern(), mod_seq.row,
           mod_seq.tick, mod_seq.speed, mod_seq_tick_len(RATE));
    for (c = 0; c < MOD_CHANNELS; c++)
        printf(" %u,%u,%u,%d,%d,%d", mod_seq.chan[c].instrument,
               mod_seq.chan[c].period, mod_seq.chan[c].volume,
               mod_seq.chan[c].vibrato_add, mod_seq.chan[c].tremolo_add,
               mod_seq.chan[c].arpeggio_add);
    printf("\n");
}

int main(int argc, char **argv)
{
    FILE *f;
    long ticks = 600, n;

    if (argc < 2) {
        fprintf(stderr, "usage: host_seq file.mod [ticks]\n");
        return 2;
    }
    if (argc > 2)
        ticks = atol(argv[2]);

    f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    fseek(f, 0, SEEK_END); filelen = ftell(f); fseek(f, 0, SEEK_SET);
    filebuf = malloc(filelen);
    if (fread(filebuf, 1, filelen, f) != (size_t)filelen) return 1;
    fclose(f);

    if (mod_parse_header(filebuf, &info) != MOD_OK) {
        fprintf(stderr, "parse failed\n");
        return 1;
    }

    mod_seq_init(&info);
    for (n = 0; n < ticks; n++) {
        emit(n);
        mod_seq_tick();
    }
    return 0;
}
