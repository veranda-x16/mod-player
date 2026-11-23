/*
 * modtrace.c -- MODTRACE.cvt: the library under Veranda, deterministic
 * (make veranda-trace; tools/gate_veranda.sh).
 *
 * The CHUNK_TRACE/CHUNK_HASH library (build/lib-trace) linked with
 * cfg/veranda-mod.cfg: zero page at $A9, the module from FOpen/FRead into
 * banks from BankAlloc. It renders CHUNK_LIMIT chunks and logs
 * "MODTRACE done <chunks> <hash>", which must equal x16-mod's
 * tests/golden/chunk_hashes.txt (the hash folds banks relative to the
 * module's first, so Veranda's bank numbers do not matter).
 *
 *   -DVIA_IRQ        the batches come from VERA's AFLOW interrupt through
 *                    intFirstVector (vr_audio.s, mod_irq_batch), the main
 *                    program only waits
 * It first logs "MODTRACE sum <s> of <n>": the bytes loaded into the banks,
 * summed, against the file's after its header (the gate computes it).
 *
 *   -DTRACE_TICKS=n  instead: the sequencer's first n ticks, in host_seq's
 *                    format, one "T " line each
 */
#include <veranda.h>
#include "x16mod.h"
#include "mod_play.h"
#include "mod_player.h"
#include "vr_mod.h"

extern void mod_hooks_init(const mod_info_t *m);
extern unsigned long chunk_count;
extern unsigned long chunk_hash;
extern void mod_aflow_on(void);

static unsigned char hdr[MOD_HEADER_SIZE];
static mod_info_t mod;
static char line[160];
static char *lp;

/* No stdio: its EOF clashes with GEOS's, and printf is 2 KB. */
static void put(const char *t) { while (*t) *lp++ = *t++; }
static void dec(unsigned long v)
{ char b[11]; unsigned char n = 0; do { b[n++] = (char)('0' + v % 10); v /= 10; } while (v); while (n) *lp++ = b[--n]; }
static void sdec(int v) { if (v < 0) { *lp++ = '-'; v = -v; } dec((unsigned)v); }
static void hex(unsigned long v)
{ signed char i; for (i = 28; i >= 0; i -= 4) { unsigned char d = (unsigned char)((v >> i) & 15); *lp++ = (char)(d < 10 ? '0' + d : 'a' + d - 10); } }
static void sp(void) { *lp++ = ' '; }
static void say(void) { *lp = 0; LogText(line); }

int main(void)
{
    mod_err_t e;
    e = vr_mod_load("music.mod", &mod, hdr);
    if (e != MOD_OK) {
        lp = line; put("MODTRACE error "); dec(e); say();
        return 0;
    }
    lp = line; put("MODTRACE loaded banks "); dec(vr_mod_bank); put("+"); dec(vr_mod_nbanks); say();
#ifndef TRACE_TICKS
    {   /* the loaded bytes, as a sum, against the file's after its header */
        unsigned long n = mod_load_total, sum = 0;
        unsigned char b = vr_mod_bank, save = *(unsigned char *)0;
        unsigned int a = 0xA000;
        while (n--) {
            *(unsigned char *)0 = b;
            sum += *(unsigned char *)a;
            if (++a == 0xC000) { a = 0xA000; ++b; }
        }
        *(unsigned char *)0 = save;
        lp = line; put("MODTRACE sum "); dec(sum); put(" of "); dec(mod_load_total); say();
    }
#endif
    if (!vr_mod_tables())
        LogText("MODTRACE error no step table");
    mod_init();
    mod_hooks_init(&mod);
    mod_seq_init(&mod);
#ifdef TRACE_TICKS
    {
        unsigned int t;
        unsigned char c;
        for (t = 0; t < TRACE_TICKS; t++) {
            lp = line; put("T "); dec(t); sp(); sdec(mod_seq.order_pos); sp();
            dec(mod_seq_pattern()); sp(); sdec(mod_seq.row); sp(); sdec(mod_seq.tick); sp();
            dec(mod_seq.speed); sp(); dec(mod_seq_tick_len(16022));
            for (c = 0; c < MOD_CHANNELS; c++) {
                const mod_chan_t *h = &mod_seq.chan[c];
                sp(); dec(h->instrument); *lp++ = ','; dec(h->period); *lp++ = ',';
                dec(h->volume); *lp++ = ','; sdec(h->vibrato_add); *lp++ = ',';
                sdec(h->tremolo_add); *lp++ = ','; sdec(h->arpeggio_add);
            }
            say();
            mod_seq_tick();
        }
        LogText("MODTRACE ticks done");
        return 0;
    }
#else
    mod_player_start();
#ifdef VIA_IRQ
    vr_audio_on();
    mod_irq_armed = 1;
    mod_aflow_on();
    while (chunk_count < CHUNK_LIMIT)
        ;
    vr_audio_off();
    mod_stop();
#else
    while (chunk_count < CHUNK_LIMIT) {
        mod_update();
    }
#endif
    lp = line; put("MODTRACE done "); hex(chunk_count); sp(); hex(chunk_hash);
    put(" batches "); dec(mod_batches); say();
    return 0;
#endif
}
