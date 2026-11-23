/*
 * vmp_ui.c -- the Veranda MOD Player's overlay 3 (vmp.h): the window and
 * everything in it, the menus' routines, the keys and the pointer. Mapped
 * at $A000 for good as appBank, so Veranda's calls to the program (the
 * vectors, the menus, the icons) find it; its strings and tables stay in
 * main RAM (rodata, data and bss are main RAM's), where the kernel reads
 * them.
 *
 * Drawing is on the foreground only (dispBufferOn = ST_WR_FORE): menus and
 * dialog boxes keep what they cover on the save-under stack. While a menu
 * is open nothing is drawn in the bitmap (it would draw over the menu, and
 * the menu would put the old picture back): the fields catch up after it;
 * the colour map (ColorRect: the meters, the progress bar, the pattern's
 * row) is drawn on, Veranda keeps a covered card's colour (V33).
 *
 * MIT licence (see LICENSE).
 */
#include "vmp.h"
#include "vmp_pics.h"

#pragma code-name ("OVERLAY3")

unsigned char view_pattern;

/*---------------------------------------------------------------
  the menus
 ---------------------------------------------------------------*/
struct item { const char *text; unsigned char type; void *p; };
struct menu2 { unsigned char top, bottom; unsigned int left, right; unsigned char flags; struct item i[2]; };
struct menu3 { unsigned char top, bottom; unsigned int left, right; unsigned char flags; struct item i[3]; };
struct menu6 { unsigned char top, bottom; unsigned int left, right; unsigned char flags; struct item i[6]; };
struct menu7 { unsigned char top, bottom; unsigned int left, right; unsigned char flags; struct item i[7]; };
struct bar4 { unsigned char top, bottom; unsigned int left, right; unsigned char flags; struct item i[4]; };

static void m_about(void), m_da0(void), m_da1(void), m_da2(void), m_da3(void), m_da4(void), m_da5(void);
static void m_open(void), m_playall(void), m_quit(void);
static void m_play(void), m_stop(void), m_next(void), m_prev(void), m_repeat(void), m_shuffle(void);
static void m_pattern(void), m_list(void);

/* Veranda Look 2's menus (DECISIONS V74, V79): the kernel draws the bar
   (theme_menus), the first menu named after the program; the lists open
   under their bar items, 16 lines an item, in the UI font */
#define IH 16
static const char t_repeat0[] = "Repeat: off", t_repeat1[] = "Repeat: on";
static const char t_shuffle0[] = "Shuffle: off", t_shuffle1[] = "Shuffle: on";
static const char t_pat0[] = "  Pattern view", t_pat1[] = "* Pattern view";
static const char t_list0[] = "  Playlist", t_list1[] = "* Playlist";
/* (the tables in main RAM, where the kernel reads them; their widths
   and the bar's end: vmp_dlg.c's menu_layout, overlay 4) */
struct menu7 mgeos = { 16, 16 + IH, 0, 0, VERTICAL | 1, {
    { "About MOD Player", MENU_ACTION, m_about },
    { da_names[0], MENU_ACTION, m_da0 }, { da_names[1], MENU_ACTION, m_da1 },
    { da_names[2], MENU_ACTION, m_da2 }, { da_names[3], MENU_ACTION, m_da3 },
    { da_names[4], MENU_ACTION, m_da4 }, { da_names[5], MENU_ACTION, m_da5 } } };
struct menu3 mfile = { 16, 16 + 3 * IH, 0, 0, VERTICAL | 3, {
    { "Open...", MENU_ACTION, m_open }, { "Play all", MENU_ACTION, m_playall },
    { "Quit", MENU_ACTION, m_quit } } };
struct menu6 mplay = { 16, 16 + 6 * IH, 0, 0, VERTICAL | 6, {
    { "Play/Pause", MENU_ACTION, m_play }, { "Stop", MENU_ACTION, m_stop },
    { "Next", MENU_ACTION, m_next }, { "Previous", MENU_ACTION, m_prev },
    { t_repeat0, MENU_ACTION, m_repeat }, { t_shuffle0, MENU_ACTION, m_shuffle } } };
struct menu2 mview = { 16, 16 + 2 * IH, 0, 0, VERTICAL | 2, {
    { t_pat0, MENU_ACTION, m_pattern }, { t_list1, MENU_ACTION, m_list } } };
struct bar4 mbar = { 0, 14, 0, 0, HORIZONTAL | UN_CONSTRAINED | 4, {
    { "MOD Player", SUB_MENU, &mgeos }, { "File", SUB_MENU, &mfile },
    { "Play", SUB_MENU, &mplay }, { "View", SUB_MENU, &mview } } };
static void menu_marks(void)
{
    mplay.i[4].text = repeat ? t_repeat1 : t_repeat0;
    mplay.i[5].text = shuffle ? t_shuffle1 : t_shuffle0;
    mview.i[0].text = view_pattern ? t_pat1 : t_pat0;
    mview.i[1].text = view_pattern ? t_list0 : t_list1;
}

static void menus_off(void) { GotoFirstMenu(); }

/*---------------------------------------------------------------
  the window's title band and status bar (vrwin.h, through main RAM)
 ---------------------------------------------------------------*/
static void title(void)
{
    const char *t = "MOD Player";
    unsigned char i;
    char s[24];
    if (loaded) {
        memcpy(s, mod.title, 20);
        s[20] = 0;
        for (i = 20; i && (s[i - 1] == ' ' || !s[i - 1]); --i)
            s[i - 1] = 0;
        t = s[0] ? s : playing_name;
    }
    currentMode = SET_BOLD;
    fit(t, 480);
    currentMode = 0;
    LOG("TITLE", sb);
    win_title(sb);
}

/* the status bar: the state at its left, the modes at its right
   (vr_win_status draws both, the band cleared); a new state redraws the
   left part alone, as vrwin.c draws it (the UI font; only its area
   cleared: the UI font costs about 4,000 cycles a character, V81) */
static char st_left[40], st_right[40];

void ui_status(const char *s)
{
    strncpy(st_left, s, sizeof st_left - 1);
    clear(WIN_L + 1, STAT_LINE + 1, STAT_SPLIT, STAT_LINE + 14);
    UseFont(FONT_UI);
    fit(st_left, STAT_SPLIT - WIN_L - 12);
    WPutString(sb, WIN_L + 6, STAT_LINE + 2 + baselineOffset);
    UseSystemFont();
}

static void status_modes(void)
{
    strcpy(st_right, repeat ? "Repeat on" : "Repeat off");
    strcat(st_right, shuffle ? "     Shuffle on" : "     Shuffle off");
    win_status(st_left, st_right);
}

/*---------------------------------------------------------------
  now playing: the fields, each redrawn when its value changes
 ---------------------------------------------------------------*/
enum { F_POS, F_PAT, F_ROW, F_SPD, F_TRACK, F_BPM, F_TIME, F_N };
static unsigned int f_last[F_N];
static const unsigned int f_x[F_N] = { 416, 552, 416, 552, 416, 552, 416 };
static const unsigned int f_y[F_N] = { 116, 116, 130, 130, 144, 144, 164 };
static const unsigned char f_w[F_N] = { 64, 64, 64, 64, 64, 64, 200 };
static const char *const f_label[F_N] = { "Position", "Pattern", "Row", "Speed", "Track", "BPM", "Time" };
static unsigned char prog_n, track_no;

static void labels(void)
{
    unsigned char i;
    for (i = 0; i < F_N; ++i)
        label(f_label[i], f_x[i] - 64, f_y[i]);
    WHorizontalLine(0xAA, RP_X, RP_R - 9, 101);
    /* the progress bar */
    ColorRect(PROG_COL, PROG_W, PROG_ROW, 1, C_BAR);
    WFrameRectangle(0xFF, PROG_COL * 8 - 1, PROG_ROW * 8 - 1, (PROG_COL + PROG_W) * 8, PROG_ROW * 8 + 8);
    /* the lights under play, pause and stop */
    for (i = 1; i < 4; ++i) {
        unsigned int x = (ICON_COL0 + 5 * i + 1) * 8;
        WFrameRectangle(0xFF, x - 1, LED_ROW * 8 - 1, x + 16, LED_ROW * 8 + 8);
    }
    for (i = 0; i < F_N; ++i)
        f_last[i] = 0xFFFF;
    prog_n = 0;
}

static void field(unsigned char i, unsigned int v)
{
    char s[16], *p;
    if (f_last[i] == v)
        return;
    f_last[i] = v;
    if (!loaded) {
        clear(f_x[i], f_y[i] - 7, f_x[i] + f_w[i] - 1, f_y[i] + 2);
        return;
    }
    switch (i) {
    case F_POS:
        p = dec(s, v + 1);
        strcpy(p, " of ");
        dec(p + 4, mod.song_length);
        break;
    case F_TRACK:
        p = dec(s, v);
        strcpy(p, " of ");
        dec(p + 4, n_mods);
        break;
    case F_TIME:
        p = mmss(s, v);
        if (song_secs) {
            strcpy(p, "  /  ");
            mmss(p + 5, song_secs);
        }
        break;
    default:
        dec(s, v);
    }
    field_text(s, f_x[i], f_y[i], f_w[i], i == F_TIME);
}

static void progress(unsigned int t)
{
    unsigned char n = 0;
    if (loaded && song_secs)
        n = t >= song_secs ? PROG_W : (unsigned char)(((unsigned long)t * PROG_W) / song_secs);
    if (n > prog_n)
        ColorRect(PROG_COL + prog_n, n - prog_n, PROG_ROW, 1, C_ROW);
    else if (n < prog_n)
        ColorRect(PROG_COL + n, prog_n - n, PROG_ROW, 1, C_BAR);
    prog_n = n;
}

static void leds(void)
{
    ColorRect(ICON_COL0 + 6, 2, LED_ROW, 1, loaded && !paused ? C_GREEN : 0);
    ColorRect(ICON_COL0 + 11, 2, LED_ROW, 1, loaded && paused ? C_YELLOW : 0);
    ColorRect(ICON_COL0 + 16, 2, LED_ROW, 1, loaded ? 0 : C_RED);
}

/* the module's own lines: its title, the file, its form, its samples' names */
static void now_static(void)
{
    unsigned char i, n;
    unsigned int y;
    char s[48], *p;
    title();
    clear(RP_X, 70, RP_R - 2, 96);
    clear(RP_X, 242, RP_R - 2, RP_B - 2);
    if (!loaded) {
        field_text("Nothing is playing.", RP_X, 80, 260, 1);
        return;
    }
    memcpy(s, mod.title, 20);
    s[20] = 0;
    field_text(s[0] ? s : "(no title)", RP_X, 80, 260, 1);
    field_text(playing_name, RP_X, 94, 260, 0);
    strcpy(s, mod.sig);
    p = s + strlen(s);
    strcpy(p, ",  ");
    p = dec(p + 3, mod.num_patterns);
    strcpy(p, " patterns,  ");
    p = dec(p + 12, (unsigned int)((mod_bytes + 1023) >> 10));
    strcpy(p, " KB");
    field_text(s, RP_X, 252, 260, 0);
    for (i = 0, n = 0, y = 268; i < MOD_MAX_SAMPLES && n < 5; ++i)
        if (mod.samples[i].name[0] && mod.samples[i].name[0] != ' ') {
            field_text(mod.samples[i].name, RP_X + 8, y, 252, 0);
            y += 12;
            ++n;
        }
}

/*---------------------------------------------------------------
  the playlist: rows of 12 pixels, a mark and a picture each, the
  selection inverted, the playing module marked and bold; the scroll bar
  (the colour map's grey, its thumb darker)
 ---------------------------------------------------------------*/
static unsigned char thumb_a, thumb_n;

static void list_head(void)
{
    char *p;
    clear(LIST_X + 4, 49, LP_R - 1, 61);
    currentMode = SET_BOLD;
    label("Playlist", LIST_X + 6, 59);
    currentMode = 0;
    fit((const char *)curDirName, 150);
    WPutString(sb, LIST_X + 62, 59);
    p = dec(sb, n_mods);
    strcpy(p, n_mods == 1 ? " module" : " modules");
    WPutString(sb, LP_R - 8 - strw(sb), 59);
}

/* the selection: row r's cards light blue (the colour map), or not */
static void list_mark(unsigned char r, unsigned char on)
{
    ColorRect(LIST_COL, SB_COL - LIST_COL, (LIST_Y >> 3) + 2 * r, 2, on ? C_SEL : 0);
}

static void list_row(unsigned char r)
{
    unsigned char e = (unsigned char)(top + r);
    unsigned int y = LIST_Y + r * ROW_H;
    const unsigned char *pic;
    clear(LIST_X, y, LIST_XR, y + ROW_H - 1);
    list_mark(r, e == sel && e < n_ent);
    if (e >= n_ent)
        return;
    ent_get(e);
    pic = cur[0] == 'U' ? pic_up : cur[0] == 'D' ? pic_folder : pic_note;
    if (e == playing_ent && loaded)
        WBitmapUp((void *)pic_playing, LIST_COL, 1, y + 4, 8);
    WBitmapUp((void *)pic, LIST_COL + 1, 1, y + 4, 8);
    currentMode = e == playing_ent && loaded ? SET_BOLD : 0;
    fit(cur[0] == 'U' ? ".." : cur + 1, LIST_XR - 36);
    WPutString(sb, LIST_X + 18, y + 11);
    currentMode = 0;
}


static void list_bar(void)
{
    unsigned char a = 0, n = SB_TRACK;
    if (n_ent > LIST_N) {
        a = (unsigned char)(((unsigned int)top * SB_TRACK) / n_ent);
        n = (unsigned char)((LIST_N * SB_TRACK) / n_ent);
        if (!n)
            n = 1;
        if (a + n > SB_TRACK)
            a = SB_TRACK - n;
    }
    if (a == thumb_a && n == thumb_n)
        return;
    ColorRect(SB_COL, 1, SB_ROW0 + 1, SB_TRACK, C_BAR);
    ColorRect(SB_COL, 1, SB_ROW0 + 1 + a, n, C_THUMB);
    thumb_a = a;
    thumb_n = n;
}

static void list_draw(void)
{
    unsigned char r;
    for (r = 0; r < LIST_N; ++r)
        list_row(r);
    list_bar();
}

static void panel(void)
{
    unsigned char r;
    if (view_pattern) {
        clear(LIST_X, LIST_Y, LP_R - 1, LP_B - 1);
        ColorRect(LIST_COL, SB_COL + 1 - LIST_COL, SB_ROW0, 33, 0);
        pv_force = 1;
        call2(pv_update);
        return;
    }
    call2(pv_off);
    clear(LIST_X, LIST_Y, LP_R - 1, LP_B - 1);
    WVerticalLine(0xFF, SB_COL * 8 - 1, LP_T + 17, LP_B);
    WBitmapUp((void *)pic_arrow_up, SB_COL, 1, SB_ROW0 * 8, 8);
    WBitmapUp((void *)pic_arrow_down, SB_COL, 1, SB_ROWN * 8, 8);
    thumb_a = thumb_n = 0xFF;
    list_head();
    for (r = 0; r < LIST_N; ++r)
        list_row(r);
    list_bar();
}

static void row_of(unsigned char e)
{
    if (!view_pattern && e != 0xFF && e >= top && e < top + LIST_N)
        list_row(e - top);
}

/* the list scrolled so that its first row is t */
static void scroll_to(int t)
{
    int d;
    unsigned char r;
    int most = n_ent > LIST_N ? n_ent - LIST_N : 0;
    if (t > most)
        t = most;
    if (t < 0)
        t = 0;
    if ((unsigned char)t == top)
        return;
    d = t - top;
    top = (unsigned char)t;
    if (d >= LIST_N || d <= -LIST_N) {
        list_draw();
        return;
    }
    /* the rows that stay are moved (CopyRect), the new ones drawn; the
       selection's colour is set again (the colour map does not move) */
    ColorRect(LIST_COL, SB_COL - LIST_COL, LIST_Y >> 3, 2 * LIST_N, 0);
    if (d > 0) {
        CopyRect(LIST_COL, SB_COL - LIST_COL, LIST_Y + d * ROW_H, (LIST_N - d) * ROW_H, LIST_COL, LIST_Y);
        for (r = LIST_N - d; r < LIST_N; ++r)
            list_row(r);
    } else {
        CopyRect(LIST_COL, SB_COL - LIST_COL, LIST_Y, (LIST_N + d) * ROW_H, LIST_COL, LIST_Y - d * ROW_H);
        for (r = 0; r < -d; ++r)
            list_row(r);
    }
    if (sel >= top && sel < top + LIST_N)
        list_mark(sel - top, 1);
    list_bar();
}

static void select_entry(int e)
{
    unsigned char old = sel, t = top;
    if (!n_ent)
        return;
    if (e < 0)
        e = 0;
    if (e >= n_ent)
        e = n_ent - 1;
    sel = (unsigned char)e;
    if (sel < top)
        top = sel;
    else if (sel >= top + LIST_N)
        top = sel - LIST_N + 1;
    if (view_pattern)
        return;                         /* (the list is drawn when it comes back) */
    if (top != t)
        list_draw();
    else {
        if (old >= top && old < top + LIST_N)
            list_mark(old - top, 0);
        list_mark(sel - top, 1);
    }
}

/*---------------------------------------------------------------
  playing
 ---------------------------------------------------------------*/
static unsigned char hist[16], n_hist;

static const char *const errs[] = { "", "Cannot open it", "Too short", "Not a 4-channel MOD",
                                    "Bad song length", "Too big for the memory" };

/* a module started or stopped: everything that shows it */
void ui_track(void)
{
    unsigned char i;
    track_no = 0;
    if (loaded)
        for (i = 0; i <= playing_ent; ++i) {
            ent_get(i);
            if (cur[0] == 'M' || cur[0] == 'L')
                ++track_no;
        }
    now_static();
    for (i = 0; i < F_N; ++i)
        f_last[i] = 0xFFFE;
    progress(0);
    leds();
    ui_status(loaded ? (paused ? "Paused" : "Playing") : "Stopped");
    if (view_pattern) {
        pv_force = 1;
        call2(pv_update);
    }
}

void ui_loading(void)
{
    char s[20], *p;
    strcpy(s, "Loading ");
    p = dec(s + 8, mod_load_total ? (unsigned int)((mod_load_done * 100) / mod_load_total) : 0);
    strcpy(p, " %");
    ui_status(s);
}

/* The playlist's position: the entry last played or tried (0xFF none),
   in the folder playing_dir. Next and previous, and the end of a song,
   go on from it -- also after a module that would not load, after stop,
   and while the playlist shows another folder (step_track goes back to
   the music's folder first). */
static unsigned char q_pos = 0xFF;

void ui_play(unsigned char e)
{
    unsigned char was = playing_ent;
    mod_err_t r;
    ent_get(e);
    if (cur[0] == 'L') {
        ui_status("The name is too long to open");
        Beep();
        return;
    }
    if (strcmp(playing_dir, (const char *)curDirName)) {
        strcpy(playing_dir, (const char *)curDirName);
        n_hist = 0;                     /* (the shuffle's history: entries of the old folder) */
    }
    q_pos = e;
    SetPointer(PTR_BUSY);
    ui_status("Loading");
    r = play_entry(e);
    SetPointer(PTR_ARROW);
    row_of(was);
    if (r != MOD_OK) {
        ui_track();
        ui_status(r <= MOD_ERR_NOMEM ? errs[r] : "Error");
        Beep();
        return;
    }
    row_of(e);
    select_entry(e);                    /* (the selection follows the music) */
    ui_track();
}

/* the next or previous module (shuffle: a random one, back through the
   ones played), wrapping at the ends of the list; a module that does not
   load is passed over (its error shown) */
static void step_track(signed char d)
{
    static unsigned char e, k, n, got, was, rnd;       /* (static: smaller code) */
    if (music_dir()) {                  /* (another folder is shown) */
        ui_list();
        LOG("FOLDER", (const char *)curDirName);
    }
    if (!n_mods)
        return;
    if (shuffle && d < 0 && n_hist) {
        ui_play(hist[--n_hist]);
        if (loaded)
            return;
    }
    /* the module playing (or last tried) when next was asked for:
       shuffle never picks it again while there are two or more, also
       after a pick that did not load (q_pos is then that pick's: the
       second pick once took the module playing before) */
    was = 0xFF;
    rnd = 0;
    if (shuffle && d > 0 && n_mods > 1) {
        rnd = 1;
        was = playing_ent;
        if (was == 0xFF)
            was = q_pos;
    }
    for (n = 0; n < n_mods; ++n) {
        /* shuffle: a random module, not the one before nor the one just
           tried; when the picks find none, the next in order (passing
           over the one before). (The picks once shared the "none found"
           counter: a first pick that fitted read as none, and next and
           a song's end did nothing.) */
        got = 0;
        if (rnd)
            for (k = 1; k; ++k) {
                GetRandom();
                e = (unsigned char)(random % n_ent);
                ent_get(e);
                if (cur[0] == 'M' && e != q_pos && e != was) {
                    got = 1;
                    if (playing_ent != 0xFF) {
                        if (n_hist == sizeof hist) {
                            memmove(hist, hist + 1, sizeof hist - 1);
                            --n_hist;
                        }
                        hist[n_hist++] = playing_ent;
                    }
                    break;
                }
            }
        if (!got) {
            e = q_pos < n_ent ? q_pos : (d > 0 ? n_ent - 1 : 0);
            for (k = 0; k < n_ent; ++k) {
                e = (unsigned char)(e + d);
                if (e >= n_ent)
                    e = d > 0 ? 0 : n_ent - 1;
                ent_get(e);
                if (cur[0] == 'M' && e != was)
                    break;
            }
            if (k == n_ent)
                return;
        }
        ui_play(e);
        if (loaded)
            return;
    }
}

static void pause_toggle(void)
{
    if (!loaded) {
        if (n_ent && sel < n_ent) {
            ent_get(sel);
            if (cur[0] == 'M') {
                ui_play(sel);
                return;
            }
        }
        step_track(1);
        return;
    }
    paused = !paused;
    mod_pause(paused);
    leds();
    ui_status(paused ? "Paused" : "Playing");
    LOG(paused ? "PAUSE" : "RESUME", 0);
}

static void stop_cmd(void)
{
    unsigned char was = playing_ent;
    stop();
    playing_ent = 0xFF;
    row_of(was);
    ui_track();
    LOG("STOP", 0);
}

/*---------------------------------------------------------------
  folders
 ---------------------------------------------------------------*/
/* the playlist read again (the folder in curDirName is the current one),
   with the busy pointer */
void ui_list(void)
{
    SetPointer(PTR_BUSY);
    list_read();
    SetPointer(PTR_ARROW);
    q_pos = playing_dir[0] && !strcmp(playing_dir, (const char *)curDirName) ? find_entry(playing_name) : 0xFF;
    playing_ent = loaded ? q_pos : 0xFF;
    if (!view_pattern)
        panel();
}

static void enter(const char *name)
{
    unsigned char l;
    if (name[0] == '.' && name[1] == '.') {
        if (at_root() || ChDir(".."))
            return;
        l = (unsigned char)strlen((char *)curDirName);
        while (l > 1 && curDirName[--l] != '/')
            ;
        curDirName[l ? l : 1] = 0;
    } else {
        l = (unsigned char)strlen((char *)curDirName);
        if (l + strlen(name) + 2 > 61 || ChDir((void *)name)) {
            ui_status("Cannot open the folder");
            Beep();
            return;
        }
        if (l > 1)
            curDirName[l++] = '/';
        strcpy((char *)curDirName + l, name);
    }
    ui_list();
    LOG("FOLDER", (const char *)curDirName);
}

static void open_entry(unsigned char e)
{
    char name[64];
    ent_get(e);
    if (cur[0] == 'D' || cur[0] == 'U') {
        strcpy(name, cur + 1);
        enter(name);
    } else
        ui_play(e);
}

/*---------------------------------------------------------------
  the menus' routines
 ---------------------------------------------------------------*/
static void m_about(void)
{
    menus_off();
    LOG("ABOUT", 0);
    call4(dlg_about);
}

static void da(unsigned char i)
{
    unsigned char r;
    menus_off();
    LOG("ACCESSORY", da_names[i]);
    vr_cd(VR_APPS);                     /* (with the applications: Veranda V71) */
    r = da_run(da_names[i]);
    ChDir((void *)curDirName);
    LOG("ACCESSORY BACK", 0);
    if (r) {
        ui_status("Cannot start it");
        Beep();
    }
}
static void m_da0(void) { da(0); }
static void m_da1(void) { da(1); }
static void m_da2(void) { da(2); }
static void m_da3(void) { da(3); }
static void m_da4(void) { da(4); }
static void m_da5(void) { da(5); }

/* File > Open...: Veranda's Open dialog (vr_file, V79): the modules and
   folders by their long names; a module chosen: its folder is the
   playlist's, and it plays */
static void m_open(void)
{
    unsigned char e;
    menus_off();
    LOG("OPEN BOX", 0);
    dlg_name[0] = 0;
    if (!choose(dlg_name)) {
        LOG("OPEN CANCEL", 0);
        return;
    }
    LOG("OPEN MODULE", dlg_name);
    ui_list();                          /* (the folder chosen: curDirName) */
    e = find_entry(dlg_name);
    if (e != 0xFF) {
        select_entry(e);
        ui_play(e);
    }
    LOG("FOLDER", (const char *)curDirName);
}

/* File > Play all: the playlist's folder from its first module */
static void m_playall(void)
{
    unsigned char e;
    menus_off();
    LOG("OPEN PLAY ALL", (const char *)curDirName);
    for (e = 0; e < n_ent; ++e) {
        ent_get(e);
        if (cur[0] == 'M') {
            ui_play(e);
            break;
        }
    }
}

static void m_quit(void)
{
    menus_off();
    stop();
#ifdef VMP_T
    LOG("UNDERRUNS", num(und_total, 5));
    LOG("POINTER", num(pointerShape, 3));
    call4(vmp_meter_log);
#endif
    LOG("QUIT", 0);
    vr_audio_off();
    vr_set_irq(0);
    EnterDeskTop();
}

static void m_play(void) { menus_off(); pause_toggle(); }
static void m_stop(void) { menus_off(); stop_cmd(); }
static void m_next(void) { menus_off(); step_track(1); }
static void m_prev(void) { menus_off(); step_track(-1); }

static void m_repeat(void)
{
    menus_off();
    repeat = !repeat;
    menu_marks();
    status_modes();
    LOG(repeat ? "REPEAT ON" : "REPEAT OFF", 0);
}

static void m_shuffle(void)
{
    menus_off();
    shuffle = !shuffle;
    n_hist = 0;
    menu_marks();
    status_modes();
    LOG(shuffle ? "SHUFFLE ON" : "SHUFFLE OFF", 0);
}

static void m_pattern(void)
{
    menus_off();
    if (view_pattern)
        return;
    view_pattern = 1;
    menu_marks();
    panel();
    LOG("VIEW PATTERN", 0);
}

static void m_list(void)
{
    menus_off();
    if (!view_pattern)
        return;
    view_pattern = 0;
    menu_marks();
    panel();
    LOG("VIEW LIST", 0);
}

/*---------------------------------------------------------------
  the transport buttons: GEOS icons (DoIcons), flashed when clicked
 ---------------------------------------------------------------*/
static void i_prev(void) { LOG("ICON PREVIOUS", 0); step_track(-1); }
static void i_play(void)
{
    LOG("ICON PLAY", 0);
    if (!loaded || paused)
        pause_toggle();
}
static void i_pause(void)
{
    LOG("ICON PAUSE", 0);
    if (loaded)
        pause_toggle();
}
static void i_stop(void) { LOG("ICON STOP", 0); stop_cmd(); }
static void i_next(void) { LOG("ICON NEXT", 0); step_track(1); }

struct icon { const void *pic; unsigned char x, y, w, h; void *f; };
struct icons5 { unsigned char n; unsigned int mx; unsigned char my; struct icon i[5]; };
struct icons5 icons = { 5, 0, 0, {
    { pic_prev,  ICON_COL0,      ICON_Y, 4, 24, i_prev },
    { pic_play,  ICON_COL0 + 5,  ICON_Y, 4, 24, i_play },
    { pic_pause, ICON_COL0 + 10, ICON_Y, 4, 24, i_pause },
    { pic_stop,  ICON_COL0 + 15, ICON_Y, 4, 24, i_stop },
    { pic_next,  ICON_COL0 + 20, ICON_Y, 4, 24, i_next } } };

/*---------------------------------------------------------------
  the pointer and the keys
 ---------------------------------------------------------------*/
static void pressed(void)
{
    unsigned int x, y;
    unsigned char r, e;
    if (mouseData & 0x80)
        return;                         /* a release */
    do {
        x = mouseXPos;
        y = mouseYPosW;
    } while (x != mouseXPos || y != mouseYPosW);
    if (y < LIST_Y && win_hit(x, y) == VW_XCLOSE) {      /* the close glyph */
        m_quit();
        return;
    }
    if (view_pattern || y < LIST_Y || y >= LIST_Y + LIST_N * ROW_H)
        return;
    if (x >= SB_COL * 8 && x < SB_COL * 8 + 8) {      /* the scroll bar */
        r = (unsigned char)(y >> 3);
        if (r == SB_ROW0)
            scroll_to((int)top - 1);
        else if (r == SB_ROWN)
            scroll_to((int)top + 1);
        else if (r < SB_ROW0 + 1 + thumb_a)
            scroll_to((int)top - (LIST_N - 1));
        else if (r >= SB_ROW0 + 1 + thumb_a + thumb_n)
            scroll_to((int)top + (LIST_N - 1));
        return;
    }
    if (x < LIST_X || x > LIST_XR)
        return;
    e = (unsigned char)(top + (y - LIST_Y) / ROW_H);
    if (e >= n_ent)
        return;
    if (e == sel && dblClickCount) {
        dblClickCount = 0;
        LOG("DOUBLE CLICK", 0);
        open_entry(e);
        return;
    }
    select_entry(e);
    dblClickCount = CLICK_COUNT;
}

static void key(void)
{
    unsigned char k = keyData;
    if (menuNumber)
        return;                         /* (a menu is open: it would be drawn over) */
    if (k == ' ')
        pause_toggle();
    else if (k == KEY_RIGHT)
        step_track(1);
    else if (k == KEY_LEFT && (keyDataMods & KEYM_ARROW))
        step_track(-1);
    else if (k == KEY_ESC)
        stop_cmd();
    else if (k == (SHORTCUT | 'o'))
        m_open();
    else if (k == (SHORTCUT | 'q'))
        m_quit();
    else if (view_pattern)
        return;
    else if (k == KEY_UP)
        select_entry((int)sel - 1);
    else if (k == KEY_DOWN)
        select_entry((int)sel + 1);
    else if (k == KEY_PGUP)
        select_entry((int)sel - (LIST_N - 1));
    else if (k == KEY_PGDN)
        select_entry((int)sel + (LIST_N - 1));
    else if (k == KEY_HOME)
        select_entry(0);
    else if (k == KEY_END)
        select_entry(255);
    else if (k == KEY_LEFT)             /* Backspace: up a folder */
        enter("..");
    else if (k == CR && n_ent)
        open_entry(sel);
}

/*---------------------------------------------------------------
  the main loop's work: at most 20 updates a second
 ---------------------------------------------------------------*/
static unsigned int last_frame;
#ifdef VMP_T
#define M_START(k) vmp_meter_start(k)
#define M_END(k) vmp_meter_end(k)
#else
#define M_START(k)
#define M_END(k)
#endif
#ifdef VMP_STRESS
static void stress(void);
#endif

static unsigned char update_body(void)
{
    signed char w;
    if ((unsigned int)(frames - last_frame) < 3)
        return 0;
    last_frame = frames;
    asm("sei");
    w = (signed char)mouseWheel;
    mouseWheel = 0;
    asm("cli");
    if (loaded && mod_status.song_ends != last_song_ends) {
        last_song_ends = mod_status.song_ends;     /* consumed where acted on */
        if (repeat) {
            t_base = mod_status.elapsed;
            LOG("SONG REPEAT", 0);
        } else {
            LOG("SONG END", 0);
            step_track(1);
            return 1;
        }
    }
    M_START(4);
    call2(vu_update);                   /* (overlay 2) */
    M_END(4);
    if (loaded) {
        unsigned int t = mod_status.elapsed - t_base;
        M_START(5);
        progress(t);
        if (!menuNumber) {
            field(F_POS, mod_status.order);
            field(F_PAT, mod_status.pattern);
            field(F_ROW, mod_status.row);
            field(F_SPD, mod_status.speed);
            field(F_BPM, mod_status.bpm);
            field(F_TRACK, track_no);
            field(F_TIME, t);
            M_END(5);
            if (view_pattern) {
                M_START(6);
                call2(pv_update);
                M_END(6);
            }
        }
    } else if (!menuNumber && f_last[F_TIME] != 0xFFFD) {
        unsigned char i;
        for (i = 0; i < F_N; ++i)
            field(i, 0xFFFD);
    }
    if (w && !view_pattern && !menuNumber)
        scroll_to((int)top + 3 * w);
#ifdef VMP_STRESS
    stress();
#endif
    return 1;
}

/* (the test builds: each pass's cycles, without the audio batches that
   ran inside it: vmodplay.c's vmp_meter) */
static void update(void)
{
#ifdef VMP_T
    vmp_meter_start(0);
    if (update_body())
        vmp_meter_end(0);
#else
    update_body();
#endif
}

#ifdef VMP_STRESS
/* The stress run (tools/gate_vmodplay.sh, VMP_STRESS=seconds): the
   folder Mods, its first module, repeat on; then every half second a
   change that redraws: the list scrolled five rows (back to the top at
   its end) and the selection moved, every 2 s the view switched (the
   whole panel: the playlist, or the pattern view's page), every 30 s the
   next module (a load); after VMP_STRESS seconds, quit. The VU meters,
   the fields, the progress bar and the pattern's rows go on as usual. */
static unsigned int st_next;
static unsigned char st_k, st_secs;

static void stress(void)
{
    if ((int)(frames - st_next) < 0)
        return;
    st_next = frames + 30;
    if (!st_k++) {
        enter("Mods");
        repeat = 1;
        menu_marks();
        status_modes();
        step_track(1);
        return;
    }
    if (!(st_k & 3)) {
        view_pattern = !view_pattern;
        menu_marks();
        vmp_meter_start(view_pattern ? 1 : 2);
        panel();
        vmp_meter_end(view_pattern ? 1 : 2);
    } else if (!view_pattern) {
        vmp_meter_start(3);
        if (top + LIST_N >= n_ent)
            scroll_to(0);
        else
            scroll_to((int)top + 5);
        vmp_meter_end(3);
        select_entry((int)top + (st_k & 7));
    }
    if (!(st_k % 60))
        step_track(1);
    if (++st_secs >= 2 * VMP_STRESS) {
        LOG("STRESS DONE", 0);
        m_quit();
    }
}
#endif

void ui_menus(void)
{
    call4(menu_layout);                 /* (overlay 4) */
    menu_marks();
    theme_menus();                      /* the menu bar (Veranda V74, V79) */
    DoMenu((void *)&mbar, 0);
}

void ui_start(void)
{
    call2(frame);
    labels();
    status_modes();
    iconSelFlg = ST_FLASH;
    DoIcons((void *)&icons);
    panel();
    ui_track();
    ui_menus();
    otherPressVec = VR_FN(pressed);
    keyVector = VR_FN(key);
    appMain = VR_FN(update);
#ifdef VMP_TEST
    call4(vmp_test_start);
#endif
}
