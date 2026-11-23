/*
 * vr_file.c -- a module from a Veranda file into banks from BankAlloc.
 */
#include <veranda.h>
#include "x16mod.h"
#include "vr_mod.h"

static unsigned char fh;

/* FRead takes a buffer at $A000 in the caller's RAM bank (Veranda V47):
   the player lends $A000 of a bank of its own for the 1,084-byte header. */
static unsigned int vr_read(void *buf, unsigned int n)
{
    return FRead(fh, buf, n);
}

unsigned char vr_mod_bank, vr_mod_nbanks;

void vr_mod_free(void)
{
    if (vr_mod_bank) {
        BankFree(vr_mod_bank);
        vr_mod_bank = 0;
    }
}

mod_err_t vr_mod_load(const char *name, mod_info_t *m, unsigned char *hdr)
{
    mod_err_t e;
    unsigned char n;

    vr_mod_free();
    fh = FOpen((void *)name, 'r');
    if (fh < 2)
        return MOD_ERR_OPEN;
    e = mod_open(vr_read, m, hdr);
    if (e == MOD_OK) {
        n = mod_banks_needed(m);
        vr_mod_bank = BankAlloc(n);
        if (!vr_mod_bank)
            e = MOD_ERR_NOMEM;
        else {
            vr_mod_nbanks = n;
            e = mod_load(vr_read, m, vr_mod_bank, n);
        }
    }
    FClose(fh);
    if (e != MOD_OK)
        vr_mod_free();
    return e;
}

/* An overlay record's RAM bank (overlays = banked: run or read in place
   there); 0 if there is none. LoadOverlay returns the error; the bank is in
   r1L and r0 is $A000. */
unsigned char vr_overlay_bank(unsigned char n)
{
    return LoadOverlay(n, 0) == 0 && r0 == 0xA000 ? r1L : 0;
}

unsigned char vr_mod_tables(void)
{
    mod_tab_bank = vr_overlay_bank(1);
    return mod_tab_bank;
}
