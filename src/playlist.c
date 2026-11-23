/*
 * playlist.c -- scan the disk directory for modules.
 *
 * CMDR-DOS returns "$" as a BASIC program listing: a load address, then one
 * line per file (link, block count, text, NUL terminator). The filename is
 * the quoted part of each line, so the parser only has to track quotes -- and
 * doing that incrementally means the whole listing never has to be held in
 * memory at once, which matters when fixed RAM is the scarce resource.
 */
#include "playlist.h"

#include <string.h>

#define RAM_BANK_REG (*(volatile unsigned char *)0x0000)
#define WIN_BASE 0xA000

/* src/kread.s */
extern unsigned char kfile_open(void);
extern void          kfile_read(void);
extern void          kfile_close(void);
extern unsigned int  kf_name;
extern unsigned char kf_namelen;
extern unsigned int  kf_dest;
extern unsigned int  kf_count;
extern unsigned int  kf_read;
extern unsigned char kf_eof;
extern unsigned char kf_slow;
extern unsigned char kf_sa;
extern unsigned char kfile_cmd(void);

static void playlist_sort(void);   /* defined below the scan */
static unsigned int ord_get(unsigned int i);   /* display position -> slot */

static unsigned int  n_entries;
/* Leading directory entries. Sorting puts ".." and every subdirectory first,
   so the modules are a contiguous tail and this is the offset between a list
   index and a track number. Without it the first module in a folder is
   announced as track 2. */
static unsigned int  n_dirs;
static unsigned char scan_err;
static unsigned char truncated;
/*
 * Chosen by playlist_init() from the machine's RAM. The defaults are the
 * worst case, so a build that never calls it (or a call that has not happened
 * yet) still addresses banks it actually owns.
 */
static unsigned char pl_banks    = PL_BANKS_MAX;
static unsigned int  pl_max      = PL_MAX_CAP;
static unsigned char pl_ord_bank = PL_ORD_BANK_MAX;
static unsigned int  scan_cap    = PL_MAX_CAP;

/* src/mod_x16.c -- where sample data may start, once the list has taken its
   share. Set here because only the playlist knows how much it took. */
extern unsigned char mod_first_bank;

unsigned char playlist_init(unsigned char ram_banks)
{
    unsigned char b = (unsigned char)(ram_banks >> 4);   /* a sixteenth */
    if (b < 2) b = 2;                                    /* 256 entries floor */
    if (b > PL_BANKS_MAX) b = PL_BANKS_MAX;
    pl_banks    = b;
    pl_max      = (unsigned int)b * PL_PER_BANK;
    pl_ord_bank = (unsigned char)(PL_BANK + b);
    scan_cap    = pl_max;
    mod_first_bank = (unsigned char)(pl_ord_bank + 1);
    return mod_first_bank;
}

unsigned int playlist_capacity(void)
{
    return pl_max;
}

/* ---------------------------------------------------------------------------
 * The shuffle bag: one bit per entry, in the half of the order bank the index
 * array does not use (2048 bits = 256 bytes at offset 4096 of 8192).
 *
 * Shuffle used to pick uniformly at random every time, which is not what
 * anyone means by shuffle: with n modules you expect a repeat after about
 * sqrt(n) picks, so a folder of fifty replays favourites within the first
 * handful while others never come up. Marking what has been heard and drawing
 * only from the rest plays everything once before anything twice.
 * ------------------------------------------------------------------------ */
#define PL_PLAYED_OFF  (PL_MAX_CAP * 2)
#define PL_PLAYED_SIZE (PL_MAX_CAP / 8)

void playlist_played_clear(void)
{
    unsigned char save = RAM_BANK_REG;
    RAM_BANK_REG = pl_ord_bank;
    memset((void *)(WIN_BASE + PL_PLAYED_OFF), 0, PL_PLAYED_SIZE);
    RAM_BANK_REG = save;
}

void playlist_played_set(unsigned int idx)
{
    unsigned char save, *p;
    if (idx >= pl_max)
        return;
    save = RAM_BANK_REG;
    RAM_BANK_REG = pl_ord_bank;
    p = (unsigned char *)(WIN_BASE + PL_PLAYED_OFF + (idx >> 3));
    *p |= (unsigned char)(1 << (idx & 7));
    RAM_BANK_REG = save;
}

unsigned char playlist_played(unsigned int idx)
{
    unsigned char save, v;
    const unsigned char *p;
    if (idx >= pl_max)
        return 1;                   /* out of range: never offer it */
    save = RAM_BANK_REG;
    RAM_BANK_REG = pl_ord_bank;
    p = (const unsigned char *)(WIN_BASE + PL_PLAYED_OFF + (idx >> 3));
    v = (unsigned char)(*p & (1 << (idx & 7)));
    RAM_BANK_REG = save;
    return v;
}
static char          namebuf[PL_ENTRY];
static unsigned char chunk[128];
static const char dollar[2] = { 0x24, 0 };
static const char dotdot[3] = { 0x2E, 0x2E, 0 };   /* ".." */
static char cmdbuf[PL_ENTRY + 8];


/*
 * Entry layout in banked RAM: one type byte, then the name.
 *
 * The bank register is saved and restored around every access. store() runs
 * while the directory file is still OPEN, and bank 0 holds CMDR-DOS's own
 * variables and buffers -- leaving the window pointed at a playlist bank
 * across a KERNAL read leaves the DOS reading its state from the wrong bank.
 * A host filesystem does not care; a real card does.
 */
static void store(const char *name, unsigned int idx, unsigned char isdir)
{
    unsigned char *dst;
    unsigned char i, save;

    if (idx >= pl_max)
        return;
    save = RAM_BANK_REG;
    RAM_BANK_REG = (unsigned char)(PL_BANK + idx / PL_PER_BANK);
    dst = (unsigned char *)(WIN_BASE + (idx % PL_PER_BANK) * PL_ENTRY);
    dst[0] = isdir;
    for (i = 0; i < PL_ENTRY - 2 && name[i]; i++)
        dst[i + 1] = (unsigned char)name[i];
    dst[i + 1] = 0;
    RAM_BANK_REG = save;
}

/*
 * Accept only names ending in ".mod".
 *
 * Compared as raw byte values, never character literals: cc65 translates
 * ASCII literals to PETSCII, so 'M' in this source is $CD (the SHIFTED M)
 * while the directory hands back $4D. Every byte that comes off the wire has
 * to be matched numerically.
 *
 * Case folding covers both ASCII lowercase ($61-$7A) and PETSCII shifted
 * ($C1-$DA), since which one appears depends on the device and charset.
 */
static unsigned char upcase(unsigned char c)
{
    if (c >= 0x61 && c <= 0x7A)
        return (unsigned char)(c - 0x20);
    if (c >= 0xC1 && c <= 0xDA)
        return (unsigned char)(c - 0x80);
    return c;
}

static unsigned char is_mod(const char *s, unsigned char len)
{
    if (len < 4)
        return 0;
    if ((unsigned char)s[len - 4] != 0x2E)          /* '.' */
        return 0;
    return (unsigned char)(upcase((unsigned char)s[len - 3]) == 0x4D &&  /* M */
                           upcase((unsigned char)s[len - 2]) == 0x4F &&  /* O */
                           upcase((unsigned char)s[len - 1]) == 0x44);   /* D */
}

/*
 * The listing is a BASIC program: per line, a link word, the block count, the
 * quoted name, then the type ("PRG", "SEQ", "DIR"), then a NUL. Tracking the
 * type column is what lets directories be listed alongside modules, so the
 * user can browse to wherever the music actually is.
 */
/*
 * Full scan by default. The batched path (playlist_scan_limit / playlist_more)
 * is ready but not yet wired into the browser: switching the default before
 * the UI knows how to ask for more would leave anything past the first batch
 * unreachable, which is worse than a slower start.
 */
unsigned int playlist_scan(void)
{
    return playlist_scan_limit(pl_max);
}

/* Extend the listing by another batch. Returns the new count, or 0 if there
   was nothing more to read. */
unsigned int playlist_more(void)
{
    unsigned int before = n_entries;
    if (!truncated)
        return 0;
    playlist_scan_limit(scan_cap + PL_FIRST_BATCH);
    return (n_entries > before) ? n_entries : 0;
}

unsigned char playlist_truncated(void)
{
    return truncated;
}

unsigned int playlist_scan_limit(unsigned int max_entries)
{
    unsigned int got, i, guard;
    unsigned char inq = 0, nlen = 0, tlen = 0, have_name = 0, ch;
    unsigned char fulllen = 0;
    char type[4], tail[4];

    n_entries = 0;
    scan_err = 0;
    truncated = 0;
    scan_cap = max_entries > pl_max ? pl_max : max_entries;

    /* ".." first, so there is always a way back up even from an empty
       directory. */
    store(dotdot, n_entries, 1);
    n_entries++;

    kf_name = (unsigned int)dollar;
    kf_namelen = 1;
    kf_slow = 1;                    /* read the listing byte at a time */
    kf_sa = 0;                      /* "$" is served on the LOAD channel */
    scan_err = kfile_open();
    if (scan_err != 0) {
        kf_slow = 0;
        kf_sa = 2;
        return n_entries;           /* caller reports it; do not fail silently */
    }

    /* A directory listing is bounded; cap the read anyway so a device that
       never signals EOF cannot wedge the player. */
    for (guard = 0; guard < 2048; guard++) {
        kf_dest = (unsigned int)chunk;
        kf_count = sizeof(chunk);
        kfile_read();
        got = kf_read;
        if (got == 0)
            break;

        for (i = 0; i < got; i++) {
            ch = chunk[i];
            if (ch == 0x22) {                   /* quote, as a raw byte */
                if (!inq) {
                    inq = 1;
                    nlen = 0;
                    fulllen = 0;
                    tail[0] = tail[1] = tail[2] = tail[3] = 0;
                } else {
                    inq = 0;
                    namebuf[nlen] = 0;
                    have_name = 1;
                    tlen = 0;
                }
            } else if (inq) {
                if (nlen < PL_ENTRY - 2)
                    namebuf[nlen++] = (char)ch;
                /* Keep the last four characters seen regardless of where the
                   buffer ran out, so the extension test looks at the true end
                   of the name rather than the end of the truncation. */
                tail[0] = tail[1]; tail[1] = tail[2];
                tail[2] = tail[3]; tail[3] = (char)ch;
                if (fulllen < 255) fulllen++;
            } else if (have_name) {
                if (ch == 0) {                  /* end of listing line */
                    type[tlen] = 0;
                    if (tlen == 3 && type[0] == 0x44 && type[1] == 0x49 &&
                        type[2] == 0x52) {      /* "DIR" */
                        /* Skip the device's own "." and ".." -- one ".." is
                           prepended above so there is always a way up even on
                           a device that does not list one. */
                        if (!(namebuf[0] == 0x2E &&
                              (namebuf[1] == 0 ||
                               (namebuf[1] == 0x2E && namebuf[2] == 0)))) {
                            store(namebuf, n_entries, 1);
                            n_entries++;
                        }
                    } else if (is_mod(tail, 4) && fulllen == nlen) {
                        /* fulllen == nlen means the name fitted; a longer one
                           could not be opened later even if it is a module. */
                        store(namebuf, n_entries, 0);
                        n_entries++;
                    }
                    have_name = 0;
                } else if (tlen < 3 && upcase(ch) >= 0x41 && upcase(ch) <= 0x5A) {
                    type[tlen++] = (char)upcase(ch);
                }
            }
            if (n_entries >= scan_cap) {
                truncated = 1;
                break;
            }
        }

        if (kf_eof || n_entries >= scan_cap)
            break;
    }

    kfile_close();
    kf_slow = 0;                    /* module loading goes back to MACPTR */
    kf_sa = 2;                      /* data files use a general channel */
    playlist_sort();

    /* Count the directories once, here: they are already in a block at the
       front, and playlist_mark never turns a file into one (it only marks a
       module too big), so this stays true until the next scan. */
    for (n_dirs = 0; n_dirs < n_entries; n_dirs++)
        if (playlist_type(n_dirs) != PL_DIR)
            break;

    /* Indices mean something different after a rescan, so the bag cannot
       carry over -- entering a folder starts its shuffle fresh. */
    playlist_played_clear();

    return n_entries;
}

unsigned int playlist_chdir(const char *name)
{
    unsigned char i;

    /* "CD:" plus the name, assembled as raw bytes -- cc65 would translate a
       string literal to PETSCII and the drive would not recognise it. */
    cmdbuf[0] = 0x43;                           /* C */
    cmdbuf[1] = 0x44;                           /* D */
    cmdbuf[2] = 0x3A;                           /* : */
    for (i = 0; i < PL_ENTRY && name[i]; i++)
        cmdbuf[3 + i] = name[i];
    cmdbuf[3 + i] = 0;

    kf_name = (unsigned int)cmdbuf;
    kf_namelen = (unsigned char)(3 + i);
    kfile_cmd();
    return playlist_scan();
}

unsigned char playlist_type(unsigned int idx)
{
    const unsigned char *src;
    unsigned char save, t;
    if (idx >= n_entries)
        return PL_FILE;
    idx = ord_get(idx);                 /* display position -> entry slot */
    save = RAM_BANK_REG;
    RAM_BANK_REG = (unsigned char)(PL_BANK + idx / PL_PER_BANK);
    src = (const unsigned char *)(WIN_BASE + (idx % PL_PER_BANK) * PL_ENTRY);
    t = src[0];
    RAM_BANK_REG = save;
    return t;
}

unsigned char playlist_is_dir(unsigned int idx)
{
    return (unsigned char)(playlist_type(idx) == PL_DIR);
}

/* Record that an entry is unusable, so it is shown as such and never
   attempted again. */
void playlist_mark(unsigned int idx, unsigned char type)
{
    unsigned char *dst, save;
    if (idx >= n_entries)
        return;
    idx = ord_get(idx);
    save = RAM_BANK_REG;
    RAM_BANK_REG = (unsigned char)(PL_BANK + idx / PL_PER_BANK);
    dst = (unsigned char *)(WIN_BASE + (idx % PL_PER_BANK) * PL_ENTRY);
    dst[0] = type;
    RAM_BANK_REG = save;
}

unsigned int playlist_count(void)
{
    return n_entries;
}

unsigned int playlist_first_file(void)
{
    return n_dirs;
}

unsigned int playlist_file_count(void)
{
    return (unsigned int)(n_entries - n_dirs);
}

/* 1-based position of idx among the modules, or 0 if idx is not one. */
unsigned int playlist_track_no(unsigned int idx)
{
    if (idx >= n_entries || idx < n_dirs)
        return 0;
    return (unsigned int)(idx - n_dirs + 1);
}

const char *playlist_name(unsigned int idx)
{
    const unsigned char *src;
    unsigned char i, save;

    if (idx >= n_entries) {
        namebuf[0] = 0;
        return namebuf;
    }
    idx = ord_get(idx);
    save = RAM_BANK_REG;
    RAM_BANK_REG = (unsigned char)(PL_BANK + idx / PL_PER_BANK);
    src = (const unsigned char *)(WIN_BASE + (idx % PL_PER_BANK) * PL_ENTRY + 1);
    for (i = 0; i < PL_ENTRY - 2 && src[i]; i++)
        namebuf[i] = (char)src[i];
    namebuf[i] = 0;
    RAM_BANK_REG = save;
    return namebuf;
}

/* ---------------------------------------------------------------------------
 * Sorting.
 *
 * FAT32 hands entries back in creation order, which for a folder of a hundred
 * modules is a pile rather than a list. Sorted once after the scan: it costs
 * nothing at play time.
 *
 * Insertion sort, entries moved in place. The element being placed is held in
 * a fixed-RAM buffer so each comparison needs only ONE read out of banked RAM
 * rather than two. Roughly n^2/4 moves -- about half a second for a hundred
 * entries, once, and directories are kept ahead of files so ".." and folders
 * stay at the top where they are useful.
 * ------------------------------------------------------------------------ */
/* Sorting runs after the scan has closed the directory, so the 128-byte read
   buffer is free: the two 64-byte entry buffers live inside it rather than
   claiming another 128 bytes of a nearly exhausted fixed RAM. */
#define ebuf (chunk)
#define cbuf (chunk + PL_ENTRY)

static void entry_read(unsigned int idx, unsigned char *dst)
{
    unsigned char save = RAM_BANK_REG, i;
    const unsigned char *src;
    RAM_BANK_REG = (unsigned char)(PL_BANK + idx / PL_PER_BANK);
    src = (const unsigned char *)(WIN_BASE + (idx % PL_PER_BANK) * PL_ENTRY);
    for (i = 0; i < PL_ENTRY; i++)
        dst[i] = src[i];
    RAM_BANK_REG = save;
}

/* Non-zero if entry idx sorts after the held element. */
/*
 * Non-zero if entry idx sorts after the held element.
 *
 * Compares straight out of the bank window rather than copying the entry to
 * fixed RAM first. The copy was 64 bytes every time, while almost every
 * comparison is settled by the first character or two -- and this runs once
 * per probe of the binary search, so it was most of what sorting cost.
 *
 * "held" must live in fixed RAM (it is a slice of chunk[]): the bank register
 * moves under our feet here, so anything behind the window would change
 * meaning mid-comparison.
 */
static unsigned char entry_after(unsigned int idx, const unsigned char *held)
{
    unsigned char save = RAM_BANK_REG;
    const unsigned char *src;
    unsigned char i, a, b, r = 0;

    RAM_BANK_REG = (unsigned char)(PL_BANK + idx / PL_PER_BANK);
    src = (const unsigned char *)(WIN_BASE + (idx % PL_PER_BANK) * PL_ENTRY);

    if (src[0] == PL_DIR && held[0] != PL_DIR) {
        r = 0;                                  /* dirs first */
    } else if (src[0] != PL_DIR && held[0] == PL_DIR) {
        r = 1;
    } else {
        for (i = 1; i < PL_ENTRY; i++) {
            a = upcase(src[i]);
            b = upcase(held[i]);
            if (a != b) { r = (unsigned char)(a > b); break; }
            if (!a) break;
        }
    }

    RAM_BANK_REG = save;
    return r;
}

/*
 * The sort order: one 16-bit index per entry, in its own bank.
 *
 * ord_get(display position) -> entry slot. Entries themselves never move.
 */
static unsigned int ord_get(unsigned int i)
{
    unsigned int v;
    unsigned char save = RAM_BANK_REG;
    const unsigned char *p;
    RAM_BANK_REG = pl_ord_bank;
    p = (const unsigned char *)(WIN_BASE + i * 2);
    v = (unsigned int)p[0] | ((unsigned int)p[1] << 8);
    RAM_BANK_REG = save;
    return v;
}

static void ord_set(unsigned int i, unsigned int v)
{
    unsigned char save = RAM_BANK_REG;
    unsigned char *p;
    RAM_BANK_REG = pl_ord_bank;
    p = (unsigned char *)(WIN_BASE + i * 2);
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    RAM_BANK_REG = save;
}

/* Open a gap at lo by shifting [lo, hi) up one slot. The whole array lives in
   one bank, so this is a single memmove -- cc65's is hand-written assembly.
   This is the operation that used to copy a 64-byte entry across the window
   for every step, which is what made sorting a big folder take minutes. */
static void ord_shift_up(unsigned int lo, unsigned int hi)
{
    unsigned char save = RAM_BANK_REG;
    unsigned char *base;
    RAM_BANK_REG = pl_ord_bank;
    base = (unsigned char *)WIN_BASE;
    memmove(base + (lo + 1) * 2, base + lo * 2, (unsigned int)(hi - lo) * 2);
    RAM_BANK_REG = save;
}

/*
 * Binary insertion sort over the index.
 *
 * Measured before this: 100 files 16.0 s, 200 files 58.7 s, 400 files 229.3 s
 * -- 3.8x per doubling, textbook O(n^2), and a 626-file folder extrapolated to
 * about nine minutes. Reading the same 600 entries off the card takes 2.1 s, so
 * essentially all of that was the sort.
 *
 * Two changes, both needed. The binary search takes comparisons from O(n^2) to
 * O(n log n) -- at 2048 entries that is ~22,000 instead of ~2,000,000. And
 * because only the index moves, each of the remaining shifts is a two-byte
 * memmove inside one bank rather than a 64-byte copy through the window.
 *
 * Stable: entry_after is false for equal keys, so an equal run is extended on
 * its right and entries that arrived earlier keep their relative order.
 */
static void playlist_sort(void)
{
    unsigned int i, lo, hi, mid, v;

    for (i = 0; i < n_entries; i++)
        ord_set(i, i);

    for (i = 1; i < n_entries; i++) {
        v = ord_get(i);
        entry_read(v, ebuf);

        lo = 0;
        hi = i;
        while (lo < hi) {
            mid = (lo + hi) >> 1;
            if (entry_after(ord_get(mid), ebuf))
                hi = mid;                       /* belongs at or before mid */
            else
                lo = mid + 1;
        }

        if (lo < i) {
            ord_shift_up(lo, i);
            ord_set(lo, v);
        }
    }
}

unsigned char playlist_error(void)
{
    return scan_err;
}
