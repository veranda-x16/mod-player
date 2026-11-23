/*
 * vmp.h -- the Veranda MOD Player's parts (examples/veranda/vmp*.c).
 *
 * Where the code lives (cfg/veranda-mod.cfg, app.hdr overlays = banked):
 *   main RAM     vmodplay.c: the start, the folder's entries (in RAM banks),
 *                loading and playing, what maps other banks (the window's
 *                chrome, the Open dialog); every table, string and buffer
 *                the kernel reads (rodata stays here: the kernel maps its
 *                own banks at $A000)
 *   overlay 1    the engine's step table (STEPTAB)
 *   overlay 2    vmp_pat.c: the pattern view, the song's length (pv()),
 *                the window's frame; the SDK's window chrome (vrwin.c,
 *                Veranda Look 2: win_title, win_status, win_hit map it)
 *   overlay 3    vmp_ui.c: the window, the playlist, the fields, the VU
 *                meters, the menus' routines, the keys and the pointer;
 *                mapped for good as appBank, so Veranda's calls to the
 *                program (vectors, menus, icons) find it at $A000
 *   overlay 4    vmp_dlg.c: the about box, the desk accessories, the Mods
 *                folder (call4(): appBank while it is open)
 *   overlay 5    the SDK's Open dialog (vrchoose.c, vr_file: choose()
 *                maps it; its BSS in main RAM)
 * Code in an overlay calls main RAM and its own overlay only; a routine in
 * main RAM that maps another bank puts the caller's back before it
 * returns.
 *
 * MIT licence (see LICENSE).
 */
#ifndef VMP_H
#define VMP_H
#include <veranda.h>
#include <vr.h>
#include <vrpath.h>              /* the card's folders (Veranda V71) */
#include <theme.h>               /* Veranda Look 2's palette (V74) */
#include <vrwin.h>               /* its window chrome (V79) */
#include <vrchoose.h>            /* its Open dialog (V79) */
#include <string.h>
#include "x16mod.h"
#include "vr_mod.h"

#define MAPBANK(b) (*(volatile unsigned char *)0 = (b))
#define BANK       (*(volatile unsigned char *)0)

/*---------------------------------------------------------------
  the layout: pixels, and cards (8 x 8) where the colour map is used
 ---------------------------------------------------------------*/
/* the window (Veranda Look 2's chrome, vrwin.h; DECISIONS V79): card
   columns 1-78, card rows 3-58 (pixels 8-631 by 24-471); its title band
   rows 3-4 (the module's title, the close glyph at the right), its status
   bar rows 57-58; a frame and the dither shadow */
#define WIN_X      1
#define WIN_Y      3
#define WIN_W      78
#define WIN_H      56
#define WIN_L      8
#define WIN_T      24
#define WIN_R      631
#define WIN_B      471

#define LP_L       15           /* the left panel: the playlist or pattern */
#define LP_T       47
#define LP_R       336
#define LP_B       328
#define LIST_COL   2            /* its text: card columns 2-40 */
#define LIST_X     16
#define LIST_XR    326
#define LIST_Y     64           /* the first row's top (card row 8) */
#define ROW_H      16           /* two cards: the selection is their colour */
#define LIST_N     16           /* rows shown (card rows 8-39) */
#define SB_COL     41           /* the scroll bar: card column 41 */
#define SB_ROW0    8            /* its up arrow; the track 9-38; down 39 */
#define SB_ROWN    39
#define SB_TRACK   30
#define PAT_ROW0   9            /* the pattern's rows: card rows 9-40 */
#define PAT_ROWS   32

#define RP_L       343          /* the right panel: now playing */
#define RP_T       47
#define RP_R       624
#define RP_B       328
#define RP_X       352          /* its text's left */
#define PROG_COL   44           /* the progress bar: card columns 44-76 */
#define PROG_W     33
#define PROG_ROW   22
#define ICON_Y     192          /* the transport buttons: 4 cards x 24 */
#define ICON_COL0  48           /* columns 48, 53, 58, 63, 68 */
#define LED_ROW    28           /* the lights under play, pause, stop */

#define VP_L       15           /* the VU meters' panel */
#define VP_T       343
#define VP_R       624
#define VP_B       440
#define VU_COL     5            /* the segments: card columns 5-76 */
#define VU_W       72
#define VU_ROW0    44           /* bars at rows 44, 47, 50, 53 (2 rows) */

#define STAT_LINE  456          /* the status bar's line (rows 57-58, vrwin.c) */
#define STAT_SPLIT 420          /* its left text's room ends (the modes at the right) */

/* colours: Veranda Look 2's palette entries (theme.h; theme_palette, the
   light or dark palette: DECISIONS V74) */
#define C_SEL      hilite       /* the selection: the theme's highlight */
#define C_BAR      CHROME       /* panel heads, the progress bar's track */
#define C_THUMB    SELECT       /* the scroll bar's thumb (its track chrome) */
#define C_VU_OFF   CHROME       /* a meter's unlit segment */
#define C_GREEN    GREEN
#define C_YELLOW   YELLOW
#define C_RED      RED
#define C_ROW      SELECT       /* the pattern's row, the progress */
extern unsigned char hilite;            /* the theme's highlight (SELECT without one) */

/*---------------------------------------------------------------
  vmodplay.c (main RAM)
 ---------------------------------------------------------------*/
#define ENT_MAX    255
extern unsigned char n_ent, n_mods, top, sel;
extern char cur[64];                    /* an entry read back: type, name */
void ent_get(unsigned char i);
unsigned char at_root(void);
void list_read(void);
unsigned char find_entry(const char *name);

extern mod_info_t mod;
extern char playing_name[64];
extern unsigned char playing_ent, loaded, paused, repeat, shuffle;
extern unsigned char last_song_ends;
extern unsigned long mod_bytes;         /* the module file's size */
extern volatile unsigned int frames;
extern unsigned char ui_bank, pat_bank, dlg_bank, file_bank;
extern unsigned int song_secs;          /* the song's length (vmp_pat.c) */
extern unsigned int t_base;             /* elapsed at the song's start */
extern char playing_dir[64];            /* the folder of the module last played or tried */
unsigned char music_dir(void);
mod_err_t play_entry(unsigned char e);
void stop(void);
void call2(void (*f)(void));
void call4(void (*f)(void));
unsigned char da_run(const char *name);
/* the window's chrome (vrwin.h) through main RAM: overlay 2 mapped, the
   caller's bank back after */
void __fastcall__ win_title(const char *title);
void win_status(const char *left, const char *right);
unsigned char win_hit(unsigned int x, unsigned int y);
extern const struct vrwin win;
/* the Open dialog (vrchoose.h, overlay 5): 1 and the module's name in name
   (64 bytes), its folder in curDirName; 0 cancelled */
unsigned char __fastcall__ choose(char *name);

/* the ROM's 8 x 8 font (glyphs 32-127) for PutCards (the pattern view:
   vmp_pat.c's text) */
extern unsigned char glyphs[96 * 8];
char *num(unsigned int v, unsigned char digits);
extern char nb[12];
extern char sb[80];                     /* a string for the kernel */

/* vmp_draw.c: drawing helpers (main RAM) */
void clear(unsigned int l, unsigned int t, unsigned int r, unsigned int b);
unsigned int strw(const char *s);       /* in the current style */
void fit(const char *s, unsigned int w);        /* s into sb, cut to w pixels */
void field_text(const char *s, unsigned int x, unsigned int y, unsigned int w, unsigned char bold);
void label(const char *s, unsigned int x, unsigned int y);
char *dec(char *p, unsigned int v);     /* v in decimal at p: the end */
char *mmss(char *p, unsigned int s);    /* m:ss */

/* the desk accessories found in the system folder (the MOD Player menu) */
#define DA_MAX     6
extern char da_names[DA_MAX][17];
extern unsigned char n_da;

#if defined(VMP_TEST) || defined(VMP_STRESS)
#define VMP_T                           /* a test build */
/* the cycles of a piece of work, without the audio batches inside it
   (VERA's cycle counter, vr_audio.s's vr_batch_cyc: VR_CYCLES); kind 0 an
   update pass, 1 the pattern view drawn whole, 2 the playlist drawn
   whole, 3 the playlist scrolled, 4 the VU meters, 5 the fields and the
   progress bar, 6 the pattern view's row (a pass's); logged at the quit */
void vmp_meter_start(unsigned char kind);
void vmp_meter_end(unsigned char kind);
void vmp_meter_log(void);               /* (vmp_dlg.c, overlay 4: call4) */
unsigned long cyc(void);
extern unsigned long vr_batch_cyc;      /* (vr_audio.s: VR_CYCLES) */
extern unsigned long m_max[], m_sum[];
extern unsigned int m_n[];
extern unsigned int und_total;
#endif

#if defined(VMP_T) || defined(VMP_LOG)
extern unsigned char log_e;             /* (vmp_dlg.c's log_track: entry log_e started) */
void log_track(void);
void logs(const char *a, const char *b);
#define LOG(a, b) logs(a, b)
#else
#define LOG(a, b)
#endif

/*---------------------------------------------------------------
  vmp_pat.c (overlay 2, through call2)
 ---------------------------------------------------------------*/
extern unsigned char pv_force;
void pv_update(void);                   /* the pattern view (pv_force: all) */
void pv_header(void);
void song_scan(void);                   /* song_secs */
void pv_off(void);
void frame(void);                       /* the window, drawn whole (and its colours) */
void vu_update(void);                   /* the VU meters (the colour map) */
void text(unsigned char x, unsigned int y, const char *s, unsigned char w, unsigned char flags);

/*---------------------------------------------------------------
  vmp_ui.c (overlay 3, appBank)
 ---------------------------------------------------------------*/
extern unsigned char view_pattern;

void ui_start(void);
void ui_loading(void);                  /* mod_load_tick's work */
void ui_status(const char *s);
void ui_track(void);                    /* a module started or stopped */
void ui_list(void);                     /* the playlist again (a new folder) */
void ui_menus(void);
void ui_play(unsigned char e);          /* play entry e, with the busy pointer */

/*---------------------------------------------------------------
  vmp_dlg.c (overlay 4, through call4)
 ---------------------------------------------------------------*/
extern char dlg_name[64];
extern unsigned int barx[4];            /* the menu bar's items' middles (the test's script) */
void menu_layout(void);                 /* the menus' widths, the bar's end (vmp_ui.c's tables) */
void dlg_about(void);
void dlg_message(void);                 /* sb: the message */
void da_scan(void);
void mods_dir(void);                    /* the Mods folder (MODS.CFG, else /MODS) */
void vmp_test_start(void);              /* VMP_TEST: the session's script */

#endif
