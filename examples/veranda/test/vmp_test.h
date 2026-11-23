/*
 * vmp_test.h -- the Veranda MOD Player's test session (included by
 * vmp_dlg.c, overlay 4, with -DVMP_TEST; tools/gate_vmodplay.sh).
 *
 * The script, through mouseSource (test/vr_play.s, which waits until the
 * system has taken each button change; positions count at presses only),
 * keys into the ROM's keyboard queue (its codes: Veranda turns them into
 * GEOS's):
 *   the root: a double click on the folder Mods in the playlist; a double
 *   click on its second module (DIZZYCRS.MOD); MOD Player > the desk accessory
 *   (the Calculator, in the root: the system folder there; da_run puts an
 *   Esc in the ROM's queue for it, so it closes, and keeps mouseSource,
 *   which its start and end clear: the player comes back and plays on;
 *   with a small module playing, so that it has the RAM banks it needs on
 *   a 512 KB machine too); the pause icon; the next
 *   icon (JESTER); View > Pattern view; View > Playlist; Play > Repeat;
 *   File > Open... (Veranda's Open dialog, vr_file, on /Mods), by its
 *   keys: Backspace (up: the top folder), M (the folder Mods), Return
 *   (into it), C (Checknob.mod), Return (open); MOD Player > About, OK;
 *   the keys Space (pause), Space (on), Right (the next module:
 *   DIZZYCRS), Left (back: Checknob), Esc (stop); File > Play all (the
 *   first module of /Mods); File > Quit.
 * The positions are computed from the layout the program uses (vmp.h, the
 * menu bar's items, barx, as the kernel places them: Veranda Look 2,
 * DECISIONS V74, V79; the about box's place). VMP_STOP=n: the session stops at the
 * script's frame n with a VRAM dump (a screenshot). "VMP SHOT name frame"
 * lines name the gate's moments.
 *
 * MIT licence (see LICENSE).
 */
extern void __fastcall__ vr_play(const void *script);

#ifndef VMP_STOP
#define VMP_STOP 0xFFF0
#endif


static unsigned int script[64 * 3];     /* (main RAM: the interrupt reads it; 59 steps) */
static unsigned int *sp_;

static void at(unsigned int f, unsigned int x, unsigned int y, unsigned char b)
{
    if (f > VMP_STOP)
        return;
    *sp_++ = f;
    *sp_++ = x;
    *sp_++ = (y & 255) | (y >= 256 ? 0x4000 : 0) | (b ? 0x100 : 0);
}

/* a press and release at (x, y) */
static unsigned int click(unsigned int f, unsigned int x, unsigned int y)
{
    at(f + 1, x, y, 1);
    at(f + 3, x, y, 0);
    return f + 10;
}

static unsigned int dclick(unsigned int f, unsigned int x, unsigned int y)
{
    at(f + 1, x, y, 1);
    at(f + 3, x, y, 0);
    at(f + 5, x, y, 1);
    at(f + 7, x, y, 0);
    return f + 14;
}

/* a key (the ROM's code) */
static unsigned int key(unsigned int f, unsigned char k)
{
    if (f > VMP_STOP)
        return f;
    *sp_++ = f;
    *sp_++ = k;
    *sp_++ = 0x8000;
    return f + 10;
}

/* menu m (0-3) of the bar, its item n: a press on the bar's item, then
   one on the list's (test/vr_play.s: between presses the pointer stays
   where the system puts it, so the sub menu that opens is not closed by
   the script). The kernel's geometry (V74): the list under its bar item,
   from its left, 16 lines an item from line 16 */
static unsigned int menu(unsigned int f, unsigned char m, unsigned char n)
{
    static struct mhd *const subs[4] = { &mgeos, &mfile, &mplay, &mview };
    unsigned int tx = barx[m], mid = subs[m]->left + 24;
    at(f, tx, 7, 1);
    at(f + 2, tx, 7, 0);
    at(f + 10, mid, 24 + 16 * n, 1);
    at(f + 12, mid, 24 + 16 * n, 0);
    return f + 30;
}

#define ROW_AT(r)  (LIST_Y + (r) * ROW_H + 6)
#define LIST_AT    (LIST_X + 60)
#define ICON_AT(i) ((ICON_COL0 + 5 * (i)) * 8 + 16)

static void shot(const char *name, unsigned int f)
{
    char s[24];
    strcpy(s, name);
    strcat(s, " ");
    strcat(s, num(f, 5));
    LOG("SHOT", s);
}

void vmp_test_start(void)
{
    unsigned int f = 60;
    sp_ = script;
    shot("root", f - 10);
    f = dclick(f, LIST_AT, ROW_AT(0));                   /* Mods */
    shot("folder", f + 60);
    f = dclick(f + 70, LIST_AT, ROW_AT(2));              /* DIZZYCRS.MOD */
    shot("playing", f + 400);
    f = menu(f + 410, 0, 1);                             /* MOD Player: Calculator (an Esc */
    f = click(f + 60, ICON_AT(2), ICON_Y + 12);          /* waits for it: da_run); pause */
    shot("paused", f + 60);
    f = click(f + 70, ICON_AT(4), ICON_Y + 12);          /* next */
    f = menu(f + 200, 3, 0);                             /* view: pattern view */
    shot("pattern", f + 300);
    f = menu(f + 310, 3, 1);                             /* view: playlist */
    shot("list", f + 100);
    shot("menu", f + 116);                               /* (the play menu open) */
    f = menu(f + 110, 2, 4);                             /* play: repeat */
    f = menu(f + 20, 1, 0);                              /* File: Open... */
    shot("open", f + 120);
    f = key(f + 130, 0x14);                              /* Backspace: up */
    shot("openroot", f + 120);
    f = key(f + 130, 'M');                               /* the folder Mods */
    f = key(f + 30, 0x0D);                               /* Return: into it */
    f = key(f + 120, 'C');                               /* Checknob.mod */
    f = key(f + 30, 0x0D);                               /* Return: open */
    f = menu(f + 300, 0, 0);                             /* MOD Player: About */
    shot("about", f + 60);
    f = click(f + 70, AB_LEFT + 29 * 8 + 24, AB_TOP + 148 + 8);    /* OK */
    f = key(f + 60, ' ');                                /* pause */
    f = key(f + 60, ' ');                                /* on */
    f = key(f + 60, 0x1D);                               /* right: next, DIZZYCRS */
    f = key(f + 200, 0x9D);                              /* left: back, Checknob */
    f = key(f + 200, 0x1B);                              /* Esc: stop */
    shot("stopped", f + 40);
    f = menu(f + 50, 1, 1);                              /* File: Play all */
    f = menu(f + 300, 1, 2);                             /* File: Quit */
    at(VMP_STOP < f + 600 ? VMP_STOP : f + 600, 320, 240, 0);
    *sp_++ = 0xFFFF;
    *sp_++ = VMP_STOP < f + 600 ? VMP_STOP : f + 600;
    *sp_ = 0;
    vr_play(script);
}
