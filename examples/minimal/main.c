/* The library alone: load music.mod into banks 1.., play it, polled. */
#include "x16mod.h"
extern unsigned char mod_kopen(const char *name);      /* src/mod_x16.c */
extern unsigned int  mod_kread(void *buf, unsigned int n);
extern void          mod_kclose(void);
static mod_info_t m;
static unsigned char hdr[MOD_HEADER_SIZE];
int main(void)
{
    unsigned int b = 0;
    if (mod_kopen("music.mod") || mod_open(mod_kread, &m, hdr)
        || mod_load(mod_kread, &m, 1, mod_banks_needed(&m)))
        return 1;
    mod_kclose();
    mod_init();
    mod_play(&m, 0);
    for (;;) {
        mod_update();
        if (mod_batches != b) {             /* a dot a batch, for the gate */
            b = mod_batches;
            *(volatile char *)0x9FBB = '.';
        }
    }
}
