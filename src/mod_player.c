/*
 * mod_player.c -- the real-time render loop.
 *
 * Joins the three halves that until now only existed separately: the
 * sequencer ticking at ~50 Hz, the sample cache pulling from banked RAM, and
 * the 65C02 mixer feeding VERA's FIFO.
 *
 * Rendering is bounded three ways, and the tightest bound wins:
 *   - the tick, so the sequencer runs at exactly the right moment
 *   - the FIFO, so we never spin once it is full
 *   - each voice's 256-byte cache page
 *
 * The cache bound is the subtle one. The mixer's phase index is 8 bits, so it
 * wraps silently at 256; if a chunk let it wrap, every frame after the wrap
 * would read the stale tail of the old page. So a page is topped up BEFORE
 * that can happen, and a chunk is capped at half the smallest remaining
 * headroom -- ProTracker clamps the period at 113, which at 16 kHz is a step
 * of 1.96, so half the headroom can never be overrun.
 */
#include "mod_play.h"
#include "mod_player.h"
#include "x16mod.h"

/* src/mixer_api.s */
extern void mix_frames(unsigned char n);
extern void cache_refill_ch(unsigned char v);
/* Refills the low pages and returns the frame count the chunk is bounded to.
   src/mixer_api.s -- see the comment at the call site. */
extern unsigned char chunk_prepare(void);
extern void audio_init_rate(unsigned char rate);
extern void audio_start(void);
extern void audio_stop(void);
extern unsigned char audio_fifo_full(void);
extern unsigned char audio_fifo_empty(void);
extern unsigned char audio_aflow(void);

#define RATE_REG   42          /* 16021.7 Hz */
#define RATE_HZ    16022
/*
 * Frames we may add once AFLOW says the FIFO is below 1/4 (1024 bytes).
 *
 * The mixer writes left and right as two separate stores, and VERA discards a
 * write to a full FIFO -- so if the FIFO fills BETWEEN them, the right byte is
 * lost, the byte count goes odd, and every frame after that is swapped. The
 * stream only re-syncs when an underrun flushes the odd byte, which is why the
 * fault comes and goes rather than staying put.
 *
 * The FIFO's usable capacity is 4095 bytes, not 4096 (vera_pcm.c accepts a
 * write only while cnt < sizeof(fifo)-1) -- an ODD number, so filling it to
 * the brim splits a pair by construction. 1024 frames is 2048 bytes, which
 * from a worst-case 1023 reaches 3071 and leaves a full kilobyte of margin.
 */
#define AFLOW_FRAMES 1024

void mod_render_batch(void);

static unsigned int tick_remaining;
static unsigned char playing;

/* Diagnostics: frames actually rendered, and how often the FIFO was found
   empty (i.e. we failed to keep up). */
unsigned int  mod_frames_rendered;   /* in units of 16 frames */
unsigned int  mod_underruns;
static unsigned char mod_frames_acc;
unsigned int  mod_ticks_run;
/* Elapsed seconds, counted rather than derived. mod_frames_rendered is a
   16-bit count of 16-frame units, so it wraps after about 65 seconds and the
   clock reset to 00:00 mid-song. */
unsigned int  mod_seconds;
static unsigned int sec_acc;

void mod_player_start(void)
{
    unsigned char v;
    audio_init_rate(RATE_REG);
    for (v = 0; v < MOD_CHANNELS; v++)
        cache_refill_ch(v);
    tick_remaining = 0;
    playing = 1;
#ifndef CHUNK_TRACE
    /* Prime before starting playback, or the first fill races the DAC
       (X16 Reference ch.9). AFLOW is set while the FIFO is under 1/4, so this
       stops as soon as it is comfortably filled. */
    while (audio_aflow())
        mod_player_update();
    /* The FIFO is empty until it is primed, so that first pass always counts
       an "underrun". Zero it here so the counter means what it says: frames
       we failed to deliver during playback. */
    mod_underruns = 0;
#else
    /* How many passes this takes depends on when the HOST drains the FIFO, so
       priming is the one remaining source of host-dependence in a trace build
       -- and it shifts the sequencer, so every later chunk differs. Skipping
       it costs nothing here: the gate it primes for is compiled out. Leaving
       this in produced three confident FAIL reports that were really two runs
       of the SAME binary disagreeing -- always check the control first. */
#endif
    audio_start();
}

void mod_player_stop(void)
{
    playing = 0;
    audio_stop();
}

#ifdef CHUNK_TRACE
/*
 * Test hook. The FIFO is the one non-deterministic input to this loop: how
 * often AFLOW fires depends on when the host's audio device drains it, so two
 * captures of the same tune are never byte-identical (measured: ~0.09% of
 * frames). Ungating the loop makes it a pure function of the module, which is
 * what lets a refactor of the chunk logic be proved equivalent rather than
 * merely "sounds the same".
 *
 * chunk_hash folds in everything the audio depends on: the frame count of
 * every chunk, each voice's 8.16 phase, and each voice's read position in
 * banked RAM. Page CONTENTS need no hashing -- they are a pure function of
 * the position they were filled from.
 */
unsigned long chunk_count;
#ifdef CHUNK_HASH
unsigned long chunk_hash;
extern unsigned char mix_phase[12];
#pragma zpsym("mix_phase")
extern unsigned char c_bank[4], c_addr[8];
/* src/mod_x16.c -- the bank sample data starts at. Folded in RELATIVE to this
   so the hash describes where the mixer is reading inside the module, not
   where the module happens to sit in RAM. The playlist now sizes itself to the
   machine and moves this base; that shifts every absolute bank number without
   altering a single output sample, and an absolute hash called it a
   regression on all seven modules. */
extern unsigned char mod_first_bank;

static void chunk_fold(unsigned char b)
{
    chunk_hash ^= b;
    chunk_hash *= 16777619UL;           /* FNV-1a */
}
#endif
#endif

/* The library's requests and status (include/x16mod.h). */
volatile mod_status_t mod_status;
volatile unsigned int mod_batches;
static unsigned char song_ends;
/* A position asked for from outside the batch: 0xFF none. Written by the
   foreground, read and cleared here, so one byte and no lock. */
volatile unsigned char mod_req_pos = 0xFF;

#ifndef CHUNK_TRACE
extern unsigned char vu_peak[4];
extern void vu_scan(void);
#endif

/* The snapshot a foreground (or another program) reads; done with the batch
   it describes, so it is never half old and half new while it is read --
   the batch runs in the interrupt, and the foreground sees it whole. */
void mod_status_snap(void)
{
    unsigned char k;
    mod_status.order = (unsigned char)mod_seq.order_pos;
    mod_status.pattern = mod_seq_pattern();
    mod_status.row = (unsigned char)mod_seq.row;
    mod_status.speed = mod_seq.speed;
    mod_status.bpm = mod_seq.bpm;
    mod_status.song_ends = song_ends;
    mod_status.elapsed = mod_seconds;
    mod_status.underruns = mod_underruns;
#ifndef CHUNK_TRACE
    vu_scan();
#endif
    for (k = 0; k < MOD_CHANNELS; k++) {
#ifndef CHUNK_TRACE
        mod_status.peak[k] = vu_peak[k];
#endif
        mod_status.vol[k] = mod_chan_volume(k);
    }
    mod_status.playing = playing;
}

void mod_player_reset_counts(void)
{
    mod_seconds = 0;
    sec_acc = 0;
    mod_frames_rendered = 0;
    mod_frames_acc = 0;
    song_ends = 0;
    mod_req_pos = 0xFF;
}

void mod_player_update(void)
{
    if (!playing)
        return;
#ifndef CHUNK_TRACE
    if (!audio_aflow())
        return;                         /* FIFO still comfortable */
#endif
    mod_render_batch();
}

/*
 * One batch: AFLOW_FRAMES frames into the FIFO. The polled player calls it
 * from mod_player_update when AFLOW is set; an interrupt-driven host calls it
 * from its AFLOW dispatcher through mod_irq_batch (src/mod_irq.s).
 */
void mod_render_batch(void)
{
    unsigned char n;
    unsigned int budget;

    if (!playing)
        return;
#if defined(CHUNK_TRACE) && defined(CHUNK_LIMIT)
    /* A trace driven from the interrupt (Veranda's MODTRACE -DVIA_IRQ) stops
       where the polled harness's loop stops: after the batch that reached
       the limit. The dispatcher leaves AFLOW masked from then on. */
    if (chunk_count >= CHUNK_LIMIT) {
        mod_irq_armed = 0;
        return;
    }
#endif
    if (mod_req_pos != 0xFF) {
        mod_seq_set_position(mod_req_pos);
        mod_req_pos = 0xFF;
        tick_remaining = 0;
    }

    budget = AFLOW_FRAMES;
    while (budget) {
        if (tick_remaining == 0) {
            if (mod_seq_tick())
                song_ends++;
            mod_ticks_run++;
            tick_remaining = mod_seq_tick_len(RATE_HZ);
        }

        /* Top up any page that is running low and work out how far the
           mixer may then run -- both in one pass, in assembly. This ran per
           voice per chunk (~250 chunks a second), and cc65 spent most of it
           recomputing the v*3+2 index with a runtime multiply. */
        n = chunk_prepare();

        if (tick_remaining < n)
            n = (unsigned char)tick_remaining;
        if (budget < n)
            n = (unsigned char)budget;

        /* Underruns are sampled HERE, once per chunk (~2.4 ms of audio),
           not once on entry to this function (~64 ms). Sampling on entry
           could not see a dropout that opened and closed inside one AFLOW
           batch, so bursts registered as a single count -- the counter read
           11 on a module a WAV capture showed had 219 gaps.
           
           The number is closer to how LONG we were caught empty than to a
           count of distinct dropouts: one long gap increments it once per
           chunk it spans. Either way, zero still means zero. */
        if (audio_fifo_empty())
            mod_underruns++;

        mix_frames(n);
#ifdef CHUNK_TRACE
        chunk_count++;
#endif
#ifdef CHUNK_HASH
        {
            unsigned char k;
            chunk_fold(n);
            for (k = 0; k < 12; k++)
                chunk_fold(mix_phase[k]);
            for (k = 0; k < 4; k++)
                /* A voice that has never held a sample has c_bank 0, which is
                   below the module base -- relative-encoding that would give a
                   different answer for each base and defeat the whole point.
                   Those get a sentinel instead. */
                chunk_fold(c_bank[k] >= mod_first_bank
                           ? (unsigned char)(c_bank[k] - mod_first_bank)
                           : 0xFF);
            for (k = 0; k < 8; k++)
                chunk_fold(c_addr[k]);
        }
#endif
        budget -= n;
        mod_frames_acc += n;
        while (mod_frames_acc >= 16) {
            mod_frames_acc -= 16;
            mod_frames_rendered++;
            if (++sec_acc >= 1001) {        /* 16022 Hz / 16 frames */
                sec_acc = 0;
                mod_seconds++;
            }
        }
        tick_remaining -= n;
    }
    mod_batches++;
#ifndef CHUNK_TRACE
    mod_status_snap();
#endif
}
