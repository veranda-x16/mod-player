/*
 * mod_load.c -- ProTracker MOD parsing.
 *
 * Portable C on purpose: the identical source is compiled natively for the
 * test oracle and with cc65 for the X16, so a disagreement between the two is
 * a porting bug rather than a question of interpretation. Nothing here does
 * I/O or allocates.
 *
 * Supported: 31-instrument, 4-channel ProTracker -- M.K., M!K!, 4CHN, FLT4.
 */
#include "x16mod.h"

#include <string.h>

#define SIG_OFFSET   1080
#define ORDER_OFFSET  952

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static void copy_name(char *dst, const uint8_t *src, uint8_t n)
{
    uint8_t i;
    for (i = 0; i < n; i++)
        dst[i] = (char)src[i];
    dst[n] = '\0';
    /* Tracker names are space- or garbage-padded; trim the tail. */
    while (n && (dst[n - 1] == ' ' || (uint8_t)dst[n - 1] < 0x20))
        dst[--n] = '\0';
}

/* Compared as raw byte values, never as string literals. cc65 translates
   ASCII literals to PETSCII for CBM targets, so "M.K." in this source would
   not equal the ASCII "M.K." sitting in the file. The format is defined in
   ASCII bytes, so that is what the comparison has to say. */
static uint8_t sig_is(const uint8_t *s, uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return (uint8_t)(s[0] == a && s[1] == b && s[2] == c && s[3] == d);
}

/*
 * Accept the 4-channel 31-instrument signatures. Trackers varied the first two
 * bytes freely (M&K! and N.T. are the same format as M.K.), so like other
 * players we key off the last two. xCHN / xxCH announce a channel count this
 * mixer cannot serve and are rejected rather than misplayed.
 */
static uint8_t sig_ok(const uint8_t *s)
{
    if (s[2] == 0x4B && s[3] == 0x2E) return 1;        /* M.K.          */
    if (s[2] == 0x4B && s[3] == 0x21) return 1;        /* M!K!, M&K!    */
    if (s[2] == 0x54 && s[3] == 0x2E) return 1;        /* N.T.          */
    if (s[2] == 0x54 && s[3] == 0x34) return 1;        /* FLT4          */
    if (sig_is(s, 0x34, 0x43, 0x48, 0x4E)) return 1;   /* 4CHN          */
    return 0;
}

mod_err_t mod_parse_header(const uint8_t *hdr, mod_info_t *out)
{
    const uint8_t *p;
    uint8_t i, hi;

    if (!sig_ok(hdr + SIG_OFFSET))
        return MOD_ERR_SIGNATURE;
    memcpy(out->sig, hdr + SIG_OFFSET, 4);
    out->sig[4] = '\0';

    copy_name(out->title, hdr, 20);

    for (i = 0; i < MOD_MAX_SAMPLES; i++) {
        mod_sample_t *s = &out->samples[i];
        p = hdr + 20 + (uint16_t)i * 30;

        copy_name(s->name, p, 22);
        s->length     = (uint32_t)be16(p + 22) * 2;
        /* Finetune is a signed 4-bit field in the low nibble. */
        s->finetune   = (int8_t)(p[24] & 0x0F);
        if (s->finetune > 7)
            s->finetune -= 16;
        s->volume     = p[25] > 64 ? 64 : p[25];
        s->loop_start = (uint32_t)be16(p + 26) * 2;
        s->loop_len   = (uint32_t)be16(p + 28) * 2;

        /* A loop shorter than 4 bytes means "no loop" -- the one-word idiom
           plus the degenerate cases. Clamp loops that run past the sample;
           plenty of real modules do this and a player that trusts them reads
           out of bounds. */
        if (s->loop_len < 4) {
            s->loop_len = 0;
            s->loop_start = 0;
        } else if (s->loop_start >= s->length) {
            s->loop_len = 0;
            s->loop_start = 0;
        } else if (s->loop_start + s->loop_len > s->length) {
            s->loop_len = s->length - s->loop_start;
        }

        s->bank = 0;
        s->addr = 0;
    }

    out->song_length = hdr[950];
    out->restart     = hdr[951];
    if (out->song_length == 0 || out->song_length > 128)
        return MOD_ERR_ORDER;

    memcpy(out->order, hdr + ORDER_OFFSET, 128);

    /* Pattern count is the highest index that appears. Scan all 128 entries,
       not just song_length: the file stores patterns for every index present,
       and truncating here would misplace the sample data that follows. */
    hi = 0;
    for (i = 0; i < 128; i++)
        if (out->order[i] > hi)
            hi = out->order[i];
    out->num_patterns = hi + 1;
    out->pattern_bank = 0;

    return MOD_OK;
}

uint32_t mod_sample_bytes(const mod_info_t *m)
{
    uint32_t total = 0;
    uint8_t i;
    for (i = 0; i < MOD_MAX_SAMPLES; i++)
        total += m->samples[i].length;
    return total;
}
