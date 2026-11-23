/*
 * oracle_micromod.c -- the reference trace, produced by micromod.
 *
 * micromod is Martin Cameron's ProTracker replay, BSD-3-Clause, fetched into
 * vendor/ by "make micromod". Its sequencer state lives in file statics, so
 * this driver includes the implementation directly in order to read them;
 * nothing in vendor/ is modified.
 *
 * Emits exactly the format host_seq.c does, so the two are diffable.
 */
#include "micromod.c"

#include <stdio.h>
#include <stdlib.h>

#define RATE 16022

static void emit(long n)
{
    long c;
    printf("%ld %ld %u %ld %ld %ld %ld",
           n, pattern, (unsigned)(sequence[pattern] & 0x7F), row, tick,
           speed, tick_len);
    for (c = 0; c < 4; c++)
        printf(" %u,%u,%u,%d,%d,%d", channels[c].instrument, channels[c].period,
               channels[c].volume, channels[c].vibrato_add,
               channels[c].tremolo_add, channels[c].arpeggio_add);
    printf("\n");
}

int main(int argc, char **argv)
{
    FILE *f;
    long len, ticks = 600, n;
    signed char *data;

    if (argc < 2) {
        fprintf(stderr, "usage: oracle_micromod file.mod [ticks]\n");
        return 2;
    }
    if (argc > 2)
        ticks = atol(argv[2]);

    f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
    data = malloc(len);
    if (fread(data, 1, len, f) != (size_t)len) return 1;
    fclose(f);

    if (micromod_initialise(data, RATE) != 0) {
        fprintf(stderr, "micromod rejected the module\n");
        return 1;
    }

    for (n = 0; n < ticks; n++) {
        emit(n);
        sequence_tick();
    }
    return 0;
}
