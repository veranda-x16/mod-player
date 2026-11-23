/*
 * mod_bankload.c -- the library's loader: a MOD streamed into banked RAM.
 *
 * Target-neutral: the bytes come from the host's read function (mod_read_fn)
 * and go to the banks the host gives (first_bank, nbanks). The stand-alone
 * player reads through the KERNAL (src/mod_x16.c), Veranda through FRead.
 *
 * Patterns and sample data are streamed straight into the 8 KB window at
 * $A000, one bank after another, so nothing large is ever held in fixed RAM
 * -- the header buffer, which the caller lends, is the only sizeable
 * temporary.
 *
 * Each sample records where it landed as (bank, addr), which is exactly the
 * form src/cache.s wants; the conversion from file offset happens once, here,
 * rather than on every refill.
 */
#include "x16mod.h"

#include <string.h>

#define RAM_BANK_REG (*(volatile unsigned char *)0x0000)
#define WIN_BASE  0xA000
#define WIN_END   0xC000

/*
 * The bank the module starts at, once mod_load has run. The chunk hash
 * (CHUNK_HASH builds of src/mod_player.c) folds every bank RELATIVE to it, so
 * where a host puts the module changes no hash.
 */
unsigned char mod_first_bank;
static unsigned char ld_last;     /* the last bank we may write */

static unsigned char ld_bank;
static unsigned int  ld_addr;
static unsigned char ld_full;     /* banked RAM exhausted, or the file ended */
static mod_read_fn   ld_rd;

/* n bytes or the end of the stream: a read function may return short. */
static unsigned int rd_full(unsigned char *buf, unsigned int n)
{
    unsigned int got = 0, r;
    while (got < n) {
        r = ld_rd(buf + got, n - got);
        if (r == 0)
            break;
        got += r;
    }
    return got;
}

/*
 * The data lands in a fixed-RAM staging buffer first and is copied into the
 * bank afterwards, so the RAM window is NEVER left pointing at a module bank
 * across a call to the host's read function. On the X16, bank 0 holds
 * CMDR-DOS's own variables and buffers; a host filesystem does not care what
 * the window shows, a real card does.
 *
 * 256 bytes: each staged chunk costs the host a read call (a CHKIN/CLRCHN
 * round trip on the KERNAL), measured at ~30 ms per halving on a 129 KB
 * module; fixed RAM is the scarcer thing.
 */
static unsigned char stage[256];

/*
 * Load progress, for a UI that would otherwise sit frozen for a second with
 * no way to tell a slow card from a hung one. The loader stays ignorant of
 * the display: it counts bytes and calls a hook.
 */
unsigned long mod_load_total;           /* bytes to read, once the header parses */
unsigned long mod_load_done;            /* bytes staged so far */
void (*mod_load_tick)(void);            /* may be NULL */
static unsigned char ld_tick_div;

static signed char ld_read(unsigned long n)
{
    unsigned int chunk;
    unsigned char save;

    while (n) {
        if (ld_full)
            return -1;

        chunk = sizeof(stage);
        if ((unsigned long)chunk > n)
            chunk = (unsigned int)n;
        /* never let a copy straddle the window boundary */
        if (chunk > (unsigned int)(WIN_END - ld_addr))
            chunk = (unsigned int)(WIN_END - ld_addr);

        if (rd_full(stage, chunk) != chunk) {
            /* Short read: the file ended early. Plenty of real modules are
               stored with the last sample truncated, so accept what arrived
               and leave the rest silent rather than rejecting the module. */
            ld_full = 1;
            return 0;
        }

        save = RAM_BANK_REG;
        RAM_BANK_REG = ld_bank;
        /* memcpy is hand-written assembly in the cc65 runtime; the equivalent
           C loop compiles to several times the work per byte. */
        memcpy((void *)ld_addr, stage, chunk);
        RAM_BANK_REG = save;

        ld_addr += chunk;
        n -= chunk;

        /* Every 16th chunk: 4 KB, about 30 updates over a large module. */
        mod_load_done += chunk;
        if (mod_load_tick && ++ld_tick_div >= 16) {
            ld_tick_div = 0;
            mod_load_tick();
        }
        if (ld_addr >= WIN_END) {
            ld_addr = WIN_BASE;
            /* ld_bank is 8-bit, so the exhaustion test has to happen BEFORE
               the increment can wrap it to bank 0 (the KERNAL's). */
            if (ld_bank == ld_last)
                ld_full = 1;
            else
                ld_bank++;
        }
    }
    return 0;
}

mod_err_t mod_open(mod_read_fn rd, mod_info_t *m, uint8_t *hdrbuf)
{
    ld_rd = rd;
    if (rd_full(hdrbuf, MOD_HEADER_SIZE) != MOD_HEADER_SIZE)
        return MOD_ERR_SHORT;
    return mod_parse_header(hdrbuf, m);
}

unsigned char mod_banks_needed(const mod_info_t *m)
{
    unsigned long b = (unsigned long)m->num_patterns * MOD_PATTERN_SIZE
                    + mod_sample_bytes(m);
    b = (b + 8191UL) >> 13;
    return b > 255UL ? 255 : (unsigned char)b;
}

mod_err_t mod_load(mod_read_fn rd, mod_info_t *m,
                   unsigned char first_bank, unsigned char nbanks)
{
    unsigned char i, save = RAM_BANK_REG;
    unsigned long patbytes;
    mod_err_t e = MOD_OK;

    if (nbanks == 0)
        return MOD_ERR_NOMEM;
    ld_rd = rd;
    mod_first_bank = first_bank;
    ld_last = (unsigned char)(first_bank + nbanks - 1);
    ld_bank = first_bank;
    ld_addr = WIN_BASE;
    ld_full = 0;

    /* Now the header has parsed, the total is known: patterns plus every
       sample. Published before the first read so a progress bar has a scale
       from the outset rather than growing one. */
    patbytes = (unsigned long)m->num_patterns * MOD_PATTERN_SIZE;
    mod_load_total = patbytes + mod_sample_bytes(m);
    mod_load_done = 0;
    ld_tick_div = 0;
    if (mod_load_tick)
        mod_load_tick();

    /* Patterns first, contiguously; the sequencer indexes them by number. */
    m->pattern_bank = ld_bank;
    if (ld_read(patbytes) != 0) {
        e = MOD_ERR_NOMEM;
        goto out;
    }

    /* Then each sample, remembering where it landed. */
    for (i = 0; i < MOD_MAX_SAMPLES; i++) {
        mod_sample_t *s = &m->samples[i];
        if (s->length == 0)
            continue;
        s->bank = ld_bank;
        s->addr = ld_addr;
        if (ld_read(s->length) != 0) {
            e = MOD_ERR_NOMEM;
            goto out;
        }
    }
out:
    RAM_BANK_REG = save;
    return e;
}

/* Advance a (bank, addr) position by n bytes, wrapping the 8 KB window. */
void mod_advance_pos(unsigned char *bank, unsigned int *addr, unsigned long n)
{
    unsigned long a = (unsigned long)*addr + n;
    while (a >= WIN_END) {
        a -= (WIN_END - WIN_BASE);
        (*bank)++;
    }
    *addr = (unsigned int)a;
}
