/*
 * mod_player.h -- real-time playback.
 *
 * mod_player_update() must be called often enough that the FIFO never runs
 * dry. It returns as soon as the FIFO is full, so calling it in a tight loop
 * alongside UI work is the intended usage.
 */
#ifndef MOD_PLAYER_H
#define MOD_PLAYER_H

void mod_player_start(void);
void mod_player_update(void);
void mod_player_stop(void);

#endif
