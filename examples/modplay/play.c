/*
 * MODPLAY -- Commander X16 MOD player with a mouse-driven interface.
 *
 * Text mode, not bitmap: fixed RAM has under 9 KB spare once the mixer's
 * tables are in, and the mixer wants most of the CPU. An 80x60 text screen
 * with per-cell colour draws a panel in a few hundred cycles, and the mouse
 * pointer is a VERA hardware sprite the KERNAL owns -- so moving it over the
 * display costs the player nothing at all.
 *
 * Every screen write is interleaved with mod_player_update(). That is not
 * cosmetic: starving the PCM FIFO flushes a partial stereo pair and the
 * output swaps channels until the next flush.
 *
 * Nothing scrolls. While music plays only the four channel lines change
 * (plus a row number, and the clock once a second); everything else
 * redraws on a track change or navigation. A scrolling pattern view was
 * tried in two forms -- a centred window redrawing 32 rows per row change
 * and a paged one redrawing two -- and both read as slow and jumpy on
 * hardware, while the full-page repaint froze the display for over a second
 * every 32 rows.
 *
 * Everything that redraws while a module plays goes through vtext.s, which
 * writes VRAM directly. conio is kept only for the frame and the buttons,
 * which are drawn once per directory.
 *
 * Layout (80x60):
 *
 *    0  --------------------------------------------------------------
 *    1   [|<] [pause] [>|]                    512k ram    x16-mod <id>
 *    2  --------------------------------------------------------------
 *    3   <title>                             <sig>  <n> pat   track i/n
 *    4   pos 07/42   row 31   spd 6    bpm 125   03:42   under 0
 *    5  --------------------------------------------------------------
 *    6   ch1  C-2  01  vol 40  c40   [level bar]
 *    ..  (one line per channel)
 *   10  --------------------------------------------------------------
 *   11   files                                     <n> entries  [^] [v]
 *   12   column A, 46 rows                |   column B, 46 rows
 *   ..
 *   58  --------------------------------------------------------------
 *   59   click to play   |< prev   >| next   space pause   q quit
 */
#include "x16mod.h"
#include "mod_play.h"
#include "mod_player.h"
#include "playlist.h"
#include "gui.h"
#include "vtext.h"

#include <conio.h>
#include <stdio.h>
#include <stdlib.h>

extern unsigned long mod_load_total, mod_load_done;
extern void (*mod_load_tick)(void);
extern mod_err_t mod_load_file(const char *filename, mod_info_t *m,
                               unsigned char *hdrbuf);
extern void mod_hooks_init(const mod_info_t *m);
extern void cache_init(void);
extern unsigned char ram_banks(void);
extern void audio_set_volume(unsigned char v);
extern unsigned char pcm_volume;
extern void mouse_init(void);
extern void mouse_poll(void);
extern unsigned int  mouse_x, mouse_y;
extern unsigned char mouse_buttons;
extern unsigned int  mod_frames_rendered;
extern unsigned int  mod_seconds;
extern unsigned int  mod_underruns;

/* Row 3: title and module info. Row 4: the status line. */
#define TITLE_X     1
#define TITLE_W    22
#define INFO_X     36
#define ST_Y        4
#define POS_X       5
#define ROW_X      17
#define SPD_X      26
#define BPM_X      35
#define CLK_X      41
#define UND_X      49

/* Rows 6-9: the channel panel -- the only thing that moves while music plays.
     ch1  C-2  01  vol 40  c40   [====....] */
#define CH_TOP      6
#define NOTE_X      6
#define SMP_X      11
#define VOL_X      19
#define FX_X       23
#define BAR_X      30
#define BAR_W      40

/* Rows 12-57: the file list, two columns of 46 -- 92 entries a page. */
#define FILES_Y    11
#define PL_TOP     12
#define PL_ROWS    46
#define PL_PAGE    (PL_ROWS * 2)
#define PL_XA       2
#define PL_XB      41
#define PL_DIV     39
#define PL_COLW    36
#define PL_NAME_W  31           /* marker(1) + number(3) + space + name */
#define HELP_Y     59

/* VERA text palette indices. */
#define C_BORDER   11           /* dark grey */
#define C_LABEL    12           /* grey      */
#define C_VALUE     1           /* white     */
#define C_NOTE      3           /* cyan      */
#define C_SMP       7           /* yellow    */
#define C_FX       13           /* light green */
#define C_TITLE     7
#define C_HOT      10           /* light red */

enum { B_PREV, B_PLAY, B_NEXT, B_MODE, B_VDN, B_VUP, B_UP, B_DOWN, B_COUNT };

static const gui_button_t buttons[B_COUNT] = {
    { 2,  1, 6, "|<"   },
    { 9,  1, 8, "play" },
    { 18, 1, 6, ">|"   },
    { 26, 1, 7, "seq"  },   /* label is overwritten with the live mode */
    { 35, 1, 3, "-"    },
    { 43, 1, 3, "+"    },
    { 70, FILES_Y, 3, "^" },
    { 74, FILES_Y, 3, "v" },
};

static void put2(unsigned char v);   /* defined below, used by the status line */

/*
 * pump() -- feed the mixer AND watch the mouse.
 *
 * The mouse used to be sampled once per main-loop pass. With a module playing
 * a pass spends most of its time drawing, so a click that began and ended
 * between two passes was never seen at all -- which is why the playlist
 * stopped responding as soon as something was loaded. Latching the press here,
 * everywhere the audio is topped up, means a click cannot fall through the gap.
 */
static void pump(void);

static unsigned char hdrbuf[MOD_HEADER_SIZE];
static mod_info_t mod;
static unsigned char barlen[MOD_CHANNELS];
static unsigned int  cur_track, list_top, list_sel;
static unsigned char paused, have_mod, prev_btn;
static unsigned char shuffle;           /* 0 = in order, 1 = random */

/*
 * A private 16-bit xorshift. Deliberately NOT mod_seq.random_seed: that one
 * drives the random vibrato/tremolo waveform and is compared tick-for-tick
 * against micromod by the trace tests, so disturbing it would break them.
 */
static unsigned int rnd_state = 0xACE1;

static unsigned int rnd_next(void)
{
    rnd_state ^= (unsigned int)(rnd_state << 7);
    rnd_state ^= (unsigned int)(rnd_state >> 9);
    rnd_state ^= (unsigned int)(rnd_state << 8);
    return rnd_state;
}
static unsigned char pend_click, pend_cx, pend_cy;

static void pump(void)
{
    unsigned char pressed;
    mod_player_update();
    mouse_poll();
    pressed = (unsigned char)(mouse_buttons & 1);
    if (pressed && !prev_btn) {
        pend_cx = (unsigned char)(mouse_x >> 3);
        pend_cy = (unsigned char)(mouse_y >> 3);
        pend_click = 1;                 /* acted on by the main loop */
    }
    prev_btn = pressed;
}
/* What the channel panel reflects: shown_hl is the row it was last updated
   for, or -1 when a track has just been loaded and the sticky note and
   sample state must start over. load_track() resets all three; shown_page
   and shown_pat remain only because it does. */
static int  shown_page = -1, shown_hl = -1;
static int  shown_pat = -1;

/* ProTracker period table, finetune 0. Periods are matched to the nearest
   entry -- effects bend them between notes, so an exact lookup would leave
   most rows blank. */
static const unsigned int periods[36] = {
    856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
    428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
    214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113
};
static const char notes[] = "C-C#D-D#E-F-F#G-G#A-A#B-";
static const char hexd[] = "0123456789abcdef";

/* cprintf costs thousands of cycles a call in cc65, and the status line and
   channel panel redraw about eight times a second -- far too often to pay for
   formatted output. Two digits, written directly -- and without dividing:
   cc65 does an 8-bit division through a ~500-cycle runtime call, and this
   runs eight times a row. */
static void put2(unsigned char v)
{
    unsigned char t = 0;
    while (v >= 10) { v -= 10; t++; }
    vt_put((char)('0' + t % 10));
    vt_put((char)('0' + v));
}

/* An unsigned int as "%u" prints it. */
static char nbuf[8];
static void putu(unsigned int v)
{
    utoa(v, nbuf, 10);
    vt_puts(nbuf);
}

/* ... left-aligned in w cells ("%-*u"). */
static void putl(unsigned int v, unsigned char w)
{
    utoa(v, nbuf, 10);
    vt_w = w;
    vt_pad(nbuf);
}

/* ... and right-aligned ("%*u"). */
static void putr(unsigned int v, unsigned char w)
{
    unsigned char len = 0;
    utoa(v, nbuf, 10);
    while (nbuf[len]) len++;
    if (len < w) { vt_w = (unsigned char)(w - len); vt_fill(' '); }
    vt_puts(nbuf);
}

/*
 * Nearest table entry. This used to scan all 36 entries with a 16-bit
 * compare and subtract each -- about 15,000 cycles per note, more than
 * everything else on a channel line put together. A binary search over the
 * descending table finds the first entry <= period; the nearest is that entry
 * or its predecessor, and a tie goes to the predecessor -- the lower index,
 * exactly what the scan chose.
 */
static void note_name(unsigned int period, char *out)
{
    unsigned char lo = 0, hi = 36, mid, best;
    if (period == 0) { out[0] = out[1] = out[2] = '-'; return; }
    while (lo < hi) {
        mid = (unsigned char)((lo + hi) >> 1);
        if (periods[mid] <= period) hi = mid; else lo = (unsigned char)(mid + 1);
    }
    if (lo == 0)       best = 0;
    else if (lo == 36) best = 35;
    else best = (periods[lo - 1] - period <= period - periods[lo])
                ? (unsigned char)(lo - 1) : lo;
    out[0] = notes[(best % 12) * 2];
    out[1] = notes[(best % 12) * 2 + 1];
    out[2] = (char)('1' + best / 12);
}

/*
 * The level meters.
 *
 * Two things were wrong with the old bars. They plotted the sequencer's
 * channel volume, which most modules leave at 64 for most of the music, so
 * they sat at full scale; and the colour was chosen from the bar's LENGTH, so
 * a loud channel turned entirely red instead of reddening at the top. Between
 * them the meter was four red blocks that rarely moved.
 *
 * Now: the real sample peak (src/vu.s), on a logarithmic scale, drawn in
 * fixed colour zones like a real meter, with a peak-hold marker that falls
 * back after a moment.
 */
#define VU_GREEN   24           /* cells 0..23  */
#define VU_AMBER   34           /* cells 24..33; 34..39 are red */
#define VU_HOLD    20           /* refreshes the peak marker sits still (~0.8 s) */
#define C_PEAK      1           /* white, so it reads against every zone */

extern unsigned char vu_peak[MOD_CHANNELS];     /* src/vu.s, 0..128 */
extern void vu_scan(void);

/*
 * Amplitude -> bar length, 8 cells per doubling: the 40-cell bar spans five
 * doublings, about 30 dB. A linear bar spends most of its travel in the top
 * few dB, which is the other half of why the old one looked pinned.
 */
static const unsigned char vu_curve[33] = {
     0,  5, 11, 14, 17, 20, 22, 23, 25, 26, 27, 28, 29, 30, 31, 32,
    32, 33, 34, 34, 35, 35, 36, 36, 37, 37, 38, 38, 39, 39, 39, 40, 40
};

static unsigned char bar_pk[MOD_CHANNELS];      /* marker cell, 0 = none */
static unsigned char bar_age[MOD_CHANNELS];

/* Paint cells [from, to) in their zone colours: up to three runs. */
static void bar_paint(unsigned char from, unsigned char to)
{
    unsigned char n;
    vt_rvs = VT_RVS;
    if (from < VU_GREEN) {
        n = (to < VU_GREEN ? to : VU_GREEN);
        vt_x = (unsigned char)(BAR_X + from);
        vt_col = 5;                     /* green */
        vt_w = (unsigned char)(n - from);
        vt_fill(' ');
        from = n;
    }
    if (from < to && from < VU_AMBER) {
        n = (to < VU_AMBER ? to : VU_AMBER);
        vt_x = (unsigned char)(BAR_X + from);
        vt_col = 7;                     /* yellow */
        vt_w = (unsigned char)(n - from);
        vt_fill(' ');
        from = n;
    }
    if (from < to) {
        vt_x = (unsigned char)(BAR_X + from);
        vt_col = C_HOT;
        vt_w = (unsigned char)(to - from);
        vt_fill(' ');
    }
    vt_rvs = 0;
}

/*
 * Only the cells that changed are touched. A full 40-cell repaint per channel
 * was affordable at the row rate (~8 Hz) but not at the 25 Hz a meter needs
 * to look like anything; between two refreshes the level usually moves a few
 * cells, so this normally writes a handful.
 */
static void draw_bar(unsigned char ch, unsigned char len)
{
    unsigned char old = barlen[ch], pk = bar_pk[ch];

    vt_y = (unsigned char)(CH_TOP + ch);

    if (old == 0xFF) {                  /* forced repaint: lay the trough down */
        vt_rvs = 0;
        vt_col = C_BORDER;
        vt_x = BAR_X;
        vt_w = BAR_W;
        vt_fill('.');
        old = 0;
        pk = 0;
    }

    if (len > old) {
        bar_paint(old, len);
    } else if (len < old) {
        vt_rvs = 0;
        vt_col = C_BORDER;
        vt_x = (unsigned char)(BAR_X + len);
        vt_w = (unsigned char)(old - len);
        vt_fill('.');
    }
    barlen[ch] = len;

    /* Peak hold: jumps up at once, sits for VU_HOLD refreshes, then falls a
       cell per refresh -- about 1.6 s from full scale to zero, which is the
       usual VU fallback. Falling a cell per VU_HOLD refreshes instead left
       markers stranded above channels that had gone quiet seconds earlier. */
    if (len >= pk) {
        pk = len;
        bar_age[ch] = VU_HOLD;
    } else if (bar_age[ch]) {
        bar_age[ch]--;
    } else if (pk) {
        pk--;
    }

    if (pk != bar_pk[ch] || len != old) {
        unsigned char oldpk = bar_pk[ch];
        /* Restore whatever the old marker was covering. */
        if (oldpk && oldpk != pk && oldpk > len) {
            vt_rvs = 0;
            vt_col = C_BORDER;
            vt_x = (unsigned char)(BAR_X + oldpk - 1);
            vt_w = 1;
            vt_fill('.');
        }
        if (pk > len && pk) {           /* only visible above the bar itself */
            vt_rvs = VT_RVS;
            vt_col = C_PEAK;
            vt_x = (unsigned char)(BAR_X + pk - 1);
            vt_w = 1;
            vt_fill(' ');
            vt_rvs = 0;
        }
    }
    bar_pk[ch] = pk;
}

/*
 * The channel panel: one line per channel, updated in place on every row --
 * about 140 cells, which is the whole cost of following the music. The note
 * and sample are sticky, the last ones triggered, rather than a blank on
 * every row that carries none; the effect column is the current row's.
 */
static char          ch_note[MOD_CHANNELS][3];
static char          ch_fx[MOD_CHANNELS][3];
static unsigned char ch_smp[MOD_CHANNELS], ch_vol[MOD_CHANNELS];

/*
 * Refresh all four meters from the mixer's cache pages. Driven on its own
 * cadence rather than on row changes: at speed 6 a row is 120 ms, and a meter
 * that updates eight times a second reads as a stutter rather than a level.
 */
static void draw_meters(void)
{
    unsigned char c, lvl;
    vu_scan();
    for (c = 0; c < MOD_CHANNELS; c++) {
        /* The page holds raw samples; the mixer applies volume through its
           table, so the audible level is the product of the two. */
        lvl = (unsigned char)(((unsigned int)vu_peak[c] * ch_vol[c]) >> 6);
        if (lvl > 128) lvl = 128;
        draw_bar(c, vu_curve[lvl >> 2]);
    }
}

static void reset_chan(unsigned char ch)
{
    ch_note[ch][0] = ch_note[ch][1] = ch_note[ch][2] = '-';
    ch_fx[ch][0] = ch_fx[ch][1] = ch_fx[ch][2] = '0';
    ch_smp[ch] = 0;
    ch_vol[ch] = 0;
}

/* Paint one line from its state: note, sample, volume, effect, level bar. */
/*
 * A digest of what is currently on screen for each channel, so an unchanged
 * line can be skipped. Most rows carry no new note, and at speed 4 the panel
 * redraws 12.5 times a second -- repainting identical text was the largest
 * avoidable cost in the per-row path, and what was starving the FIFO.
 *
 * Two bytes a channel rather than the eight fields themselves: fixed RAM has
 * no room to spare, and holding the fields outright pushed BSS past a page
 * boundary, which the page-aligned voice caches after it cannot absorb.
 */
static unsigned int  drawn_sig[MOD_CHANNELS];
static unsigned char drawn_valid[MOD_CHANNELS];

static unsigned int chan_sig(unsigned char ch)
{
    return (unsigned int)(ch_note[ch][0] + ch_note[ch][1] * 3u
                        + ch_note[ch][2] * 5u + ch_fx[ch][0] * 7u
                        + ch_fx[ch][1] * 11u + ch_fx[ch][2] * 13u
                        + ch_smp[ch] * 17u + ch_vol[ch] * 19u);
}

static void draw_chan(unsigned char ch)
{
    unsigned int sig = chan_sig(ch);

    if (drawn_valid[ch] && drawn_sig[ch] == sig)
        return;                         /* nothing about this line changed */
    drawn_valid[ch] = 1;
    drawn_sig[ch] = sig;

    vt_rvs = 0;
    vt_y = (unsigned char)(CH_TOP + ch);
    vt_x = NOTE_X; vt_col = C_NOTE; vt_w = 3; vt_write(ch_note[ch]);
    vt_x = SMP_X;  vt_col = C_SMP;
    if (ch_smp[ch]) put2(ch_smp[ch]); else vt_puts("--");
    vt_x = VOL_X;  vt_col = C_VALUE; put2(ch_vol[ch]);
    vt_x = FX_X;   vt_col = C_FX;    vt_w = 3; vt_write(ch_fx[ch]);
    /* The level bar is not drawn here: it follows the audio, not the row.
       See draw_meters(). */
}

/* Fold the current row into a channel's state. */
static void update_chan(unsigned char ch, unsigned char pat, unsigned char row)
{
    unsigned char note[4], smp;
    unsigned int period;

    mod_read_note(pat, row, ch, note);
    period = (unsigned int)((note[0] & 0x0F) << 8) | note[1];
    smp = (unsigned char)((note[2] >> 4) | (note[0] & 0x10));
    if (period) note_name(period, ch_note[ch]);
    if (smp) ch_smp[ch] = smp;
    ch_fx[ch][0] = hexd[note[2] & 0x0F];
    ch_fx[ch][1] = hexd[note[3] >> 4];
    ch_fx[ch][2] = hexd[note[3] & 15];
    ch_vol[ch] = (unsigned char)mod_chan_volume(ch);
}

static void draw_channels(int row)
{
    unsigned char pat = mod_seq_pattern(), c;
    if (shown_hl < 0) {                 /* new track: nothing has sounded yet */
        for (c = 0; c < MOD_CHANNELS; c++) {
            reset_chan(c);
            drawn_valid[c] = 0;
        }
    }
    for (c = 0; c < MOD_CHANNELS; c++) {
        pump();
        update_chan(c, pat, (unsigned char)row);
        draw_chan(c);
    }
    shown_hl = row;
}

static void draw_buttons(void)
{
    unsigned char i;
    textcolor(C_VALUE);
    for (i = 0; i < B_COUNT; i++) {
        pump();
        gui_draw_button(&buttons[i], 0);
    }
    vt_rvs = 0;
    vt_col = C_LABEL;
    vt_x = 39; vt_y = 1; vt_w = 3;
    putr(pcm_volume, 2);
    vt_put(' ');

    vt_rvs = VT_RVS;
    vt_col = shuffle ? C_TITLE : C_VALUE;
    vt_x = (unsigned char)(buttons[B_MODE].x + 1);
    vt_y = buttons[B_MODE].y;
    vt_w = 5;
    vt_pad(shuffle ? "rnd" : "seq");
    vt_rvs = 0;

    gotoxy(buttons[B_PLAY].x + 1, buttons[B_PLAY].y);
    revers(1);
    cputs(paused ? "play " : "pause");
    revers(0);
}

/* The static frame: rules, labels, help. conio, drawn once per directory. */
static void draw_frame(void)
{
    unsigned char i;

    revers(0);
    bgcolor(0);
    textcolor(C_VALUE);
    clrscr();
    pump();                 /* the frame is conio: ~20 ms of it in one go */

    textcolor(C_BORDER);
    gui_hline(0, 0, 79);
    gui_hline(0, 2, 79);
    gui_hline(0, 5, 79);
    gui_hline(0, 10, 79);
    gui_hline(0, 58, 79);
    for (i = PL_TOP; i < PL_TOP + PL_ROWS; i++) {
        gotoxy(PL_DIV, i);
        cputc('|');
    }
    pump();

    textcolor(C_LABEL);
    gotoxy(54, 1);
    cprintf("%uk ram    x16-mod ", (unsigned)ram_banks() * 8);
#ifdef BUILD_ID
    cputs(BUILD_ID);        /* so it is obvious which binary is running */
#endif

    gotoxy(POS_X - 4, ST_Y);
    cputs("pos");
    gotoxy(ROW_X - 4, ST_Y);
    cputs("row");
    gotoxy(SPD_X - 4, ST_Y);
    cputs("spd");
    gotoxy(BPM_X - 4, ST_Y);
    cputs("bpm");
    pump();

    for (i = 0; i < MOD_CHANNELS; i++) {
        textcolor(C_LABEL);
        gotoxy(1, (unsigned char)(CH_TOP + i));
        cprintf("ch%u", i + 1);
        gotoxy(VOL_X - 4, (unsigned char)(CH_TOP + i));
        cputs("vol");
        textcolor(C_BORDER);
        gotoxy(BAR_X - 1, (unsigned char)(CH_TOP + i));
        cputc('[');
        gotoxy(BAR_X + BAR_W, (unsigned char)(CH_TOP + i));
        cputc(']');
        reset_chan(i);
        barlen[i] = 0xFF;               /* so the empty bar is painted too */
        draw_chan(i);
    }
    pump();

    textcolor(C_LABEL);
    gotoxy(1, FILES_Y);
    cputs("files");
    gotoxy(1, HELP_Y);
    cputs("click to play  arrows move  L/R col  r shuffle  +/- vol  space pause  q quit");
    draw_buttons();
    shown_page = -1; shown_hl = -1; shown_pat = -1;
}

/*
 * The status lines are split by update rate. The row number and the clock
 * are cheap and frequent; the title, position, speed and tempo change at
 * most once a pattern -- redrawing them all on every row meant ten formatted
 * prints per row, which is what was starving the FIFO.
 */
static unsigned int last_secs = 0xFFFF;
static int last_pos = -1, last_spd = -1, last_bpm = -1;

static void draw_header_slow(void)
{
    vt_rvs = 0;
    vt_x = TITLE_X; vt_y = 3;
    vt_col = C_TITLE;
    vt_w = TITLE_W;                     /* pads or CUTS the title to fit */
    vt_pad(have_mod ? mod.title : "- no module loaded -");
    vt_x = INFO_X;
    vt_col = C_LABEL;
    if (have_mod) {
        vt_puts(mod.sig);
        vt_puts("  ");
        putu(mod.num_patterns);
        vt_puts(" pat   track ");
        /* Position among the MODULES, not the list index: folders sort first,
           so "cur_track + 1" announced the first module in a folder as 2. */
        putu(playlist_track_no(cur_track));
        vt_put('/');
        putu(playlist_file_count());
        vt_w = (unsigned char)(79 - vt_x);
        vt_fill(' ');                   /* whatever was there before */
    } else {
        vt_w = 79 - INFO_X;
        vt_pad("click a folder or module in the list");
    }

    vt_y = ST_Y;
    vt_col = C_VALUE;
    vt_x = POS_X;
    put2((unsigned char)mod_seq.order_pos);
    vt_put('/');
    put2(mod.song_length);
    vt_x = SPD_X;
    putl(mod_seq.speed, 2);
    vt_x = BPM_X;
    putl(mod_seq.bpm, 3);
    last_pos = mod_seq.order_pos;
    last_spd = mod_seq.speed;
    last_bpm = mod_seq.bpm;
}

/* The cheap part: row number, clock, underruns. */
static void draw_header(int row)
{
    unsigned int secs = mod_seconds;
    pump();

    if (mod_seq.order_pos != last_pos || mod_seq.speed != last_spd ||
        mod_seq.bpm != last_bpm)
        draw_header_slow();

    vt_rvs = 0;
    vt_col = C_VALUE;
    vt_y = ST_Y;
    vt_x = ROW_X;
    put2((unsigned char)row);

    if (secs != last_secs) {
        last_secs = secs;
        vt_x = CLK_X;
        put2((unsigned char)(secs / 60));
        vt_put(':');
        put2((unsigned char)(secs % 60));
        vt_col = mod_underruns ? C_HOT : C_LABEL;
        vt_x = UND_X;
        vt_puts("under ");
        putl(mod_underruns, 5);
    }
}

/* Show the page holding entry idx. Pages are whole screens: 92 entries. */
static void show_page_of(unsigned int idx)
{
    list_top = idx - idx % PL_PAGE;
}

/*
 * Called by load_track() once a module is loaded and playing. The name is
 * historical: this painted the sample list, which the two-column file list
 * has displaced, and it keeps the name because load_track() is not to be
 * edited right now. What a fresh track needs is the title line redrawn at
 * the next status update and the list paged to where the track is.
 */
static void draw_samples(void)
{
    last_pos = -1;
    show_page_of(cur_track);
}

/*
 * The file list: two columns of 46, column A filled first. Two independent
 * states, both visible at once:
 *
 *   keyboard cursor (idx == list_sel)   -> reverse video, on EVERY row type
 *   playing track (have_mod && cur_track) -> C_TITLE and a '>' marker
 *
 * The cursor used to be set and then reassigned on file rows from the
 * playing state, so it showed on folders and vanished on files -- the rows
 * one actually steps through with the arrow keys. Names are padded or CUT
 * to the column: a long name once ran past its panel and corrupted the
 * screen.
 */
static void draw_list(void)
{
    unsigned int i, idx, fileno, n = playlist_count();
    unsigned char playing, type;

    vt_rvs = 0;
    vt_col = C_LABEL;
    vt_x = 55; vt_y = FILES_Y;
    putr(n, 3);
    vt_puts(" entries");

    /*
     * Number FILES, not entries. ".." and any subdirectories are entries too,
     * so numbering by index made the first actual module read as 2. Count the
     * non-directory entries below the window once, then run the counter on
     * from there. This is one pass over the list per redraw -- on navigation,
     * never per audio frame.
     */
    fileno = 0;
    for (i = 0; i < list_top && i < n; i++) {
        if (playlist_type(i) != PL_DIR)
            fileno++;
    }

    for (i = 0; i < PL_PAGE; i++) {
        idx = list_top + i;
        mod_player_update();
        if (i < PL_ROWS) {
            vt_x = PL_XA;
            vt_y = (unsigned char)(PL_TOP + i);
        } else {
            vt_x = PL_XB;
            vt_y = (unsigned char)(PL_TOP + i - PL_ROWS);
        }
        vt_w = PL_COLW;
        vt_rvs = 0;
        vt_col = C_LABEL;
        if (idx >= n) { vt_fill(' '); continue; }
        vt_rvs = idx == list_sel ? VT_RVS : 0;
        if (idx == 0 && playlist_error()) {
            /* Say why the list is empty instead of showing a bare "..". */
            vt_col = C_HOT;
            vt_pad("dir read failed");
            continue;
        }
        type = playlist_type(idx);
        if (type == PL_DIR) {
            vt_col = C_NOTE;
            vt_puts(" dir ");
        } else if (type == PL_BIG) {
            fileno++;                       /* still a file: keep numbering contiguous */
            vt_col = C_BORDER;              /* dimmed: needs more RAM */
            vt_put('!');
            putr(fileno, 3);
            vt_put(' ');
        } else {
            fileno++;
            playing = (unsigned char)(have_mod && idx == cur_track);
            vt_col = playing ? C_TITLE : C_LABEL;
            vt_put(playing ? '>' : ' ');
            putr(fileno, 3);
            vt_put(' ');
        }
        vt_w = PL_NAME_W;
        vt_pad(playlist_name(idx));
    }
}

/*
 * Loading feedback.
 *
 * A large module takes about a second to come off the card, and until now the
 * screen simply stopped -- with no way to tell a slow read from a hang, which
 * is a bad thing for a program that HAS hung on real hardware before. The
 * loader calls load_tick() as it goes (see mod_load_tick in src/mod_x16.c);
 * this paints a bar over the title line and puts the title back afterwards.
 */
#define LOAD_W  22              /* the title field, which it borrows */

static void load_tick(void)
{
    unsigned char n = 0;
    if (mod_load_total)
        n = (unsigned char)((mod_load_done * LOAD_W) / mod_load_total);
    if (n > LOAD_W) n = LOAD_W;

    vt_y = 3;
    vt_x = TITLE_X;
    vt_rvs = VT_RVS;
    vt_col = C_FX;
    vt_w = n;
    vt_fill(' ');
    vt_rvs = 0;
    vt_col = C_BORDER;
    vt_w = (unsigned char)(LOAD_W - n);
    vt_fill('.');
}

/* Announce what is being loaded before the read starts, so a module that
   never finishes at least names itself. */
static void load_begin(const char *name)
{
    vt_rvs = 0;
    vt_y = 3;
    vt_x = TITLE_X;
    vt_col = C_LABEL;
    vt_w = LOAD_W;
    vt_pad(name);
    vt_x = INFO_X;
    vt_col = C_VALUE;
    vt_w = (unsigned char)(79 - INFO_X);
    vt_pad("loading");
}

/* And say so when one fails, rather than silently skipping to the next. */
static void load_failed(mod_err_t e)
{
    vt_rvs = 0;
    vt_y = 3;
    vt_x = INFO_X;
    vt_col = C_HOT;
    vt_w = (unsigned char)(79 - INFO_X);
    vt_pad(mod_strerror(e));
}

static unsigned char load_track(unsigned int idx)
{
    mod_player_stop();
    have_mod = 0;
    if (idx >= playlist_count()) return 0;
    if (playlist_type(idx) != PL_FILE) return 0;    /* dir, or known too big */
    cur_track = idx;
    {
        mod_err_t e;
        load_begin(playlist_name(idx));
        mod_load_tick = load_tick;
        e = mod_load_file(playlist_name(idx), &mod, hdrbuf);
        mod_load_tick = 0;
#ifdef VERIFY_ORDER
        /* Test hook (tools/gate_vmp_mouse.sh): each module tried, "+name"
           (it plays) or "-name" (it did not load), through the emulator's
           stdout port, so the Veranda player's order can be compared with
           this one's. Never defined for a release build. */
        {
            const char *p;
            *(unsigned char *)0x9FBB = e == MOD_OK ? 0x2B : 0x2D;
            for (p = playlist_name(idx); *p; ++p) *(unsigned char *)0x9FBB = *p;
            *(unsigned char *)0x9FBB = 10;
        }
#endif
        if (e != MOD_OK) {
            /* Out of banked RAM is a property of the machine, not a transient
               failure: remember it so the entry is shown as unplayable and
               never retried. */
            if (e == MOD_ERR_NOMEM)
                playlist_mark(idx, PL_BIG);
            load_failed(e);
            return 0;
        }
    }
    playlist_played_set(idx);       /* heard, however it was chosen */
    cache_init();
    mod_hooks_init(&mod);
    mod_seq_init(&mod);
    mod_frames_rendered = 0;
    mod_seconds = 0;
    have_mod = 1;
    paused = 0;
    shown_page = -1; shown_hl = -1; shown_pat = -1;
    mod_player_start();
    draw_samples();
    return 1;
}

/*
 * Step to the next playable entry, wrapping. Directories and files that fail
 * to load are skipped, so a bad module or a folder cannot stall the list --
 * and if the directory holds no modules at all, this simply gives up and
 * leaves the player idle rather than looping forever.
 */
/*
 * Pick a random playable entry, avoiding the one already playing so shuffle
 * never repeats a track back-to-back. Falls back to sequential if it cannot
 * find one -- a folder with a single module would otherwise spin here.
 */
/* Unheard, playable entries remaining in this folder. */
static unsigned int bag_left(unsigned int first, unsigned int nfiles)
{
    unsigned int i, left = 0;
    for (i = 0; i < nfiles; i++)
        if (!playlist_played(first + i) && playlist_type(first + i) == PL_FILE)
            left++;
    return left;
}

static unsigned int pick_random(void)
{
    unsigned int first  = playlist_first_file();
    unsigned int nfiles = playlist_file_count();
    unsigned int left, k, i;

    if (!nfiles)
        return cur_track;

    /* Stir in the elapsed frame count so the order differs run to run. */
    rnd_state ^= mod_frames_rendered;
    if (rnd_state == 0)
        rnd_state = 0xACE1;

    left = bag_left(first, nfiles);
    if (!left) {
        /* Everything has been heard: start the folder over, but hold back the
           track that just finished so the new pass cannot open with it. */
        playlist_played_clear();
        if (have_mod)
            playlist_played_set(cur_track);
        left = bag_left(first, nfiles);
        if (!left)
            return cur_track;           /* a folder with a single module */
    }

    /* Take the k-th unheard module rather than retrying until a pick lands on
       one. Drawing from the whole list meant a folder of mostly subfolders
       could burn every attempt on directories and fall back to replaying the
       current track. */
    k = rnd_next() % left;
    for (i = 0; i < nfiles; i++) {
        if (playlist_played(first + i) || playlist_type(first + i) != PL_FILE)
            continue;
        if (k == 0)
            return first + i;
        k--;
    }
    return cur_track;
}

/*
 * Advance to the next track. Shuffle applies to going forward only -- "prev"
 * stays sequential, because a random "back" is not what anyone means by it.
 */
static void step_track(int dir)
{
    unsigned int n = playlist_count();
    unsigned int tries = n, idx = cur_track;

    if (!n) return;

    if (shuffle && dir > 0) {
        idx = pick_random();
        if (load_track(idx)) {
            draw_list();
            draw_buttons();
            return;
        }
        /* random pick failed to load: fall through to a sequential search */
    }

    while (tries--) {
        idx = (unsigned int)(idx + dir + n) % n;
        if (load_track(idx)) break;
    }
    draw_list();
    draw_buttons();
}

/*
 * Play with nothing loaded starts the folder: the first playable entry --
 * not a directory, not marked too big, skipping any that fail to load.
 * main() then carries on through the rest of it on song_end via step_track,
 * which skips the same things and wraps, so a folder plays in order and
 * loops. Nothing starts by itself at launch or on entering a directory: the
 * player comes up showing what it found and waits to be asked.
 */
static void play_first(void)
{
    unsigned int i, n = playlist_count();
    for (i = 0; i < n; i++)
        if (playlist_type(i) == PL_FILE && load_track(i)) break;
    draw_list();
    draw_buttons();
}

/* Enter a directory and show what is in it. */
/*
 * Where the cursor sat in each folder on the way down, so that stepping back
 * out with ".." lands on the folder you just came from instead of at the top
 * of the list -- which meant scrolling back down to it every time.
 *
 * Positions rather than names: the parent's listing is rebuilt by the same
 * scan and the same sort, so the position is reproducible, and it costs two
 * bytes a level instead of a 64-byte name. nav_deep counts levels below what
 * the stack can hold, so descending very deep and climbing back out pops in
 * step rather than returning stale positions.
 */
#define NAV_DEPTH 12
static unsigned int  nav_sel[NAV_DEPTH];
static unsigned char nav_n, nav_deep;

/* Raw bytes: cc65 would translate a ".." literal to PETSCII. */
static unsigned char is_updir(const char *n)
{
    return (unsigned char)(n[0] == 0x2E && n[1] == 0x2E && n[2] == 0);
}

static void enter_dir(unsigned int idx)
{
    unsigned char up = is_updir(playlist_name(idx));
    unsigned int back = 0;

    mod_player_stop();
    have_mod = 0;

    if (up) {
        if (nav_deep)      nav_deep--;
        else if (nav_n)    back = nav_sel[--nav_n];
    } else {
        if (nav_n < NAV_DEPTH) nav_sel[nav_n++] = idx;
        else                   nav_deep++;
    }

    playlist_chdir(playlist_name(idx));
    cur_track = 0;
    /* The folder may have changed on disk since we were last here. */
    if (back >= playlist_count())
        back = 0;
    list_sel = back;
    show_page_of(list_sel);
    draw_frame();
    draw_samples();
    draw_list();
    draw_header(0);
}

static void activate(unsigned int idx)
{
    if (idx >= playlist_count())
        return;
    if (playlist_is_dir(idx)) {
        enter_dir(idx);
    } else {
        load_track(idx);
        draw_list();
        draw_buttons();
    }
}

/*
 * Move the cursor, paging by whole screens. The directory scan is bounded
 * (playlist.h) and extends on demand, so stepping past what is loaded reads
 * more of the directory first.
 */
static void move_sel(int delta)
{
    unsigned int n = playlist_count();
    if (delta > 0) {
        if (list_sel + delta >= n && playlist_truncated()) {
            playlist_more();
            n = playlist_count();
        }
        if (!n) return;
        list_sel = list_sel + delta < n ? list_sel + delta : n - 1;
    } else {
        list_sel = list_sel >= (unsigned int)(-delta) ? list_sel + delta : 0;
    }
    show_page_of(list_sel);
    draw_list();
}

static void do_click(unsigned char cx, unsigned char cy)
{
    int b = gui_hit(buttons, B_COUNT, cx, cy);
    unsigned int idx;

    switch (b) {
    case B_PREV: step_track(-1); return;
    case B_NEXT: step_track(1);  return;
    case B_PLAY:
        if (!have_mod) { play_first(); return; }
        paused = !paused;
        if (paused) mod_player_stop(); else mod_player_start();
        draw_buttons();
        return;
    case B_VDN:
        if (pcm_volume) audio_set_volume((unsigned char)(pcm_volume - 1));
        draw_buttons();
        return;
    case B_VUP:
        if (pcm_volume < 15) audio_set_volume((unsigned char)(pcm_volume + 1));
        draw_buttons();
        return;
    case B_MODE:
        shuffle = !shuffle;
        draw_buttons();
        return;
    case B_UP:   move_sel(-PL_PAGE); return;
    case B_DOWN: move_sel(PL_PAGE);  return;
    default: break;
    }

    /* a click in either list column enters a directory or plays a module */
    if (cy >= PL_TOP && cy < PL_TOP + PL_ROWS) {
        if (cx >= PL_XA && cx < PL_XA + PL_COLW)
            idx = list_top + (cy - PL_TOP);
        else if (cx >= PL_XB && cx < PL_XB + PL_COLW)
            idx = list_top + PL_ROWS + (cy - PL_TOP);
        else
            return;
        if (idx < playlist_count()) {
            list_sel = idx;
            activate(idx);
        }
    }
}

int main(void)
{
    unsigned char k;
    int row = -1;
    unsigned int n;

    clrscr();
    cputs("scanning directory...\r\n");
    /* Size the file list to the machine before the first scan: a fixed
       2048-entry list would cost a quarter of a 512 KB machine's RAM. */
    playlist_init(ram_banks());
    n = playlist_scan();
    (void)n;

    /* An empty directory is not an error: come up anyway so the user can
       browse to wherever the modules actually are. */
    audio_set_volume(15);       /* BSS starts at zero; audio_init has not run */
    mouse_init();
    /* Test hook: playback cannot be started without a click in a headless
       emulator, so -DAUTOPLAY exercises the channel panel in an automated
       run. The cursor follows the loaded track, as it does when a track is
       chosen by mouse or keyboard. Never defined for a release build. */
#ifdef AUTOPLAY
    { unsigned int t; for (t = 0; t < playlist_count(); t++)
        if (!playlist_is_dir(t) && load_track(t)) { list_sel = t; break; } }
#endif
    /* Deliberately does NOT start playing. The player comes up showing what
       it found and waits: launching straight into whichever module happens to
       sort first is rarely what was wanted. */
    /* Test hook: mouse clicks cannot be injected into a headless emulator, so
       -DVERIFY_CHDIR lets the directory-navigation path be exercised in an
       automated run. Never defined for a release build. */
#ifdef VERIFY_SHUFFLE
    /* Test hook: draw twice as many tracks as the folder holds, marking each
       as heard the way load_track() would, and print the sequence through the
       emulator's stdout port. A correct bag emits every module once before it
       emits any of them twice. Never defined for a release build. */
    {
        unsigned int i, pick, nf = playlist_file_count();
        signed char sh;
        for (i = 0; i < nf * 2u; i++) {
            pick = pick_random();
            playlist_played_set(pick);
            cur_track = pick;
            have_mod = 1;
            for (sh = 12; sh >= 0; sh -= 4) {
                unsigned char d = (unsigned char)((pick >> sh) & 15);
                *(unsigned char *)0x9FBB = d < 10 ? 48 + d : 97 + d - 10;
            }
            *(unsigned char *)0x9FBB = 10;
        }
        *(unsigned char *)0x9FBB = 'd'; *(unsigned char *)0x9FBB = 'o';
        *(unsigned char *)0x9FBB = 'n'; *(unsigned char *)0x9FBB = 'e';
        *(unsigned char *)0x9FBB = 10;
    }
#endif
#ifdef VERIFY_NAV
    /* Test hook: descend into the first folder, then step straight back out,
       so the "cursor returns to the folder you came from" path can be checked
       without a mouse. Never defined for a release build. */
    {
        unsigned int d;
        for (d = 0; d < playlist_count(); d++)
            if (playlist_is_dir(d) && !is_updir(playlist_name(d))) {
                enter_dir(d);
                break;
            }
        for (d = 0; d < playlist_count(); d++)
            if (playlist_is_dir(d) && is_updir(playlist_name(d))) {
                enter_dir(d);
                break;
            }
    }
#endif
#ifdef VERIFY_CHDIR
    if (!have_mod) {
        unsigned int d;
        for (d = 1; d < playlist_count(); d++)
            if (playlist_is_dir(d)) { enter_dir(d); break; }
    }
#endif
    draw_frame();       /* clears the screen, so panels are drawn after it */
    draw_samples();
    draw_list();
    draw_header(0);     /* idle screen: nothing else would draw it */

    for (;;) {
        pump();

        /*
         * Input is serviced FIRST and unconditionally. It used to sit after a
         * song-end branch that did "continue", so while that branch was live
         * the keyboard was never read at all -- the UI looked frozen and q
         * did nothing.
         */
        if (kbhit()) {
            k = cgetc();
            if (k == 'q' || k == 'Q') break;
            if (k == 'n' || k == 'N') { step_track(1);  row = -1; }
            if (k == 'r' || k == 'R') { shuffle = !shuffle; draw_buttons(); }
            if ((k == '+' || k == '=') && pcm_volume < 15) {
                audio_set_volume((unsigned char)(pcm_volume + 1)); draw_buttons();
            }
            if ((k == '-' || k == '_') && pcm_volume) {
                audio_set_volume((unsigned char)(pcm_volume - 1)); draw_buttons();
            }
            if (k == 'p' || k == 'P') { step_track(-1); row = -1; }
            if (k == ' ') do_click(buttons[B_PLAY].x, buttons[B_PLAY].y);
            if (k == 17)  move_sel(1);                          /* cursor down */
            if (k == 145) move_sel(-1);                         /* cursor up */
            /* Left/right jump a whole column. The list is two columns of
               PL_ROWS, so +PL_ROWS lands on the same row of the next column
               and then on the next page -- a way through a long directory
               that does not mean holding "down" past every file. */
            if (k == 29)  move_sel(PL_ROWS);                    /* cursor right */
            if (k == 157) move_sel(-PL_ROWS);                   /* cursor left */
            if (k == 13) { activate(list_sel); row = -1; }      /* enter */
        }

        pump();
        if (pend_click) {
            pend_click = 0;
            do_click(pend_cx, pend_cy);
        }

#ifdef NO_UI
        /* Report the underrun count through the emulator's debug port once a
           second: the on-screen counter is not drawn in this mode, so it
           would otherwise show whatever was painted at startup. */
        {
            static unsigned int lastsec = 0xFFFF;
            unsigned int sc = mod_frames_rendered / 1001;
            if (sc != lastsec && sc && (sc % 10) == 0) {
                lastsec = sc;
                *(volatile unsigned char *)0x9FBB = 'u';
                *(volatile unsigned char *)0x9FBB =
                    (char)('0' + (mod_underruns / 10) % 10);
                *(volatile unsigned char *)0x9FBB =
                    (char)('0' + mod_underruns % 10);
                *(volatile unsigned char *)0x9FBB = '\n';
            }
        }
        if (0) {
#else
        if (have_mod && !paused && mod_seq.row != row) {
#endif
            row = mod_seq.row;
            draw_header(row);
            draw_channels(row);
        }

        /*
         * Meters run on the clock, not on rows. mod_frames_rendered counts
         * 16-frame units, so it advances about 1001 times a second: every
         * 40 units is 25 Hz, fast enough for the bars to read as levels
         * rather than as a slideshow of whatever the last row did.
         */
        if (have_mod && !paused) {
            static unsigned int last_vu;
            unsigned int now = mod_frames_rendered;
            if ((unsigned int)(now - last_vu) >= 40) {
                last_vu = now;
                draw_meters();
                pump();
            }
        }

        /*
         * A backward order jump ends the song. The flag MUST be cleared here:
         * the sequencer only resets it on its next tick, and this loop spins
         * far faster than that, so leaving it set made every pass load another
         * track -- a track-change storm that looked like a hang.
         */
        if (have_mod && !paused && mod_seq.song_end) {
            mod_seq.song_end = 0;
            step_track(1);
            row = -1;
        }
    }

    mod_player_stop();
    clrscr();
    return 0;
}
