/*
 * mod_lib.c -- the library's transport: play, stop, pause, position, volume,
 * and a pattern row for a viewer (include/x16mod.h).
 */
#include "x16mod.h"
#include "mod_play.h"
#include "mod_player.h"

#define RAM_BANK_REG (*(volatile unsigned char *)0x0000)

/* src/mixer_api.s, src/vera_audio.s, src/mod_player.c, src/mod_irq.s */
extern void cache_init(void);
extern void audio_start(void);
extern void audio_stop(void);
extern void audio_set_volume(unsigned char v);
extern void mod_hooks_init(const mod_info_t *m);
extern void mod_player_reset_counts(void);
extern void mod_status_snap(void);
extern volatile unsigned char mod_req_pos;
extern void mod_aflow_on(void);
extern void mod_aflow_off(void);

void mod_init(void)
{
    cache_init();
    mod_req_pos = 0xFF;
}

void mod_stop(void)
{
    mod_irq_armed = 0;
    mod_aflow_off();
    mod_player_stop();
    mod_status.playing = 0;
}

void mod_play(const mod_info_t *m, unsigned char order)
{
    mod_stop();
    mod_hooks_init(m);
    mod_seq_init(m);
    if (order)
        mod_seq_set_position(order);
    mod_player_reset_counts();
    mod_player_start();         /* primes the FIFO (AFLOW is masked), starts */
    mod_status_snap();
    if (mod_use_irq) {
        mod_irq_armed = 1;
        mod_aflow_on();
    }
}

void mod_pause(unsigned char on)
{
    if (on)
        audio_stop();
    else
        audio_start();
}

void mod_set_master_volume(unsigned char v)
{
    audio_set_volume(v);
}

void mod_request_position(unsigned char order)
{
    mod_req_pos = order;
}

void mod_update(void)
{
    mod_player_update();
}

void mod_read_row(const mod_info_t *m, uint8_t pattern, uint8_t row,
                  uint8_t *out16)
{
    unsigned char save = RAM_BANK_REG, k;
    const unsigned char *p;
    RAM_BANK_REG = (unsigned char)(m->pattern_bank + (pattern >> 3));
    p = (const unsigned char *)(0xA000u
                              + ((unsigned int)(pattern & 7) << 10)
                              + ((unsigned int)row << 4));
    for (k = 0; k < 16; k++)
        out16[k] = p[k];
    RAM_BANK_REG = save;
}
