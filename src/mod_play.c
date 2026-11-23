/*
 * mod_play.c -- ProTracker sequencer.
 *
 * Portable C: the identical source compiles natively for the trace oracle and
 * with cc65 for the X16, so a disagreement between the two is a porting bug
 * rather than a question of interpretation.
 *
 * Phase 6 implements position/row progression, speed and tempo, note
 * triggering, and effects Bxx, Cxx, Dxx, Fxx. Other effects are decoded and
 * stored but do nothing yet; they arrive one at a time in Phase 7, each with
 * its own trace test.
 */
#include "mod_play.h"

mod_seq_t mod_seq;

static const mod_info_t *info;

/*
 * ProTracker finetune is 1/8 of a semitone per step, so the period multiplier
 * is 2^(-ft/96). Scaled by 4096 and indexed by (nibble & 7) - (nibble & 8) + 8,
 * which puts the neutral value at index 8.
 */
static const uint16_t finetune_mul[16] = {
    4340, 4308, 4277, 4247, 4216, 4186, 4156, 4126,
    4096, 4067, 4037, 4008, 3979, 3951, 3922, 3894
};


/*
 * Vibrato/tremolo sine: floor(255 * sin(pi*i/32)) over a half period; the
 * phase bit 0x20 flips the sign. Saw, square and random are the other three
 * ProTracker waveforms.
 */
static const uint8_t sine_table[32] = {
      0,  24,  49,  74,  97, 120, 141, 161, 180, 197, 212, 224, 235, 244, 250, 253,
    255, 253, 250, 244, 235, 224, 212, 197, 180, 161, 141, 120,  97,  74,  49,  24
};

/* Arpeggio: semitone ratios, 4096 * 2^(-n/12). */
static const uint16_t arp_tuning[16] = {
    4096, 3866, 3649, 3444, 3251, 3069, 2896, 2734,
    2580, 2435, 2299, 2170, 2048, 1933, 1825, 1722
};

static int16_t waveform(uint8_t phase, uint8_t type)
{
    int16_t amp;
    switch (type & 3) {
    case 0:                                     /* sine */
        amp = (int16_t)sine_table[phase & 0x1F];
        if (phase & 0x20)
            amp = (int16_t)-amp;
        return amp;
    case 1:                                     /* saw down */
        return (int16_t)(255 - (((phase + 0x20) & 0x3F) << 3));
    case 2:                                     /* square */
        return (int16_t)(255 - ((phase & 0x20) << 4));
    default:                                    /* random */
        amp = (int16_t)((mod_seq.random_seed >> 20) - 255);
        mod_seq.random_seed = (mod_seq.random_seed * 65 + 17) & 0x1FFFFFFFUL;
        return amp;
    }
}

static void do_vibrato(mod_chan_t *ch)
{
    ch->vibrato_add = (int8_t)((waveform(ch->vibrato_phase, ch->vibrato_type)
                                * ch->vibrato_depth) >> 7);
}

static void do_tremolo(mod_chan_t *ch)
{
    ch->tremolo_add = (int8_t)((waveform(ch->tremolo_phase, ch->tremolo_type)
                                * ch->tremolo_depth) >> 6);
}

/* 3xx: slide the current period toward the last note's target. */
static void tone_portamento(mod_chan_t *ch)
{
    if (ch->period < ch->porta_period) {
        uint16_t p = (uint16_t)(ch->period + ch->porta_speed);
        ch->period = (p > ch->porta_period) ? ch->porta_period : p;
    } else if (ch->period > ch->porta_period) {
        int32_t p = (int32_t)ch->period - ch->porta_speed;
        ch->period = (p < ch->porta_period) ? ch->porta_period : (uint16_t)p;
    }
}

/* Axy and friends: upper nibble slides up, lower nibble slides down. */
static void volume_slide(mod_chan_t *ch, uint8_t param)
{
    int16_t v = (int16_t)ch->volume + (param >> 4) - (param & 0x0F);
    if (v < 0) v = 0;
    if (v > 64) v = 64;
    ch->volume = (uint8_t)v;
}

/* Effective period: base, plus vibrato, scaled by the arpeggio semitone. */
uint16_t mod_chan_period(uint8_t c)
{
    const mod_chan_t *ch = &mod_seq.chan[c];
    int32_t period;

    /*
     * arp_tuning[0] is 4096, so with no arpeggio the multiply and both shifts
     * below collapse to the identity: ((p * 4096) >> 11 >> 1) == p. Almost
     * every note is in that case, and cc65 turns the 32-bit multiply into a
     * slow runtime call -- worth about 50 cycles per output frame to skip.
     *
     * The guard keeps the 16-bit path to periods where the addition cannot
     * overflow; anything larger falls through to the original arithmetic.
     */
    if (ch->arpeggio_add == 0 && ch->period < 0x7F00) {
        int16_t p = (int16_t)((int16_t)ch->period + ch->vibrato_add);
        return (uint16_t)(p < 14 ? 6848 : p);
    }

    period = (int32_t)ch->period + ch->vibrato_add;
    period = (period * arp_tuning[ch->arpeggio_add & 0x0F]) >> 11;
    period = (period >> 1) + (period & 1);
    if (period < 14)
        period = 6848;
    return (uint16_t)period;
}

/* Effective volume: base plus tremolo, clamped. */
uint8_t mod_chan_volume(uint8_t c)
{
    const mod_chan_t *ch = &mod_seq.chan[c];
    int16_t v = (int16_t)ch->volume + ch->tremolo_add;
    if (v < 0) v = 0;
    if (v > 64) v = 64;
    return (uint8_t)v;
}

uint8_t mod_seq_pattern(void)
{
    return (uint8_t)(info->order[mod_seq.order_pos] & 0x7F);
}

/*
 * Samples per tick = rate * 2.5 / bpm.
 *
 * Cached: this is asked for on every tick, and cc65 renders the 32-bit
 * multiply and divide as two runtime calls -- about 4,700 cycles a tick for a
 * result that only changes when Fxx sets a new tempo.
 */
static uint16_t tl_cached;
static uint8_t  tl_bpm;
static uint16_t tl_rate;

uint16_t mod_seq_tick_len(uint16_t output_rate)
{
    if (mod_seq.bpm != tl_bpm || output_rate != tl_rate) {
        tl_bpm = mod_seq.bpm;
        tl_rate = output_rate;
        tl_cached = (uint16_t)(((uint32_t)output_rate * 5UL)
                             / ((uint32_t)tl_bpm * 2UL));
    }
    return tl_cached;
}

void mod_seq_init(const mod_info_t *m)
{
    uint8_t c;
    info = m;
    mod_seq.order_pos = 0;
    mod_seq.row = 0;
    mod_seq.speed = 6;
    mod_seq.bpm = 125;
    mod_seq.song_end = 0;
    for (c = 0; c < MOD_CHANNELS; c++) {
        mod_chan_t *ch = &mod_seq.chan[c];
        ch->period = 0;
        ch->porta_period = 0;
        ch->instrument = 0;
        ch->assigned = 0;
        ch->volume = 0;
        ch->finetune = 8;
        ch->effect = 0;
        ch->param = 0;
        ch->note_key = 0;
        ch->note_ins = 0;
        ch->fx_count = 0;
        ch->sample_offset = 0;
        ch->porta_speed = 0;
        ch->vibrato_type = ch->vibrato_phase = 0;
        ch->vibrato_speed = ch->vibrato_depth = 0;
        ch->tremolo_type = ch->tremolo_phase = 0;
        ch->tremolo_speed = ch->tremolo_depth = 0;
        ch->vibrato_add = ch->tremolo_add = ch->arpeggio_add = 0;
        ch->pl_row = 0;
        ch->trig = 0;
    }
    mod_seq_set_position(0);
}

/*
 * Seeds break_order rather than assigning the position directly, so that the
 * first sequence_row() performs the jump through the normal path. tick = 1
 * means the very next tick starts a row. This mirrors ProTracker (and the
 * micromod oracle) exactly, including the quirk that starting at position 0
 * reports song_end on the first row because the jump target is not greater
 * than the current position.
 */
void mod_seq_set_position(uint8_t pos)
{
    uint8_t c;
    if (pos >= info->song_length)
        pos = 0;
    mod_seq.break_order = (int16_t)pos;
    mod_seq.next_row = 0;
    mod_seq.tick = 1;
    mod_seq.speed = 6;
    mod_seq.bpm = 125;
    mod_seq.song_end = 0;
    mod_seq.pl_count = -1;
    mod_seq.pl_channel = -1;
    mod_seq.random_seed = 0xABCDEFUL;
    for (c = 0; c < MOD_CHANNELS; c++) {
        mod_seq.chan[c].instrument = 0;
        mod_seq.chan[c].assigned = 0;
        mod_seq.chan[c].volume = 0;
        mod_seq.chan[c].pl_row = 0;
    }
    mod_seq_tick();               /* land on the first row, as micromod does */

    /*
     * That tick sets song_end: seeding the jump target makes the "did we jump
     * backwards?" test true when starting at position 0, since the target is
     * not greater than the current position. It is an artefact of how the
     * position is seeded, not a real end of song -- and a player that trusts
     * it advances to the next track the instant a module loads.
     */
    mod_seq.song_end = 0;
}

/*
 * Start (or retune) a note on one channel.
 *
 * "ins" is the instrument number written on THIS row, not the channel's
 * running assignment. That distinction is the whole point: an instrument
 * number reloads the sample's default volume and finetune, while a bare note
 * (or an empty row) must leave them alone. Testing the persistent assignment
 * instead makes every empty row silently undo the preceding Cxx.
 */
static void trigger(mod_chan_t *ch)
{
    uint16_t key = ch->note_key;
    uint8_t  ins = ch->note_ins;
    uint32_t period;

    if (ins > 0 && ins <= MOD_MAX_SAMPLES) {
        const mod_sample_t *s = &info->samples[ins - 1];
        ch->assigned = ins;
        ch->sample_offset = 0;
        ch->finetune = (uint8_t)(((s->finetune & 7) - (s->finetune & 8)) + 8);
        ch->volume = s->volume;
        /* An instrument number with no note swaps the sounding sample right
           away -- but only when the incoming sample loops and the channel is
           already playing something. Trackers rely on this to change timbre
           mid-note; without it the channel keeps the old sample until the next
           actual note. */
        if (s->loop_len > 0 && ch->instrument > 0)
            ch->instrument = ins;
    }

    if (ch->effect == 0x09)                       /* 9xx sample offset */
        ch->sample_offset = (uint16_t)(ch->param) << 8;
    else if (ch->effect == 0x15)                  /* E5x set finetune */
        ch->finetune = ch->param;

    if (key > 0) {
        /* Apply finetune, then halve with rounding: the table is scaled by
           4096 but shifted by 11, so the result is twice the period. */
        /* finetune_mul[8] is 4096, so neutral finetune reduces to the
           identity ((key*4096)>>11)>>1 == key. That is the common case, and
           it avoids a 32-bit multiply on every note-on. */
        if ((ch->finetune & 0x0F) == 8) {
            ch->porta_period = key;
        } else {
            period = ((uint32_t)key * finetune_mul[ch->finetune & 0x0F]) >> 11;
            ch->porta_period = (uint16_t)((period >> 1) + (period & 1));
        }

        /* Tone portamento (3xx) and its volume-slide pairing (5xy) retune the
           target without restarting the sample. */
        if (ch->effect != 0x03 && ch->effect != 0x05) {
            ch->instrument = ch->assigned;
            ch->period = ch->porta_period;
            ch->trig = 1;
            /* Waveform types 0-3 restart from phase 0 on a new note; types
               4-7 are the "continue" variants that carry the phase across. */
            if (ch->vibrato_type < 4) ch->vibrato_phase = 0;
            if (ch->tremolo_type < 4) ch->tremolo_phase = 0;
        }
    }
}

/* Process one channel at the start of a row. */
static void channel_row(uint8_t c)
{
    mod_chan_t *ch = &mod_seq.chan[c];
    uint8_t note[4];
    uint16_t key;
    uint8_t ins, effect, param;

    mod_read_note(mod_seq_pattern(), (uint8_t)mod_seq.row, c, note);

    key = (uint16_t)((note[0] & 0x0F) << 8) | note[1];
    ins = (uint8_t)((note[2] >> 4) | (note[0] & 0x10));
    effect = (uint8_t)(note[2] & 0x0F);
    param = note[3];

    /* Give the E-subcommands their own range, then reuse the vacated 0x0E
       for arpeggio (file effect 0 with a parameter). */
    if (effect == 0x0E) {
        effect = (uint8_t)(0x10 | (param >> 4));
        param &= 0x0F;
    } else if (effect == 0x00 && param > 0) {
        effect = 0x0E;
    }

    ch->effect = effect;
    ch->param = param;
    ch->note_key = key;
    ch->note_ins = ins;
    ch->fx_count = 0;
    ch->trig = 0;
    ch->vibrato_add = ch->tremolo_add = ch->arpeggio_add = 0;

    /* EDx holds the trigger until tick x, so the note is kept on the channel
       rather than passed in. */
    if (!(effect == 0x1D && param > 0))
        trigger(ch);

    switch (effect) {
    case 0x03:                                  /* 3xx tone portamento */
        if (param > 0)
            ch->porta_speed = param;
        break;
    case 0x04:                                  /* 4xy vibrato */
        if (param & 0xF0) ch->vibrato_speed = (uint8_t)(param >> 4);
        if (param & 0x0F) ch->vibrato_depth = (uint8_t)(param & 0x0F);
        do_vibrato(ch);
        break;
    case 0x06:                                  /* 6xy vibrato + vol slide */
        do_vibrato(ch);
        break;
    case 0x07:                                  /* 7xy tremolo */
        if (param & 0xF0) ch->tremolo_speed = (uint8_t)(param >> 4);
        if (param & 0x0F) ch->tremolo_depth = (uint8_t)(param & 0x0F);
        do_tremolo(ch);
        break;
    case 0x08:                                  /* 8xx panning */
        /* Amiga panning is fixed (L=1+4, R=2+3); 8xx is a later extension
           and has no meaning for a 4-channel ProTracker module. */
        break;
    case 0x0B:                                  /* Bxx position jump */
        /* Ignored inside an E6x loop, or the loop could never finish. */
        if (mod_seq.pl_count < 0) {
            mod_seq.break_order = (int16_t)param;
            mod_seq.next_row = 0;
        }
        break;
    case 0x0C:                                  /* Cxx set volume */
        ch->volume = param > 64 ? 64 : param;
        break;
    case 0x0D:                                  /* Dxx pattern break */
        if (mod_seq.pl_count < 0) {
            if (mod_seq.break_order < 0)
                mod_seq.break_order = (int16_t)(mod_seq.order_pos + 1);
            /* The parameter is DECIMAL, not hex -- a ProTracker quirk. */
            mod_seq.next_row = (int16_t)((param >> 4) * 10 + (param & 0x0F));
            if (mod_seq.next_row >= MOD_ROWS)
                mod_seq.next_row = 0;
        }
        break;
    case 0x0F:                                  /* Fxx speed / tempo */
        if (param > 0) {
            if (param < 32)
                mod_seq.tick = mod_seq.speed = param;
            else
                mod_seq.bpm = param;
        }
        break;
    case 0x11:                                  /* E1x fine portamento up */
        ch->period = (ch->period > param) ? (uint16_t)(ch->period - param) : 0;
        break;
    case 0x12:                                  /* E2x fine portamento down */
        ch->period = (uint16_t)(ch->period + param);
        break;
    case 0x14:                                  /* E4x vibrato waveform */
        if (param < 8) ch->vibrato_type = param;
        break;
    case 0x16:                                  /* E6x pattern loop */
        if (param == 0) {
            ch->pl_row = (uint8_t)mod_seq.row;  /* set the marker here */
        }
        if (ch->pl_row < mod_seq.row && mod_seq.break_order < 0) {
            if (mod_seq.pl_count < 0) {         /* start looping */
                mod_seq.pl_count = (int8_t)param;
                mod_seq.pl_channel = (int8_t)c;
            }
            if (mod_seq.pl_channel == (int8_t)c) {
                if (mod_seq.pl_count == 0)      /* done: retire the marker */
                    ch->pl_row = (uint8_t)(mod_seq.row + 1);
                else
                    mod_seq.next_row = (int16_t)ch->pl_row;
                mod_seq.pl_count--;
            }
        }
        break;
    case 0x17:                                  /* E7x tremolo waveform */
        if (param < 8) ch->tremolo_type = param;
        break;
    case 0x1A:                                  /* EAx fine volume up */
        ch->volume = (ch->volume + param > 64) ? 64 : (uint8_t)(ch->volume + param);
        break;
    case 0x1B:                                  /* EBx fine volume down */
        ch->volume = (ch->volume > param) ? (uint8_t)(ch->volume - param) : 0;
        break;
    case 0x1C:                                  /* ECx note cut */
        if (param == 0)
            ch->volume = 0;
        break;
    case 0x1E:                                  /* EEx pattern delay */
        mod_seq.tick = (int16_t)(mod_seq.speed + mod_seq.speed * param);
        break;
    default:
        break;
    }
}

/* Effect continuation on every tick of a row after the first. */
static void channel_tick(uint8_t c)
{
    mod_chan_t *ch = &mod_seq.chan[c];
    uint8_t effect = ch->effect;
    uint8_t param = ch->param;

    ch->trig = 0;
    ch->fx_count++;

    switch (effect) {
    case 0x01:                                  /* 1xx portamento up */
        ch->period = (ch->period > param) ? (uint16_t)(ch->period - param) : 0;
        break;
    case 0x02:                                  /* 2xx portamento down */
        ch->period = (uint16_t)(ch->period + param);
        break;
    case 0x03:                                  /* 3xx tone portamento */
        tone_portamento(ch);
        break;
    case 0x04:                                  /* 4xy vibrato */
        ch->vibrato_phase = (uint8_t)(ch->vibrato_phase + ch->vibrato_speed);
        do_vibrato(ch);
        break;
    case 0x05:                                  /* 5xy tone porta + vol slide */
        tone_portamento(ch);
        volume_slide(ch, param);
        break;
    case 0x06:                                  /* 6xy vibrato + vol slide */
        ch->vibrato_phase = (uint8_t)(ch->vibrato_phase + ch->vibrato_speed);
        do_vibrato(ch);
        volume_slide(ch, param);
        break;
    case 0x07:                                  /* 7xy tremolo */
        ch->tremolo_phase = (uint8_t)(ch->tremolo_phase + ch->tremolo_speed);
        do_tremolo(ch);
        break;
    case 0x0A:                                  /* Axy volume slide */
        volume_slide(ch, param);
        break;
    case 0x0E:                                  /* 0xy arpeggio */
        /* Cycles base / +x / +y semitones once per tick. */
        if (ch->fx_count > 2)
            ch->fx_count = 0;
        if (ch->fx_count == 0)      ch->arpeggio_add = 0;
        else if (ch->fx_count == 1) ch->arpeggio_add = (int8_t)(param >> 4);
        else                        ch->arpeggio_add = (int8_t)(param & 0x0F);
        break;
    case 0x19:                                  /* E9x retrigger */
        if (ch->fx_count >= param) {
            ch->fx_count = 0;
            ch->trig = 1;
        }
        break;
    case 0x1C:                                  /* ECx note cut */
        if (param == ch->fx_count)
            ch->volume = 0;
        break;
    case 0x1D:                                  /* EDx note delay */
        if (param == ch->fx_count)
            trigger(ch);
        break;
    default:
        break;
    }
}

static void sequence_row(void)
{
    uint8_t c;

    if (mod_seq.next_row < 0) {                 /* pattern ran off the end */
        mod_seq.break_order = (int16_t)(mod_seq.order_pos + 1);
        mod_seq.next_row = 0;
    }
    if (mod_seq.break_order >= 0) {
        if (mod_seq.break_order >= (int16_t)info->song_length) {
            mod_seq.break_order = 0;
            mod_seq.next_row = 0;
        }
        if (mod_seq.break_order <= mod_seq.order_pos)
            mod_seq.song_end = 1;
        mod_seq.order_pos = mod_seq.break_order;
        for (c = 0; c < MOD_CHANNELS; c++)
            mod_seq.chan[c].pl_row = 0;
        mod_seq.break_order = -1;
    }

    mod_seq.row = mod_seq.next_row;
    mod_seq.next_row = (int16_t)(mod_seq.row + 1);
    if (mod_seq.next_row >= MOD_ROWS)
        mod_seq.next_row = -1;

    for (c = 0; c < MOD_CHANNELS; c++)
        channel_row(c);
}

uint8_t mod_seq_tick(void)
{
    uint8_t c;

    mod_seq.song_end = 0;
    if (--mod_seq.tick <= 0) {
        mod_seq.tick = mod_seq.speed;
        sequence_row();
    } else {
        for (c = 0; c < MOD_CHANNELS; c++)
            channel_tick(c);
    }
    for (c = 0; c < MOD_CHANNELS; c++)
        mod_apply(c);
    return mod_seq.song_end;
}
