/*
 * t_chunk.c -- deterministic equivalence + load harness for the render loop.
 *
 * Two things the WAV route cannot do.
 *
 * Equivalence (-DCHUNK_HASH): the FIFO is the one non-deterministic input to
 * mod_player_update -- how often AFLOW fires depends on when the HOST's audio
 * device drains it. Measured: two captures of one unmodified binary differ in
 * ~0.09% of frames, which is more than a real regression need be. -DCHUNK_TRACE
 * compiles that gate out, making the loop a pure function of the module, and
 * the hash then covers everything the mixer's output depends on: the frame
 * count of every chunk, all four phases, all four read positions. Page
 * contents need no hashing; they are a pure function of the position filled
 * from. Equal hash sequence => bit-identical audio.
 *
 * Load (without -DCHUNK_HASH): total cycles for a fixed number of frames, from
 * the emulator's guest-readable counter. The hash itself costs far more than
 * the code under test, so the two modes are deliberately exclusive.
 *
 * Output goes through $9FBB (raw emulator stdout), not printf: 20,000 lines
 * through the screen editor overruns -echo and loses all but a handful.
 */
#include "x16mod.h"
#include "mod_play.h"
#include "mod_player.h"

extern mod_err_t mod_load_file(const char *filename, mod_info_t *m,
                              unsigned char *hdrbuf);
extern void mod_hooks_init(const mod_info_t *m);
extern unsigned long chunk_count;
#ifdef CHUNK_HASH
extern unsigned long chunk_hash;
#endif

#define EMU_OUT (*(unsigned char *)0x9FBB)

/* tests/t_chunk_cyc.s -- the counter's snapshot semantics need the four byte
   reads kept in order, which C cannot be trusted to do. */
extern void __fastcall__ cyc_latch(unsigned char dummy);
extern unsigned long cyc_now(void);

#ifndef PERIOD_ALL
#define PERIOD_ALL 0
#endif
#ifndef CHUNK_LIMIT
#define CHUNK_LIMIT 20000
#endif

static unsigned char hdrbuf[MOD_HEADER_SIZE];
static mod_info_t mod;

/* Numeric, not character literals: cc65 translates string and char literals
   to PETSCII for CBM targets, and $9FBB emits the byte verbatim -- so '\n'
   would arrive as a CR and the whole trace would land on one line. */
#define NL   10
#define SP   32

static void put(unsigned char c) { EMU_OUT = c; }

static void hex32(unsigned long v)
{
    signed char i;
    for (i = 28; i >= 0; i -= 4) {
        unsigned char d = (unsigned char)((v >> i) & 15);
        put(d < 10 ? 48 + d : 97 + d - 10);      /* '0'..'9', 'a'..'f' */
    }
}

int main(void)
{
    unsigned long spent = 0, t0;
    unsigned long updates = 0;

    if (mod_load_file("music.mod", &mod, hdrbuf) != MOD_OK) {
        put(69); put(NL);               /* 'E' */
        return 1;
    }
    mod_hooks_init(&mod);
    mod_seq_init(&mod);
    mod_player_start();

    cyc_latch(0);
    while (chunk_count < CHUNK_LIMIT) {
        t0 = cyc_now();
        mod_player_update();
        spent += cyc_now() - t0;
        updates++;
#ifdef CHUNK_HASH
        /* Periodic, so a divergence is reported near where it started rather
           than only as "the totals differ". */
        if (PERIOD_ALL || (updates & 15) == 0) {
            hex32(chunk_count); put(SP); hex32(chunk_hash); put(NL);
        }
#endif
    }
    put(100); put(111); put(110); put(101); put(SP);   /* "done " */
    hex32(chunk_count); put(SP);
#ifdef CHUNK_HASH
    hex32(chunk_hash);
#else
    hex32(0);
#endif
    put(SP);
    hex32(updates); put(SP);
    hex32(spent); put(NL);
    return 0;
}
