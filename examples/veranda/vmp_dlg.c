/*
 * vmp_dlg.c -- the Veranda MOD Player's overlay 4 (vmp.h): the about box,
 * the desk accessories' names, the Mods folder. Called through call4(),
 * which maps this overlay and makes it appBank while the call lasts, so
 * the box's routine (DB_USR_ROUT) runs here. Its tables and strings are
 * in main RAM (rodata, data and bss are main RAM's).
 *
 * (The player's own open box, Veranda Calc's chooser's design, was here:
 * File > Open is now Veranda's Open dialog, vr_file, in overlay 5.)
 *
 * MIT licence (see LICENSE).
 */
#include "vmp.h"
#include "vmp_pics.h"

#pragma code-name ("OVERLAY4")
#pragma rodata-name ("OVL4RO")          /* (read here only; what the kernel reads is copied first) */

extern unsigned char __fastcall__ vmp_dlgbox(const void *box);

char dlg_name[64];
static unsigned char tab[16];

/*---------------------------------------------------------------
  the menus' layout (vmp_ui.c's tables, Veranda Look 2's bar: V74, V79)
 ---------------------------------------------------------------*/
struct item { const char *text; unsigned char type; void *p; };
struct mhd { unsigned char top, bottom; unsigned int left, right; unsigned char flags; struct item i[1]; };
extern struct mhd mgeos, mfile, mplay, mview, mbar;
#define IH 16

static const char *const barnames[4] = { "MOD Player", "File", "Play", "View" };
unsigned int barx[4];

/* s's width in the font now (vmp_draw.c's strw is overlay 3's) */
static unsigned int textw(const char *s)
{
    unsigned int w = 0;
    while (*s)
        w += GetCharWidth(*s++);
    return w;
}

/* where the kernel ends the bar's first n items (kernel/u/menu.s,
   DECISIONS V74; the SDK's vr_bar_end, here to keep it out of main RAM):
   the mark (24 pixels) and item 0 in bold, each other item 6 pixels from
   its text both sides, each item's end on a whole card; in the UI font */
static unsigned int bar_end(unsigned char n)
{
    unsigned char i;
    unsigned int e = 0;
    const char *p;
    for (i = 0; i < n; ++i) {
        p = barnames[i];
        e += i ? 6 + 6 : 24 + 6;
        while (*p)
            e += GetCharWidth(*p++) + !i;
        e = (e + 7) & ~7;
    }
    return e;
}

/* the lists' widths in the UI font (the menus' font: theme_menus); the
   kernel takes the bar's clicks up to its items' end */
void menu_layout(void)
{
    static unsigned int *const lefts[4] = { &mgeos.left, &mfile.left, &mplay.left, &mview.left };
    static unsigned int *const rights[4] = { &mgeos.right, &mfile.right, &mplay.right, &mview.right };
    static const char *const longest[4] = { "About MOD Player", "Open...", "Shuffle: off", "* Pattern view" };
    unsigned int e[5], w;
    unsigned char i;
    mgeos.flags = (unsigned char)(VERTICAL | (1 + n_da));
    mgeos.bottom = (unsigned char)(16 + (1 + n_da) * IH);
    UseFont(FONT_UI);
    for (i = 0; i < 5; ++i)
        e[i] = bar_end(i);
    for (i = 0; i < 4; ++i) {
        *lefts[i] = e[i];
        barx[i] = (e[i] + e[i + 1]) >> 1;
        w = textw(longest[i]);
        if (i == 0) {
            unsigned char k;
            for (k = 0; k < n_da; ++k)
                if (textw(da_names[k]) > w)
                    w = textw(da_names[k]);
        }
        *rights[i] = e[i] + w + 16;
    }
    UseSystemFont();
    mbar.right = e[4];
}

/*---------------------------------------------------------------
  the about box
 ---------------------------------------------------------------*/
#define AB_TOP   152
#define AB_BOT   327
#define AB_LEFT  168
#define AB_RIGHT 471

static void ab_draw(void)
{
    static const char *const lines[] = {
        "ProTracker modules, four channels,",
        "16 kHz stereo from VERA's audio",
        "interrupt, for Veranda.",
        "x16-mod's engine. MIT licence.",
    };
    unsigned char i;
    WBitmapUp((void *)pic_app, (AB_LEFT + 16) >> 3, 3, AB_TOP + 16, 21);
    currentMode = SET_BOLD;
    strcpy(sb, "MOD Player 1.2");
    WPutString(sb, AB_LEFT + 56, AB_TOP + 26);
    currentMode = 0;
    for (i = 0; i < 4; ++i) {
        strcpy(sb, lines[i]);
        WPutString(sb, AB_LEFT + 56, AB_TOP + 50 + 14 * i);
    }
}

static const unsigned char ab_tab[] = {
    SET_DB_POS | WIDE_DB | 1,
    AB_TOP & 255, AB_TOP >> 8, AB_BOT & 255, AB_BOT >> 8,
    AB_LEFT & 255, AB_LEFT >> 8, AB_RIGHT & 255, AB_RIGHT >> 8,
    DB_USR_ROUT, 0, 0,
    OK, 29, 148,
    0
};

void dlg_about(void)
{
    memcpy(tab, ab_tab, sizeof ab_tab);
    tab[10] = (unsigned char)VR_FN(ab_draw);
    tab[11] = (unsigned char)(VR_FN(ab_draw) >> 8);
    vmp_dlgbox(tab);
}

/*---------------------------------------------------------------
  the desk accessories with the applications (/VERANDA/APPS, else
  /VERANDA, else the root: Veranda's vrpath.h, V71), for the MOD Player menu;
  back to curDirName
 ---------------------------------------------------------------*/
void da_scan(void)
{
    vr_cd(VR_APPS);
    n_da = (unsigned char)(DA_MAX - FindFTypes(da_names, DESK_ACC, DA_MAX, 0));
    if (n_da > DA_MAX)
        n_da = 0;
    ChDir((void *)curDirName);
}

/*---------------------------------------------------------------
  the Mods folder (Veranda's docs/APPFORMAT.md, "The Mods folder";
  V84): MODS.CFG in the data folder (vr_cd(VR_DATA): /VERANDA/.SYSTEM,
  else /VERANDA, else the root) holds the folder's path, the bytes up
  to the first LF, CR or 0. No file, a path not starting with '/' or
  longer than 61 characters: the default, /MODS. If ChDir takes it, it
  is curDirName and the current folder; if not (no such folder),
  nothing changes. (dlg_name is the buffer: main RAM, for the kernel's
  FRead.)
 ---------------------------------------------------------------*/
#define MODS_MAX 61
static char cfg_name[] = "MODS.CFG";    /* (main RAM: the kernel reads it) */

void mods_dir(void)
{
    unsigned char fh, n = 0;
    vr_cd(VR_DATA);
    fh = FOpen(cfg_name, 'r');
    if (fh >= 2) {
        n = (unsigned char)FRead(fh, dlg_name, MODS_MAX + 2);
        FClose(fh);
        if (n > MODS_MAX + 2)
            n = 0;
    }
    dlg_name[n] = 0;
    for (n = 0; dlg_name[n] && dlg_name[n] != '\r' && dlg_name[n] != '\n'; ++n)
        ;
    dlg_name[n] = 0;
    if (dlg_name[0] != '/' || n > MODS_MAX)
        strcpy(dlg_name, "/MODS");
    if (ChDir(dlg_name)) {
        ChDir((void *)curDirName);
        return;
    }
    strcpy((char *)curDirName, dlg_name);
    LOG("MODS", dlg_name);
}

#if defined(VMP_T) || defined(VMP_LOG)
/* (the test builds) the module started: its place among the modules, its
   first order and pattern, its length, its name; the song's seconds */
unsigned char log_e;
void log_track(void)
{
    {
        char s[64];                     /* (37 characters and the name's first 20: once 48) */
        unsigned char i, e = log_e, t = 0;
        for (i = 0; i <= e; ++i) {
            ent_get(i);
            if (cur[0] == 'M' || cur[0] == 'L')
                ++t;
        }
        strcpy(s, num(t, 3));
        strcat(s, " ORDER ");
        strcat(s, num(mod_status.order, 3));
        strcat(s, " PATTERN ");
        strcat(s, num(mod_status.pattern, 3));
        strcat(s, " LENGTH ");
        strcat(s, num(mod.song_length, 3));
        strcat(s, " ");
        strncat(s, playing_name, 20);
        LOG("TRACK", s);
        LOG("SONG SECONDS", num(song_secs, 5));
    }
}
#endif

#ifdef VMP_T
#define MK 7
static char *ldec(char *p, unsigned long v)
{
    char t[11];
    unsigned char n = 0;
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        *p++ = t[--n];
    *p = 0;
    return p;
}
/* the meters (vmodplay.c's vmp_meter_*), logged at the quit */
void vmp_meter_log(void)
{
    static const char *const kinds[MK] = { "UPDATE", "PATTERN PANEL", "LIST PANEL", "LIST SCROLL",
                                           "VU", "FIELDS", "PATTERN ROWS" };
    unsigned char k;
    char s[64], *p;
    for (k = 0; k < MK; ++k) {
        if (!m_n[k])
            continue;
        p = ldec(s, m_n[k]);
        strcpy(p, " AVG ");
        p = ldec(p + 5, m_sum[k] / m_n[k]);
        strcpy(p, " MAX ");
        ldec(p + 5, m_max[k]);
        LOG(kinds[k], s);
    }
    ldec(s, vr_batch_cyc);
    LOG("BATCH CYCLES", s);
    ldec(s, cyc());
    LOG("CYCLES", s);
}
#endif

#ifdef VMP_TEST
#include "test/vmp_test.h"
#endif
