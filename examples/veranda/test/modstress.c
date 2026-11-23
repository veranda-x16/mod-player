/*
 * modstress.c -- real-time playback under Veranda with the system busy
 * (make veranda-stress; tools/gate_veranda_rt.sh; M3).
 *
 * music.mod plays from VERA's AFLOW interrupt (vr_audio.s through
 * intFirstVector) while:
 *   - a script (vr_play.s, through mouseSource) opens the "file" menu every
 *     four seconds, picks "about", and presses the dialog box's OK;
 *   - appMain runs one step at a time: CopyRect of half the screen x20, a
 *     PutCards screen x10, a directory listing (DirOpen/DirNext), a 64 KB
 *     read of data.bin, a pause.
 * At SECONDS of music it logs "MODSTRESS <name> <decimal>" lines: underruns
 * (chunks that found the FIFO empty: 0 is the gate), batches, the cycles
 * the batches took, the steps and dialog boxes done, and the quiet and
 * playing times of a CopyRect and a PutCards screen.
 */
#include <veranda.h>
#include <vr.h>
#include "x16mod.h"
#include "vr_mod.h"

#ifndef SECONDS
#define SECONDS 180
#endif

extern void __fastcall__ vr_play(const void *script);
extern unsigned long vr_batch_cyc;

#define C0 (*(volatile unsigned char *)0x9FB8)
#define C1 (*(volatile unsigned char *)0x9FB9)
#define C2 (*(volatile unsigned char *)0x9FBA)
#define C3 (*(volatile unsigned char *)0x9FBB)
static unsigned long cyc(void)
{ unsigned long v; v = C0; v |= (unsigned long)C1 << 8; v |= (unsigned long)C2 << 16; v |= (unsigned long)C3 << 24; return v; }

static unsigned char hdr[MOD_HEADER_SIZE];
static mod_info_t mod;
static char line[64];
static char *lp;
static void put(const char *t) { while (*t) *lp++ = *t++; }
static void dec(unsigned long v)
{ char b[11]; unsigned char n = 0; do { b[n++] = (char)('0' + v % 10); v /= 10; } while (v); while (n) *lp++ = b[--n]; }
static void say(const char *tag, unsigned long v)
{ lp = line; put("MODSTRESS "); put(tag); *lp++ = ' '; dec(v); *lp = 0; LogText(line); }

struct item { const char *text; unsigned char type; void (*fn)(void); };
struct sub { unsigned char top, bottom; unsigned int left, right; unsigned char flags; struct item items[1]; };
struct bar { unsigned char top, bottom; unsigned int left, right; unsigned char flags; struct item items[1]; };

static void about(void);
static struct sub filemenu = { 14, 28, 0, 60, VERTICAL | 1, { { "about", MENU_ACTION, about } } };
static struct bar bar = { 0, 14, 0, 80, HORIZONTAL | UN_CONSTRAINED | 1, { { "file", SUB_MENU, (void (*)(void))&filemenu } } };

static const unsigned char dlg[] = {
    DEF_DB_POS | 1,
    DBTXTSTR, TXT_LN_X, TXT_LN_2_Y, 0, 0,
    OK, DBI_X_0, DBI_Y_2,
    0
};
static unsigned char table[sizeof dlg];
static const char text[] = "Music plays under this box.";
static unsigned int dialogs, steps[5];
static unsigned char busy, step, done, unit, fh;
static char txt[81];
static unsigned char buf[1024];
static unsigned char entry[3 + 64];

/* frame, x, y | flags << 8 (bit 0 the button, bit 6 y + 256) */
#ifndef FIRSTF
#define FIRSTF 60
#endif
#define CYCLES ((SECONDS + 20) / 10)
static unsigned int script[CYCLES * 8 * 3 + 3];
extern volatile unsigned int vr_play_frame;     /* vr_play.s: the script's frame */
#define frames vr_play_frame
static unsigned char quiet(void)
{
    unsigned int t;
    if (frames < FIRSTF - 45)
        return 1;
    t = (frames - (FIRSTF - 45)) % 600;
    return t < 67 || (t >= 60 && t < 127) || (t >= 180 && t < 247);
}

/* a position from frame f: the button down if b */
static void at(unsigned int **p, unsigned int f, unsigned int x, unsigned int y, unsigned char b)
{
    unsigned int *q = *p;
    *q++ = f; *q++ = x; *q++ = (y & 255) | (y >= 256 ? 0x4000 : 0) | (b ? 0x100 : 0);
    *p = q;
}

static void about(void)
{
    ++dialogs;
    busy = 1;
    DoDlgBox(table);
    busy = 0;
    ReDoMenu();
}

static unsigned long copy(void) { unsigned long t = cyc(); CopyRect(0, 80, 32, 240, 0, 240); return cyc() - t; }
static unsigned long cards(void)
{ unsigned char r; unsigned long t = cyc(); for (r = 4; r < 60; ++r) PutCards(txt, (void *)0x0801, 0, 80, r * 8, 0); return cyc() - t; }

static void stress(void)
{
    unsigned char i;
    static const unsigned char units[5] = { 20, 10, 1, 1, 1 };
    if (busy || done || quiet())
        return;
    /* one unit a call, so that the main loop sees the script's presses */
#ifdef NOSTRESS
    if (0)
#endif
    switch (step) {
    case 0: copy(); break;                              /* x20 */
    case 1: cards(); break;                             /* x10 */
    case 2: if (DirOpen("*") == 0) while (DirNext(entry) == 0) ; break;
    case 3:                                             /* 64 KB, 4 KB a call */
        if (!fh) fh = FOpen("data.bin", 'r');
        if (fh >= 2) {
            for (i = 0; i < 4; ++i)
                if (FRead(fh, buf, sizeof buf) != sizeof buf) { FClose(fh); fh = 0; break; }
        } else
            fh = 0;
        break;
    case 4: break;
    }
    if (step == 3 ? !fh : ++unit >= units[step]) {
        ++steps[step];
        unit = 0;
        if (++step == 5)
            step = 0;
    }
    if (mod_status.elapsed >= SECONDS) {
        done = 1;
        say("elapsed", mod_status.elapsed);
        say("underruns", mod_status.underruns);
        say("batches", mod_batches);
        say("batch_cycles", vr_batch_cyc);
        say("dialogs", dialogs);
        say("copies", steps[0] * 20u);
        say("cardscreens", steps[1] * 10u);
        say("listings", steps[2]);
        say("reads64k", steps[3]);
        say("copy_playing", copy());
        say("cards_playing", cards());
        mod_stop();
        vr_audio_off();
        LogText("MODSTRESS DONE");
    }
}

int main(void)
{
    unsigned char i;
    unsigned int *p = script, c;
    mod_err_t e;
    for (i = 0; i < 80; ++i) txt[i] = (char)('A' + (i & 15));
    for (i = 0; i < sizeof dlg; ++i) table[i] = dlg[i];
    table[4] = (unsigned int)text & 255;
    table[5] = (unsigned int)text >> 8;
    SetPattern(2);
    WRectangle(0, 16, 639, 479);
    say("copy_quiet", copy());
    say("cards_quiet", cards());
    e = vr_mod_load("music.mod", &mod, hdr);
    if (e != MOD_OK) {
        say("error", e);
        return 0;
    }
    say("banks", vr_mod_nbanks);
    if (!vr_mod_tables())
        LogText("MODSTRESS error no step table");
    mod_init();
    vr_audio_on();
    mod_play(&mod, 0);
    DoMenu((void *)&bar, 0);
    /* every ten seconds: "file", then where Veranda puts the pointer when
       the sub menu opens (the first item's middle: another position closes
       it), "about", the dialog box's OK, away. The script's positions are
       absolute, and a press is taken where the pointer is when the main
       loop gets to it, so stress() rests from 45 frames before each press
       to 20 after (quiet()): a step (a PutCards screen takes 35 frames
       while the music plays) would otherwise put it a frame late, somewhere else. */
    for (c = 0; c < CYCLES; ++c) {
        unsigned int f = FIRSTF + c * 600;
        at(&p, f, 12, 7, 0);
        at(&p, f + 1, 12, 7, 1);
        at(&p, f + 2, 30, 21, 0);
        at(&p, f + 60, 30, 21, 1);
        at(&p, f + 62, 30, 21, 0);
        at(&p, f + 180, 240, 272, 1);
        at(&p, f + 182, 240, 272, 0);
        at(&p, f + 300, 600, 400, 0);
    }
#ifndef STOPF
#define STOPF 0xFFF0
#endif
    *p++ = 0xFFFF; *p++ = STOPF; *p = 0;
    vr_play(script);
    appMain = VR_FN(stress);
    LogText("MODSTRESS PLAYING");
    return 0;
}
