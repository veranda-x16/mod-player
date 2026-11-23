/*
 * vmp_songlen.c -- the Veranda MOD Player's song length (its walk,
 * examples/veranda/vmp_len.c, built natively with -DVMP_HOST) against
 * micromod's micromod_calculate_song_duration, module by module.
 *
 *   vmp_songlen <file.mod> ...    "ok <s> <name>" or "DIFF <ours> <micromod>"
 *
 * The player shows whole seconds; a difference of one is rounding (its
 * ticks are 2.5 / BPM s exactly, micromod counts whole samples). Exit 1
 * on a larger difference. MIT licence (see LICENSE).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "x16mod.h"
#include "micromod.h"

mod_info_t mod;
unsigned int song_secs;
void song_scan(void);

static unsigned char *data;

void mod_read_row(const mod_info_t *m, uint8_t pattern, uint8_t row, uint8_t *out16)
{
    (void)m;
    memcpy(out16, data + MOD_HEADER_SIZE + (size_t)pattern * MOD_PATTERN_SIZE + (size_t)row * 16, 16);
}

int main(int argc, char **argv)
{
    int i, bad = 0, n = 0;
    for (i = 1; i < argc; ++i) {
        FILE *f = fopen(argv[i], "rb");
        long len, ref;
        if (!f)
            continue;
        fseek(f, 0, SEEK_END);
        len = ftell(f);
        fseek(f, 0, SEEK_SET);
        data = calloc(1, (size_t)len + 128 * MOD_PATTERN_SIZE);
        if (fread(data, 1, (size_t)len, f) != (size_t)len) {
            fclose(f);
            continue;
        }
        fclose(f);
        if (len < MOD_HEADER_SIZE || mod_parse_header(data, &mod) != MOD_OK
            || micromod_initialise((signed char *)data, 48000)) {
            free(data);
            continue;                   /* (not a module both take) */
        }
        song_scan();
        ref = (micromod_calculate_song_duration() + 24000) / 48000;
        ++n;
        if (labs((long)song_secs - ref) > 1) {
            printf("DIFF %u %ld %s\n", song_secs, ref, argv[i]);
            bad = 1;
        } else
            printf("ok   %u %s\n", song_secs, argv[i]);
        free(data);
    }
    printf("%d modules\n", n);
    return bad;
}
