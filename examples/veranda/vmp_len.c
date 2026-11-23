/*
 * vmp_len.c -- the Veranda MOD Player's overlay 2, part two (vmp.h): the
 * song's length, for the time shown as "elapsed / length" and the
 * progress bar. Built natively too (-DVMP_HOST: tests/vmp_songlen.c),
 * checked against micromod's micromod_calculate_song_duration.
 *
 * MIT licence (see LICENSE).
 */
#ifdef VMP_HOST
#include <stdint.h>
#include <string.h>
#include "x16mod.h"
extern mod_info_t mod;
extern unsigned int song_secs;
#else
#include "vmp.h"
#pragma code-name ("OVERLAY2")
#pragma rodata-name ("OVL2RO")
#endif

/*---------------------------------------------------------------
  the song's length: the sequencer's walk (src/mod_play.c) without the
  sound -- speed and tempo (Fxx), position jumps (Bxx), pattern breaks
  (Dxx), pattern loops (E6x), pattern delays (EEx) -- to its first jump
  back (where the engine counts the song's end). A tick is 2.5 / BPM s.
 ---------------------------------------------------------------*/
static unsigned char pl_row[4];

void song_scan(void)
{
    unsigned char row16[16];
    unsigned char order = 0, row = 0, speed = 6, bpm = 125, c, e, p;
    unsigned int ticks;             /* (EEx: up to 31 x 16) */
    signed char pl_count = -1, pl_chan = -1;
    int break_order, next_row;
    unsigned long us = 0, tick_us = 20000;
    uint16_t n = 0;

    memset(pl_row, 0, 4);
    for (;;) {
        mod_read_row(&mod, mod.order[order], row, row16);
        break_order = -1;
        next_row = row + 1;
        ticks = speed;
        for (c = 0; c < 4; ++c) {
            e = row16[4 * c + 2] & 15;
            p = row16[4 * c + 3];
            if (e == 0x0E) {
                e = (unsigned char)(0x10 | (p >> 4));
                p &= 15;
            }
            switch (e) {
            case 0x0B:
                if (pl_count < 0) {
                    break_order = p;
                    next_row = 0;
                }
                break;
            case 0x0D:
                if (pl_count < 0) {
                    if (break_order < 0)
                        break_order = order + 1;
                    next_row = (p >> 4) * 10 + (p & 15);
                    if (next_row >= 64)
                        next_row = 0;
                }
                break;
            case 0x0F:
                if (p) {
                    if (p < 32)
                        ticks = speed = p;
                    else {
                        bpm = p;
                        tick_us = 2500000UL / bpm;
                    }
                }
                break;
            case 0x16:
                if (p == 0)
                    pl_row[c] = row;
                if (pl_row[c] < row && break_order < 0) {
                    if (pl_count < 0) {
                        pl_count = (signed char)p;
                        pl_chan = (signed char)c;
                    }
                    if (pl_chan == (signed char)c) {
                        if (pl_count == 0)
                            pl_row[c] = row + 1;
                        else
                            next_row = pl_row[c];
                        --pl_count;
                    }
                }
                break;
            case 0x1E:
                ticks = speed + speed * (unsigned int)p;
                break;
            }
        }
        us += tick_us * ticks;
        if (++n == 0)
            break;                      /* (65,536 rows: no end found) */
        if (next_row >= 64) {
            next_row = 0;
            if (break_order < 0)
                break_order = order + 1;
        }
        if (break_order >= 0) {
            if (break_order >= mod.song_length || break_order <= order)
                break;                  /* the song's end */
            order = (unsigned char)break_order;
            memset(pl_row, 0, 4);
        }
        row = (unsigned char)next_row;
    }
    song_secs = (unsigned int)((us + 500000UL) / 1000000UL);
}
