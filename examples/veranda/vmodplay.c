/*
 * vmodplay.c -- Veranda MOD Player: x16-mod's engine as a Veranda
 * application (make veranda; app.hdr). Main RAM's part (vmp.h says what
 * is where): the start, the folder's entries, loading and playing, and
 * every routine that maps another RAM bank.
 *
 * The screen (vmp_ui.c), Veranda Look 2's (DECISIONS V74, V77, V79): the
 * kernel's menu bar (theme_menus: the Veranda mark; MOD Player, File,
 * Play, View in the UI font; the clock and the speaker); the SDK's window
 * chrome (vrwin.h: the title band on the theme's accent with the module's
 * name and the close glyph at the right, the status bar, a frame and the
 * dither shadow) over the desk's colour, in Look 2's palette; in it the
 * playlist (the folder's folders and modules, a scroll
 * bar) or the pattern view, "now playing" (the title, the position, the
 * pattern, the row, speed, BPM, the track, the time and the song's length,
 * a progress bar, the transport buttons as GEOS icons with their lights),
 * the four channels' VU meters (the colour map: green, yellow, red, the
 * peak held), and the status bar. The window's text in the system font
 * (WPutString), its chrome in the UI font; the pattern view in the ROM's
 * 8 x 8 font (PutCards). Everything is drawn on
 * the foreground only: the menus and dialog boxes keep what they cover on
 * Veranda's save-under stack (DECISIONS V14). The fields are redrawn when
 * their values change, the meters through the colour map only.
 *
 * The music comes from VERA's AFLOW interrupt through intFirstVector
 * (vr_audio.s; Veranda DECISIONS V52), so menus, dialog boxes and file
 * listings do not stop it.
 *
 * Started by the shell with a module (app.hdr's documents = MOD, Veranda
 * V48): r0L & ST_LD_DATA, the name in dataLongName (its whole FAT name,
 * V82), its folder in curDirName. File > Open is Veranda's Open dialog
 * (vr_file, V79), long names too.
 *
 * VMP_TEST: the test build (tools/gate_vmodplay.sh): a script through
 * mouseSource (test/vr_play.s) and lines on the console.
 *
 * MIT licence (see LICENSE).
 */
#include "vmp.h"

/*---------------------------------------------------------------
  text in cards: the ROM's 8 x 8 font (glyphs 32-127), PutCards
 ---------------------------------------------------------------*/
unsigned char glyphs[96 * 8];
static const unsigned char smiley[8] = { 0x7E, 0x81, 0xA5, 0x81, 0xBD, 0x99, 0x81, 0x7E };

static void load_font(void)
{
    unsigned char keep;
    unsigned int a, f = 0xC800;
    asm("sei");                 /* ROM bank 6 has no interrupt vectors */
    keep = *(unsigned char *)1;
    *(unsigned char *)1 = 6;
    for (a = 0xC000; a; a += 0x800)
        if (!memcmp((void *)(a + 8), smiley, 8)) {
            f = a;
            break;
        }
    memcpy(glyphs, (void *)(f + 256), sizeof glyphs);
    *(unsigned char *)1 = keep;
    asm("cli");
}

char sb[80];

char nb[12];
char *num(unsigned int v, unsigned char digits)
{
    unsigned char i = digits;
    nb[i] = 0;
    while (i) {
        nb[--i] = (char)('0' + v % 10);
        v /= 10;
    }
    return nb;
}

#ifdef VMP_T
extern unsigned long vr_batch_cyc;
#define C0 (*(volatile unsigned char *)0x9FB8)  /* (read first: it latches all four) */
#define C1 (*(volatile unsigned char *)0x9FB9)
#define C2 (*(volatile unsigned char *)0x9FBA)
#define C3 (*(volatile unsigned char *)0x9FBB)
unsigned long cyc(void)
{
    unsigned long v;
    v = C0;
    v |= (unsigned long)C1 << 8;
    v |= (unsigned long)C2 << 16;
    v |= (unsigned long)C3 << 24;
    return v;
}
#define MK 7
static unsigned long m_t0[MK], m_b0[MK];
unsigned long m_max[MK], m_sum[MK];     /* (vmp_dlg.c's vmp_meter_log) */
unsigned int m_n[MK];
void vmp_meter_start(unsigned char k)
{
    asm("sei");                 /* (a batch between the two readings would count in one only) */
    m_b0[k] = vr_batch_cyc;
    m_t0[k] = cyc();
    asm("cli");
}
void vmp_meter_end(unsigned char k)
{
    unsigned long d, b;
    asm("sei");
    d = cyc() - m_t0[k];
    b = vr_batch_cyc - m_b0[k];
    asm("cli");
    d -= b;
    if (d > m_max[k])
        m_max[k] = d;
    m_sum[k] += d;
    ++m_n[k];
}
#endif

#if defined(VMP_T) || defined(VMP_LOG)
static char lb[96];
void logs(const char *a, const char *b)
{
    strcpy(lb, "VMP ");
    strcat(lb, a);
    if (b) {
        strcat(lb, " ");
        strcat(lb, b);
    }
    LogText(lb);
}
#endif

/*---------------------------------------------------------------
  the folder: its entries in RAM banks, 64 bytes each: type, name.
  Types: U "..", D a folder, M a module, L a module whose name is too
  long to open (DirNext keeps 63 characters)
 ---------------------------------------------------------------*/
#define ENT_BANKS  2
static unsigned char ent_bank;
unsigned char n_ent, top, sel, n_mods;
static unsigned char ent[3 + 64];       /* DirNext's entry */
char cur[64];
static unsigned char order[ENT_MAX];    /* the entries sorted: their slots */

static char *slot(unsigned char i)      /* (maps its bank: put the caller's back) */
{
    MAPBANK((unsigned char)(ent_bank + (i >> 7)));
    return (char *)(0xA000 + ((unsigned int)(i & 127) << 6));
}

void ent_get(unsigned char i)
{
    unsigned char save = BANK;
    memcpy(cur, slot(order[i]), 64);
    MAPBANK(save);
}

static void ent_put(unsigned char type, const char *name)
{
    unsigned char save = BANK;
    char *p;
    p = slot(n_ent);
    order[n_ent] = n_ent;
    p[0] = type;
    strncpy(p + 1, name, 62);
    p[63] = 0;
    MAPBANK(save);
    ++n_ent;
}

static unsigned char lower(unsigned char c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

/* "x.mod" or the Amiga's "mod.x" */
static unsigned char is_mod(const char *s)
{
    unsigned char l = (unsigned char)strlen(s);
    if (l > 4 && s[l - 4] == '.' && lower(s[l - 3]) == 'm' && lower(s[l - 2]) == 'o' && lower(s[l - 1]) == 'd')
        return 1;
    return l > 4 && lower(s[0]) == 'm' && lower(s[1]) == 'o' && lower(s[2]) == 'd' && s[3] == '.';
}

unsigned char at_root(void) { return curDirName[1] == 0; }

/* entries a and b (slots) in order: type (.., folders, modules), then the
   name without regard to case */
static signed char ent_cmp(unsigned char a, unsigned char b)
{
    unsigned char save = BANK, i, x, y;
    signed char r = 0;
    memcpy(cur, slot(a), 64);
    {
        const char *q = slot(b);
        x = (unsigned char)(cur[0] == 'U' ? 0 : cur[0] == 'D' ? 1 : 2);
        y = (unsigned char)(q[0] == 'U' ? 0 : q[0] == 'D' ? 1 : 2);
        if (x != y)
            r = x < y ? -1 : 1;
        else
            for (i = 1; i < 64; ++i) {
                x = lower(cur[i]);
                y = lower(q[i]);
                if (x != y) { r = x < y ? -1 : 1; break; }
                if (!x) break;
            }
    }
    MAPBANK(save);
    return r;
}

static void list_sort(void)
{
    unsigned char i, j, t;
    for (i = 1; i < n_ent; ++i) {
        t = order[i];
        for (j = i; j && ent_cmp(order[j - 1], t) > 0; --j)
            order[j] = order[j - 1];
        order[j] = t;
    }
}

/* The whole listing is read before any file is opened (Veranda V16: on
   the SD card a file opened ends a listing): folders, then modules. */
void list_read(void)
{
    unsigned char pass;
    n_ent = 0;
    n_mods = 0;
    if (!at_root())
        ent_put('U', "..");
    for (pass = 0; pass < 2; ++pass) {
        if (DirOpen("*"))
            continue;
        while (DirNext(ent) == 0 && n_ent < ENT_MAX) {
            const char *nm = (const char *)ent + 3;
            if (nm[0] == '.' && (nm[1] == 0 || (nm[1] == '.' && nm[2] == 0)))
                continue;
            if (pass == 0 && ent[0] == 'D')
                ent_put('D', nm);
            else if (pass == 1 && ent[0] == 'F' && is_mod(nm)) {
                ent_put(strlen(nm) >= 62 ? 'L' : 'M', nm);
                ++n_mods;
            }
        }
    }
    list_sort();
    top = sel = 0;
}

/* the entry named name (a module), or 0xFF */
unsigned char find_entry(const char *name)
{
    unsigned char i;
    for (i = 0; i < n_ent; ++i) {
        ent_get(i);
        if (cur[0] == 'M' && !strcmp(cur + 1, name))
            return i;
    }
    return 0xFF;
}

/*---------------------------------------------------------------
  the overlays' banks: overlay 3 is appBank; 2, 4 and 5 for a call
 ---------------------------------------------------------------*/
unsigned char ui_bank, pat_bank, dlg_bank, file_bank;
unsigned char hilite;

/* the window (vrwin.h): the SDK's vrwin.c is in overlay 2; the UI
   (overlay 3) reaches it through these, which put its bank back */
const struct vrwin win = { WIN_X, WIN_Y, WIN_W, WIN_H, 0, 2, VW_CLOSE | VW_FRAME | VW_SHADOW };

void __fastcall__ win_title(const char *title)
{
    unsigned char b = BANK;
    MAPBANK(pat_bank);
    vr_win_title(&win, title);
    MAPBANK(b);
}

void win_status(const char *left, const char *right)
{
    unsigned char b = BANK;
    MAPBANK(pat_bank);
    vr_win_status(&win, left, right);
    MAPBANK(b);
}

unsigned char win_hit(unsigned int x, unsigned int y)
{
    unsigned char b = BANK, r;
    MAPBANK(pat_bank);
    r = vr_win_hit(&win, x, y);
    MAPBANK(b);
    return r;
}

/* the Open dialog (vrchoose.h, Veranda DECISIONS V79), compiled into
   overlay 5: modules (by is_mod) and folders, by their long FAT names;
   called with its overlay mapped (it maps a bank of its own for the
   entries through BankCopy, and the kernel calls its routines through
   vr_tramp), the caller's bank back after */
static unsigned char is_mod(const char *s);
static unsigned char mod_filter(const char *name)
{
    return is_mod(name);
}

unsigned char __fastcall__ choose(char *name)
{
    unsigned char b = BANK, r;
    vr_file_filter = mod_filter;
    MAPBANK(file_bank);
    r = vr_file(VRF_OPEN, "Open a module", name);
    MAPBANK(b);
    return r;
}

void call2(void (*f)(void))
{
    unsigned char b = BANK;
    MAPBANK(pat_bank);
    f();
    MAPBANK(b);
}

void call4(void (*f)(void))
{
    appBank = dlg_bank;
    MAPBANK(dlg_bank);
    f();
    appBank = ui_bank;
    MAPBANK(ui_bank);
}

/* a desk accessory (GetFile): it comes back here with RstrAppl, the audio
   routine's vector cleared and AFLOW off (Veranda V41, V52): both again,
   and the DAC's rate if it was playing */
unsigned char da_run(const char *name)
{
    unsigned char r;
#ifdef VMP_TEST
    /* the session's script is mouseSource, which an accessory's start and
       end clear: kept here; the accessory gets an Esc (it closes) */
    unsigned int src = mouseSource;
    asm("lda #$1B");
    asm("jsr $FEC3");           /* the ROM's kbdbuf_put */
#endif
    strcpy(sb, name);
    r = GetFile(sb, 0, 0, 0, 0);
#ifdef VMP_TEST
    mouseSource = src;
    if (r) {
        asm("jsr $FFE4");       /* (it did not start: its Esc taken back, GETIN) */
    }
#endif
    appBank = ui_bank;
    MAPBANK(ui_bank);
    vr_audio_on();
    if (loaded && !paused)
        mod_pause(0);
    return r;
}

/*---------------------------------------------------------------
  playing
 ---------------------------------------------------------------*/
mod_info_t mod;
char playing_name[64];
unsigned char playing_ent = 0xFF;      /* the list entry playing */
unsigned char loaded, paused, repeat, shuffle;
unsigned char last_song_ends;
unsigned long mod_bytes;
unsigned int song_secs, t_base;
volatile unsigned int frames;
static void tick(void) { ++frames; }

#ifdef VMP_T
static unsigned char load_logged;
#endif
/* every 4 KB loaded (vr_mod_load, with any bank mapped) */
static void load_tick(void)
{
    unsigned char b = BANK;
#ifdef VMP_T
    if (!load_logged++)
        LOG("LOAD POINTER", num(pointerShape, 3));
#endif
    MAPBANK(ui_bank);
    ui_loading();
    MAPBANK(b);
}

#ifdef VMP_T
unsigned int und_total;
#endif
void stop(void)
{
    unsigned char b = BANK;
#ifdef VMP_T
    if (loaded)
        und_total += mod_status.underruns;
#endif
    mod_stop();
    loaded = 0;
    paused = 0;
    vr_mod_free();
    MAPBANK(b);
}

/* load entry e (a module) and start it: MOD_OK, or the error (nothing
   plays then). The engine maps its banks: the caller's is put back. */
mod_err_t play_entry(unsigned char e)
{
    mod_err_t r;
    unsigned char i;
    ent_get(e);
    if (cur[0] != 'M')
        return MOD_ERR_OPEN;
    strcpy(playing_name, cur + 1);
    stop();                     /* (playback stops while a module loads) */
    playing_ent = 0xFF;
    /* the header (1,084 bytes) is read into the third of our banks, not
       fixed RAM (FRead takes a buffer at $A000 in the caller's bank:
       Veranda V47) */
#ifdef VMP_T
    load_logged = 0;
#endif
    i = BANK;
    MAPBANK((unsigned char)(ent_bank + ENT_BANKS));
    r = vr_mod_load(playing_name, &mod, (unsigned char *)0xA000);
    MAPBANK(i);
    if (r != MOD_OK) {
        LOG("ERROR", playing_name);
        return r;
    }
    mod_bytes = mod_load_total;
    playing_ent = e;
    loaded = 1;
    call2(song_scan);
    mod_play(&mod, 0);          /* (it maps the patterns' banks) */
    MAPBANK(i);
    last_song_ends = mod_status.song_ends;
    t_base = 0;
#if defined(VMP_T) || defined(VMP_LOG)
    log_e = e;
    call4(log_track);                   /* (overlay 4) */
#endif
    return MOD_OK;
}

/* the playlist's folder made the music's again (the folder of the module
   last played or tried; next, previous, a song's end): 0 if it is shown,
   1 if curDirName changed to it (the caller reads the list) */
char playing_dir[64];
unsigned char music_dir(void)
{
    if (!playing_dir[0] || !strcmp(playing_dir, (const char *)curDirName))
        return 0;
    if (ChDir((void *)playing_dir)) {
        ChDir((void *)curDirName);      /* (it went: the list stays) */
        return 0;
    }
    strcpy((char *)curDirName, playing_dir);
    return 1;
}

/*---------------------------------------------------------------
  the desk accessories in the system folder, for the MOD Player menu
  (vmp_dlg.c's da_scan)
 ---------------------------------------------------------------*/
char da_names[DA_MAX][17];
unsigned char n_da;

int main(void)
{
    unsigned char doc = (unsigned char)(r0L & ST_LD_DATA);     /* (before anything uses r0) */
    unsigned char i;
    char name[LONG_MAX + 1];

    if (doc) {                          /* (the whole name, long or not: API 5, V82) */
        memcpy(name, (void *)dataLongName, LONG_MAX);
        name[LONG_MAX] = 0;
    }
    load_font();
    ent_bank = BankAlloc(ENT_BANKS + 1);       /* (+1: the header's) */
    if (!ent_bank) {
        LogText("VMP NO BANKS");
        EnterDeskTop();
    }
    pat_bank = vr_overlay_bank(2);
    ui_bank = vr_overlay_bank(3);
    dlg_bank = vr_overlay_bank(4);
    file_bank = vr_overlay_bank(5);
    if (!vr_mod_tables() || !pat_bank || !ui_bank || !dlg_bank || !file_bank) {
        LogText("VMP NO OVERLAYS");
        EnterDeskTop();
    }
    appBank = ui_bank;
    MAPBANK(ui_bank);
    mod_init();
    vr_audio_on();
    vr_set_irq(tick);
    mod_load_tick = load_tick;
    if (curDirName[0])                  /* (started from the top folder, or */
        ChDir((void *)curDirName);      /* from /VERANDA/APPS: Veranda's V71) */
    if (!curDirName[0]) {
        curDirName[0] = '/';
        curDirName[1] = 0;
    }
    if (!doc)                   /* (no module: the Mods folder, MODS.CFG's */
        call4(mods_dir);        /* or /MODS, if it is there) */
    call4(da_scan);             /* (it comes back to curDirName) */
    list_read();
    ui_start();
    LogText("VMP RUNNING");
    if (doc) {
        i = find_entry(name);
        if (i != 0xFF)
            ui_play(i);
        LOG("DOCUMENT", name);
    }
    return 0;
}
