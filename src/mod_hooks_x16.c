/*
 * mod_hooks_x16.c -- the X16 side of the sequencer's platform hooks.
 *
 * mod_play.c is target-agnostic; these two functions are everything it needs
 * from the machine. mod_read_note pulls pattern bytes out of banked RAM, and
 * mod_apply pushes channel state across the boundary in src/mixer_api.s.
 * Neither the sequencer nor this file ever touches VERA.
 */
#include "mod_play.h"
#include "x16mod.h"

#define RAM_BANK_REG (*(volatile unsigned char *)0x0000)
#define WIN_BASE 0xA000
#define WIN_SIZE 0x2000

/* src/mixer_api.s */
extern void mixer_set_period(void);
extern void mixer_set_volume_ch(void);
extern void mixer_set_sample(void);
extern void mixer_trigger(void);
extern void mixer_stop(void);
extern unsigned char mx_ch, mx_vol, mx_bank, mx_ebank, mx_lbank, mx_loop;
extern unsigned int  mx_period, mx_addr, mx_eaddr, mx_laddr;

extern void mod_advance_pos(unsigned char *bank, unsigned int *addr,
                            unsigned long n);

static const mod_info_t *pinfo;

extern void mix_clear(void);

/* Before mod_seq_init, whose first tick already triggers notes. */
void mod_hooks_init(const mod_info_t *m)
{
    pinfo = m;
    mix_clear();                /* a cold start, whatever zero page held */
}

void mod_read_note(uint8_t pattern, uint8_t row, uint8_t chan, uint8_t *out4)
{
    unsigned int addr;
    const unsigned char *p;

    /*
     * A pattern is 1024 bytes and a bank is 8192, so exactly eight patterns
     * fit per bank and the address is pure 16-bit arithmetic:
     *
     *     bank = pattern_bank + pattern/8
     *     addr = $A000 + (pattern%8)*1024 + row*16 + chan*4
     *
     * The previous form built a byte offset that overflowed 16 bits (up to
     * 131,068) and so pulled in cc65's 32-bit multiply plus a call to
     * mod_advance_pos, on a function called four times per row.
     */
    RAM_BANK_REG = (unsigned char)(pinfo->pattern_bank + (pattern >> 3));
    addr = (unsigned int)(WIN_BASE
                        + ((unsigned int)(pattern & 7) << 10)
                        + ((unsigned int)row << 4)
                        + ((unsigned int)chan << 2));
    p = (const unsigned char *)addr;
    /* A note is 4 bytes and patterns are 16-byte aligned per row, so a note
       never straddles the 8 KB window boundary. */
    out4[0] = p[0];
    out4[1] = p[1];
    out4[2] = p[2];
    out4[3] = p[3];
}

void mod_apply(uint8_t ch)
{
    const mod_chan_t *c = &mod_seq.chan[ch];
    const mod_sample_t *s;
    unsigned char bank;
    unsigned int addr;

    mx_ch = ch;

    if (c->trig && c->instrument > 0 && c->instrument <= MOD_MAX_SAMPLES) {
        s = &pinfo->samples[c->instrument - 1];
        if (s->length == 0) {
            mixer_stop();
            return;
        }

        /* Start position, honouring a 9xx sample offset. */
        bank = s->bank;
        addr = s->addr;
        mod_advance_pos(&bank, &addr,
                        c->sample_offset < s->length ? c->sample_offset : 0);
        mx_bank = bank;
        mx_addr = addr;

        if (s->loop_len > 0) {
            /* Looping: end at loop_start+loop_len, wrap back to loop_start. */
            bank = s->bank; addr = s->addr;
            mod_advance_pos(&bank, &addr, s->loop_start + s->loop_len);
            mx_ebank = bank; mx_eaddr = addr;
            bank = s->bank; addr = s->addr;
            mod_advance_pos(&bank, &addr, s->loop_start);
            mx_lbank = bank; mx_laddr = addr;
            mx_loop = 1;
        } else {
            bank = s->bank; addr = s->addr;
            mod_advance_pos(&bank, &addr, s->length);
            mx_ebank = bank; mx_eaddr = addr;
            mx_lbank = s->bank; mx_laddr = s->addr;
            mx_loop = 0;
        }
        mixer_set_sample();
    }

    /* Effective values: base period bent by vibrato and arpeggio, base volume
       offset by tremolo. */
    if (c->period > 0) {
        mx_period = mod_chan_period(ch);
        mixer_set_period();
    }
    mx_vol = mod_chan_volume(ch);
    mixer_set_volume_ch();

    if (c->trig)
        mixer_trigger();
}
