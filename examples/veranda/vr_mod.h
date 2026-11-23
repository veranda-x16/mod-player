/* The MOD player's Veranda glue (examples/veranda). */
#ifndef VR_MOD_H
#define VR_MOD_H
#include "x16mod.h"

/* vr_file.c: open name, BankAlloc the banks it needs, load; the banks
   stay until vr_mod_free or the next load. */
mod_err_t vr_mod_load(const char *name, mod_info_t *m, unsigned char *hdr);
void vr_mod_free(void);
extern unsigned char vr_mod_bank, vr_mod_nbanks;

/* vr_file.c: the engine's step table, overlay record 1 (cfg/veranda-mod.cfg),
   from its RAM bank (mod_tab_bank); 0 if there is none */
unsigned char vr_mod_tables(void);
unsigned char vr_overlay_bank(unsigned char n);
extern unsigned char mod_tab_bank;

/* vr_audio.s: the AFLOW routine in intFirstVector (DECISIONS V52) */
void vr_audio_on(void);
void vr_audio_off(void);

#endif
