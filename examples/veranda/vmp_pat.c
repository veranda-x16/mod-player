/*
 * vmp_pat.c -- the Veranda MOD Player's overlay 2 (vmp.h): the pattern
 * view, the window's frame (and vmp_len.c: the song's length). Called through call2(), which maps this
 * overlay's bank and puts the caller's back.
 *
 * It runs in place in its RAM bank. It reads a pattern row through
 * mod_read_row, which is in main RAM: that maps the pattern's bank over
 * the overlay, copies the row into the caller's (fixed RAM: the C stack)
 * buffer and maps the overlay back before it returns. The AFLOW batch and
 * Veranda's interrupt put the bank back too. Strings for the kernel are
 * built in main RAM (sb, text()'s buffer).
 *
 * MIT licence (see LICENSE).
 */
#include "vmp.h"
#include "vmp_pics.h"

#pragma code-name ("OVERLAY2")
#pragma rodata-name ("OVL2RO")

/*---------------------------------------------------------------
  text in cards: the ROM's 8 x 8 font (vmodplay.c's glyphs), PutCards
 ---------------------------------------------------------------*/
#define GLYPHS ((void *)(glyphs - 256))
static char tb[81];
/* s at card column x, row y (pixels), padded with spaces to w cards;
   flags: 1 reversed, 2 bold */
void text(unsigned char x, unsigned int y, const char *s, unsigned char w, unsigned char flags)
{
    unsigned char i = 0;
    while (s[i] && i < w) {
        tb[i] = (s[i] >= 32 && s[i] < 127) ? s[i] : '?';
        ++i;
    }
    while (i < w)
        tb[i++] = ' ';
    PutCards(tb, GLYPHS, x, w, y, flags);
}

/*---------------------------------------------------------------
  the pattern view: 32 rows a page in the left panel, the current one a
  band of colour (the colour map)
 ---------------------------------------------------------------*/
static const char notes[] = "C-C#D-D#E-F-F#G-G#A-A#B-";
static const char hexd[] = "0123456789ABCDEF";
/* ProTracker's periods (finetune 0), octaves 1-3, falling */
static const unsigned int periods[36] = {
    856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
    428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
    214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113 };
static unsigned char pv_pattern = 0xFF, pv_page, pv_row = 0xFF;
unsigned char pv_force;

/* a cell (4 bytes) as "C-3" (the nearest note, within 1/32 of the
   period: finetuned samples), the instrument, the effect */
static void note_text(char *o, const unsigned char *n)
{
    unsigned int per = ((unsigned int)(n[0] & 15) << 8) | n[1], d;
    unsigned char ins = (n[0] & 0xF0) | (n[2] >> 4), k;
    o[0] = o[1] = o[2] = '-';
    if (per) {
        for (k = 0; k < 35 && periods[k] > per; ++k)
            ;
        /* periods[k] <= per (or the last): k or k - 1, the nearer */
        d = per >= periods[k] ? per - periods[k] : periods[k] - per;
        if (k && periods[k - 1] - per < d) {
            --k;
            d = periods[k] - per;
        }
        if (d <= (per >> 5)) {
            o[0] = notes[2 * (k % 12)];
            o[1] = notes[2 * (k % 12) + 1];
            o[2] = (char)('1' + k / 12);
        }
    }
    o[3] = ins ? hexd[ins >> 4] : '.';
    o[4] = ins ? hexd[ins & 15] : '.';
    o[5] = hexd[n[2] & 15];
    o[6] = hexd[n[3] >> 4];
    o[7] = hexd[n[3] & 15];
    o[8] = ' ';
}

/* the panel's head: "Pattern 8, rows 00-1F" */
void pv_header(void)
{
    unsigned char c;
    SetPattern(0);
    WRectangle(LIST_X + 4, 49, LP_R - 1, 61);
    currentMode = SET_BOLD;
    strcpy(sb, "Pattern ");
    if (loaded) {
        strcat(sb, num(pv_pattern == 0xFF ? mod_status.pattern : pv_pattern, 3));
        for (c = 8; sb[c] == '0' && sb[c + 1]; ++c)
            ;
        strcpy(sb + 8, sb + c);
    }
    WPutString(sb, LIST_X + 6, 59);
    currentMode = 0;
    if (loaded) {
        strcpy(sb, pv_page ? "rows 32-63" : "rows 0-31");
        WPutString(sb, LIST_X + 120, 59);
    }
    /* the channels' heads, over their columns (bold cards) */
    strcpy(sb, "Rw Ch 1     Ch 2     Ch 3     Ch 4    ");
    text(LIST_COL, PAT_ROW0 * 8 - 8, sb, 39, 2);
}

/* the page's rows from pv_todo, 8 of them: a page is drawn over four
   passes of the main loop, so the meters and fields go on meanwhile */
static unsigned char pv_todo = PAT_ROWS;

static void pv_draw_rows(void)
{
    unsigned char r, c, n = 8;
    unsigned char row16[16];
    char line[40];
    for (r = pv_todo; n && r < PAT_ROWS; ++r, --n) {
        unsigned char row = (unsigned char)(pv_page * PAT_ROWS + r);
        mod_read_row(&mod, pv_pattern, row, row16);
        line[0] = hexd[row >> 4];
        line[1] = hexd[row & 15];
        line[2] = ' ';
        for (c = 0; c < 4; ++c)
            note_text(line + 3 + 9 * c, row16 + 4 * c);
        line[39] = 0;
        text(LIST_COL, (PAT_ROW0 + r) * 8, line, 39, 0);
    }
    pv_todo = r;
}

void pv_update(void)
{
    unsigned char pat = mod_status.pattern, row = mod_status.row, page = row / PAT_ROWS;
    if (pv_force) {
        pv_force = 0;
        if (pv_row != 0xFF)
            ColorRect(LIST_COL, 39, PAT_ROW0 + (pv_row % PAT_ROWS), 1, 0);
        pv_row = 0xFF;
        pv_pattern = 0xFF;
        if (!loaded) {
            pv_page = 0;
            pv_header();
            SetPattern(0);
            WRectangle(LIST_X, PAT_ROW0 * 8, LIST_XR, PAT_ROW0 * 8 + PAT_ROWS * 8 - 1);
            strcpy(sb, "Nothing is playing.");
            WPutString(sb, LIST_X + 8, PAT_ROW0 * 8 + 16);
            return;
        }
    }
    if (!loaded)
        return;
    if (pat != pv_pattern || page != pv_page) {
        if (pv_row != 0xFF)
            ColorRect(LIST_COL, 39, PAT_ROW0 + (pv_row % PAT_ROWS), 1, 0);
        pv_pattern = pat;
        pv_page = page;
        pv_header();
        pv_todo = 0;
        pv_row = 0xFF;
    }
    if (pv_todo < PAT_ROWS)
        pv_draw_rows();
    if (row != pv_row) {
        if (pv_row != 0xFF)
            ColorRect(LIST_COL, 39, PAT_ROW0 + (pv_row % PAT_ROWS), 1, 0);
        ColorRect(LIST_COL, 39, PAT_ROW0 + (row % PAT_ROWS), 1, C_ROW);
        pv_row = row;
    }
}

/* the pattern view put away (the playlist comes back): its row's colour */
void pv_off(void)
{
    if (pv_row != 0xFF)
        ColorRect(LIST_COL, 39, PAT_ROW0 + (pv_row % PAT_ROWS), 1, 0);
    pv_row = 0xFF;
    pv_pattern = 0xFF;
}

/*---------------------------------------------------------------
  the VU meters (vmp_ui.c's update, through call2), in the colour map: the segments' paper green (to -12
  dB), amber (to -4 dB), red, unlit dark grey; only the segments that
  change
 ---------------------------------------------------------------*/
unsigned char vu_n[4], vu_hold[4], vu_holdt[4];
static unsigned char zone(unsigned char i) { return i < 54 ? C_GREEN : i < 66 ? C_YELLOW : C_RED; }

static void vu_span(unsigned char bar, unsigned char from, unsigned char to, unsigned char on)
{
    unsigned char a, b, z;
    while (from < to) {
        z = zone(from);
        a = from;
        b = from;
        while (b < to && zone(b) == z)
            ++b;
        ColorRect(VU_COL + a, b - a, VU_ROW0 + 3 * bar, 2, on ? z : C_VU_OFF);
        from = b;
    }
}

/* x (peak 0-128 x volume 0-64, so 0-8192) in segments, by its log: 9
   segments a bit (6 dB: two thirds of a dB each), 8192 the last, 64
   (-42 dB) the first */
static unsigned char level(unsigned int x)
{
    unsigned char b = 12, f;
    if (x < 64)
        return 0;
    if (x >= 8192)
        return VU_W;
    while (!(x & 0x1000)) {
        x <<= 1;
        --b;
    }
    f = (unsigned char)((x >> 9) & 7);             /* the next three bits: 1 each */
    return (unsigned char)(9 * (b - 5) + f);
}

void vu_update(void)
{
    unsigned char k, n, h;
    for (k = 0; k < 4; ++k) {
        n = 0;
        if (loaded && !paused)
            n = level((unsigned int)mod_status.peak[k] * mod_status.vol[k]);
        h = vu_hold[k];
        if (n != vu_n[k]) {
            if (n > vu_n[k])
                vu_span(k, vu_n[k], n, 1);
            else
                vu_span(k, n, vu_n[k], 0);
            vu_n[k] = n;
        }
        /* the peak: held 12 updates, then it falls a segment an update */
        if (n >= h) {
            h = n;
            vu_holdt[k] = 12;
        } else if (vu_holdt[k])
            --vu_holdt[k];
        else
            --h;
        if (h != vu_hold[k]) {
            if (vu_hold[k] > vu_n[k] && vu_hold[k])
                ColorRect(VU_COL + vu_hold[k] - 1, 1, VU_ROW0 + 3 * k, 2, C_VU_OFF);
            if (h > n && h)
                ColorRect(VU_COL + h - 1, 1, VU_ROW0 + 3 * k, 2, zone(h - 1));
            vu_hold[k] = h;
        }
    }
}

/*---------------------------------------------------------------
  the window's frame: the desk, the window (Veranda Look 2's chrome:
  vrwin.h, DECISIONS V79; the SDK's vrwin.c is in this overlay), the
  panels, the meters' segments. Look 2's palette (theme_palette: light or
  dark, V74) for every colour; the menu bar's cards are the kernel's
  (DoMenu colours them after this: nothing here covers them)
 ---------------------------------------------------------------*/
void frame(void)
{
    unsigned char k;
    theme_load();
    theme_palette();
    hilite = theme.hilite ? theme.hilite : SELECT;
    dispBufferOn = ST_WR_FORE;
    ColorRect(0, 80, 2, 58, theme.desk);        /* the colour map on: the desk's colour */
    SetPattern(theme.desk ? 0 : 2);             /* (none: GEOS's grey) */
    WRectangle(0, 16, 639, 479);
    vr_win(&win, "MOD Player");
    /* the panels */
    WFrameRectangle(0xFF, LP_L, LP_T, LP_R, LP_B);
    ColorRect(2, 40, 6, 2, C_BAR);
    WHorizontalLine(0xFF, LP_L, LP_R, 63);
    WFrameRectangle(0xFF, RP_L, RP_T, RP_R, RP_B);
    ColorRect(43, 35, 6, 2, C_BAR);
    WHorizontalLine(0xFF, RP_L, RP_R, 63);
    currentMode = SET_BOLD;
    strcpy(sb, "Now playing");
    WPutString(sb, RP_X, 59);
    currentMode = 0;
    WFrameRectangle(0xFF, VP_L, VP_T, VP_R, VP_B);
    /* the meters: unlit (chrome) segments, an ink line at each one's left */
    memset(sb, 0x80, VU_W);
    for (k = 0; k < 4; ++k) {
        unsigned int y = (VU_ROW0 + 3 * k) * 8, r;
        ColorRect(VU_COL, VU_W, VU_ROW0 + 3 * k, 2, C_VU_OFF);
        WFrameRectangle(0xFF, VU_COL * 8 - 1, y - 1, (VU_COL + VU_W) * 8, y + 16);
        for (r = 0; r < 16; ++r)
            WriteRow(sb, VU_COL, VU_W, y + r);
    }
    for (k = 0; k < 4; ++k) {
        sb[0] = (char)('1' + k);
        sb[1] = 0;
        WPutString(sb, 24, (VU_ROW0 + 3 * k) * 8 + 11);
    }
}

