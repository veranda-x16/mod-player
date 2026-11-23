/*
 * mod_play.h -- the ProTracker sequencer.
 *
 * Ticks at ~50 Hz and writes sample/period/volume/trigger into four channel
 * states. It never touches VERA, the bank register, or a phase accumulator --
 * that is the boundary src/mixer_api.s defines.
 *
 * Effects use an internal encoding that gives the E-subcommands their own
 * space: an Exy in the file becomes effect 0x10|x with param y, which frees
 * slot 0x0E, and arpeggio (file effect 0 with a non-zero parameter) is moved
 * there. That makes every effect a single switch case.
 */
#ifndef MOD_PLAY_H
#define MOD_PLAY_H

#include <stdint.h>
#include "x16mod.h"

typedef struct {
    uint16_t period;        /* current period, after effects */
    uint16_t porta_period;  /* target period of the last note */
    uint8_t  instrument;    /* sample now sounding, 1..31 (0 = none) */
    uint8_t  assigned;      /* last sample assigned by a note */
    uint8_t  volume;        /* 0..64 */
    uint8_t  finetune;      /* index into the finetune table, 8 = neutral */
    uint8_t  effect;
    uint8_t  param;
    uint16_t note_key;      /* period written on this row, 0 = none */
    uint8_t  note_ins;      /* instrument written on this row, 0 = none */
    uint8_t  fx_count;
    uint16_t sample_offset; /* bytes; effect 9xx sets param<<8 */
    uint8_t  porta_speed;   /* 3xx tone portamento rate */
    uint8_t  vibrato_type, vibrato_phase, vibrato_speed, vibrato_depth;
    uint8_t  tremolo_type, tremolo_phase, tremolo_speed, tremolo_depth;
    int8_t   vibrato_add;   /* period offset, 4xy */
    int8_t   tremolo_add;   /* volume offset, 7xy */
    int8_t   arpeggio_add;  /* semitones up, 0xy */
    uint8_t  pl_row;        /* E6x pattern-loop marker */
    uint8_t  trig;          /* 1 for the tick on which the note retriggered */
} mod_chan_t;

typedef struct {
    int16_t  order_pos;     /* index into the order table */
    int16_t  break_order;   /* pending position jump, -1 = none */
    int16_t  row;
    int16_t  next_row;      /* -1 = pattern finished */
    int16_t  tick;          /* counts DOWN to 0, then a new row */
    uint8_t  speed;         /* ticks per row */
    uint8_t  bpm;
    uint8_t  song_end;
    int8_t   pl_count;      /* E6x loops remaining, -1 = not looping */
    int8_t   pl_channel;    /* which channel owns the loop */
    uint32_t random_seed;   /* for the random vibrato/tremolo waveform */
    mod_chan_t chan[MOD_CHANNELS];
} mod_seq_t;

extern mod_seq_t mod_seq;

void    mod_seq_init(const mod_info_t *m);
void    mod_seq_set_position(uint8_t pos);
uint8_t mod_seq_tick(void);            /* returns 1 when the song wraps */
uint8_t mod_seq_pattern(void);         /* pattern number at the current position */

/* Samples per tick at the current tempo, for the chosen output rate. */
uint16_t mod_seq_tick_len(uint16_t output_rate);

/* Effective values after vibrato / arpeggio / tremolo. These are what the
   mixer is given; the raw period and volume fields are the base the effects
   modulate. */
uint16_t mod_chan_period(uint8_t ch);
uint8_t  mod_chan_volume(uint8_t ch);

/* Platform hooks. */
void mod_read_note(uint8_t pattern, uint8_t row, uint8_t chan, uint8_t *out4);
void mod_apply(uint8_t ch);            /* push channel state to the mixer */

#endif
