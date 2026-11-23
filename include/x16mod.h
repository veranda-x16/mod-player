/*
 * x16mod -- ProTracker MOD replay library for the Commander X16.
 *
 * The replay engine knows nothing about VERA. It writes sample/period/volume
 * /trigger into four voice states; a mixer behind that boundary turns them
 * into PCM. That is what allows the 65C02 mixer to be swapped (for a 65C816
 * one, say) without touching MOD playback.
 */
#ifndef X16MOD_H
#define X16MOD_H

#include <stdint.h>

#define MOD_MAX_SAMPLES   31
#define MOD_CHANNELS       4
#define MOD_ROWS          64
#define MOD_HEADER_SIZE 1084          /* 20 + 31*30 + 1 + 1 + 128 + 4 */
#define MOD_PATTERN_SIZE 1024         /* 64 rows * 4 channels * 4 bytes */

typedef enum {
    MOD_OK = 0,
    MOD_ERR_OPEN,
    MOD_ERR_SHORT,        /* file ended inside the header */
    MOD_ERR_SIGNATURE,    /* not a 31-instrument 4-channel MOD we support */
    MOD_ERR_ORDER,        /* song length out of range */
    MOD_ERR_NOMEM         /* not enough banked RAM for the sample data */
} mod_err_t;

typedef struct {
    char     name[23];
    uint32_t length;      /* bytes; the file stores words */
    uint32_t loop_start;  /* bytes */
    uint32_t loop_len;    /* bytes; <= 2 means "no loop" */
    uint8_t  volume;      /* 0..64 */
    int8_t   finetune;    /* -8..+7 */
    uint8_t  bank;        /* where it was loaded, once loaded */
    uint16_t addr;        /* $A000..$BFFF */
} mod_sample_t;

typedef struct {
    char         title[21];
    char         sig[5];
    uint8_t      song_length;   /* 1..128 positions */
    uint8_t      restart;
    uint8_t      num_patterns;  /* highest order entry + 1 */
    uint8_t      order[128];
    mod_sample_t samples[MOD_MAX_SAMPLES];
    uint8_t      pattern_bank;  /* first bank holding pattern data */
} mod_info_t;

/* Parse a MOD_HEADER_SIZE-byte header. Pure: no I/O, no allocation, so the
   same code runs natively in the test oracle and on the X16. */
mod_err_t mod_parse_header(const uint8_t *hdr, mod_info_t *out);

/* Total bytes of sample data implied by the descriptors. */
uint32_t  mod_sample_bytes(const mod_info_t *m);

/* The stand-alone player's error texts (src/mod_strerror.c): not in the
   library, whose objects carry no strings -- a host prints its own. */
const char *mod_strerror(mod_err_t e);

/* ======================================================================
 * The library (build/lib/x16mod.lib: make lib).
 *
 * No I/O of its own, no banks chosen, no strings. The host supplies a read
 * function, the banks, and the way the FIFO is fed: polled (mod_update from
 * a loop, the stand-alone player) or by VERA's AFLOW interrupt
 * (mod_irq_batch from the host's dispatcher, Veranda).
 * ====================================================================== */

/* Read up to n bytes of the open module into buf; the count read (0 at the
   end). The library calls it again after a short count until 0. */
typedef unsigned int (*mod_read_fn)(void *buf, unsigned int n);

void          mod_init(void);
/* The header (MOD_HEADER_SIZE bytes, read into hdrbuf, which the caller
   lends) parsed into *m. The stream is left at the first pattern. */
mod_err_t     mod_open(mod_read_fn rd, mod_info_t *m, uint8_t *hdrbuf);
/* Patterns plus samples in 8 KB banks, rounded up (255 at most). */
unsigned char mod_banks_needed(const mod_info_t *m);
/* The rest of the stream into banks [first, first + nbanks): MOD_ERR_NOMEM
   if they run out. The caller's RAM bank is mapped again on return. */
mod_err_t     mod_load(mod_read_fn rd, mod_info_t *m,
                       unsigned char first_bank, unsigned char nbanks);
extern void (*mod_load_tick)(void);   /* every 4 KB loaded; may be 0 */
extern unsigned long mod_load_total, mod_load_done;

void mod_play(const mod_info_t *m, unsigned char order);  /* primes, starts */
void mod_stop(void);
void mod_pause(unsigned char on);     /* the DAC held, the FIFO kept */
void mod_set_master_volume(unsigned char v);              /* 0-15 */
void mod_request_position(unsigned char order);  /* at the next batch */

/* Feeding the FIFO: one of the two. */
void mod_update(void);     /* polled: renders a batch when AFLOW is set */
/* asm: one batch, for the host's AFLOW dispatcher, which calls it with
   AFLOW masked in IEN and interrupts on. It saves and restores the RAM
   bank and cc65's zero page and runs on its own C stack. The dispatcher
   unmasks AFLOW afterwards only while mod_irq_armed is non-zero. */
void mod_irq_batch(void);
/* Set by the host once its dispatcher is in place: mod_play then enables
   AFLOW in IEN (mod_irq_armed = 1); mod_stop disables it. */
extern unsigned char mod_use_irq;
extern volatile unsigned char mod_irq_armed;

typedef struct {           /* copied at the end of every batch */
    uint8_t  order, pattern, row, speed, bpm;
    uint8_t  song_ends;    /* counts each wrap of the song */
    uint16_t elapsed, underruns;  /* seconds played, chunks found starved */
    uint8_t  peak[4];      /* 0-128, from the voices' cache pages */
    uint8_t  vol[4];       /* 0-64, the channels' effective volume */
    uint8_t  playing;
} mod_status_t;
extern volatile mod_status_t mod_status;
extern volatile unsigned int mod_batches;    /* batches rendered */

/* Pattern row as 16 bytes (4 channels x 4), bank-safe. */
void mod_read_row(const mod_info_t *m, uint8_t pattern, uint8_t row,
                  uint8_t *out16);

#endif
