/*
 * host_modinfo.c -- native driver for src/mod_load.c.
 *
 * Prints what the parser sees, so it can be diffed against an independent
 * player (openmpt123 --info, and later micromod). The parser itself is the
 * same source the X16 build compiles.
 */
#include "x16mod.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    uint8_t hdr[MOD_HEADER_SIZE];
    mod_info_t m;
    mod_err_t e;
    FILE *f;
    long filesize;
    int i, verbose = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: host_modinfo [-v] file.mod\n");
        return 2;
    }
    if (argv[1][0] == '-' && argv[1][1] == 'v') {
        verbose = 1;
        argv++;
    }

    f = fopen(argv[1], "rb");
    if (!f) {
        printf("error: %s\n", mod_strerror(MOD_ERR_OPEN));
        return 1;
    }
    fseek(f, 0, SEEK_END);
    filesize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fread(hdr, 1, MOD_HEADER_SIZE, f) != MOD_HEADER_SIZE) {
        fclose(f);
        printf("error: %s\n", mod_strerror(MOD_ERR_SHORT));
        return 1;
    }
    fclose(f);

    e = mod_parse_header(hdr, &m);
    if (e != MOD_OK) {
        printf("error: %s\n", mod_strerror(e));
        return 1;
    }

    printf("Title:     %s\n", m.title);
    printf("Signature: %s\n", m.sig);
    printf("Length:    %u\n", m.song_length);
    printf("Patterns:  %u\n", m.num_patterns);
    printf("Samples:   %u\n", (unsigned)MOD_MAX_SAMPLES);
    printf("SampleB:   %lu\n", (unsigned long)mod_sample_bytes(&m));

    /* The header, the patterns and the sample data should account for the
       whole file. A mismatch means the pattern count is wrong, which is the
       classic way to misparse a MOD. */
    {
        long expect = MOD_HEADER_SIZE
                    + (long)m.num_patterns * MOD_PATTERN_SIZE
                    + (long)mod_sample_bytes(&m);
        printf("FileSize:  %ld\n", filesize);
        printf("Accounted: %ld %s\n", expect,
               expect == filesize ? "(exact)" : "(MISMATCH)");
    }

    if (verbose) {
        printf("Order:    ");
        for (i = 0; i < m.song_length; i++)
            printf(" %u", m.order[i]);
        printf("\n");
        for (i = 0; i < MOD_MAX_SAMPLES; i++) {
            mod_sample_t *s = &m.samples[i];
            if (!s->length)
                continue;
            printf("  %2d %-22s len=%-6lu vol=%-2u ft=%-3d loop=%lu+%lu\n",
                   i + 1, s->name, (unsigned long)s->length, s->volume,
                   s->finetune, (unsigned long)s->loop_start,
                   (unsigned long)s->loop_len);
        }
    }
    return 0;
}
