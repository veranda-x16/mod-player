/*
 * playlist.h -- module list built by scanning the disk directory.
 *
 * Entries live in banked RAM, not fixed RAM: a few hundred real filenames is
 * tens of kilobytes and fixed RAM has under 9 KB spare, while banked RAM has
 * 2 MB and a module uses at most a few hundred KB of it.
 */
#ifndef PLAYLIST_H
#define PLAYLIST_H

#include <stdint.h>

/*
 * Real module filenames run long -- 40+ characters is ordinary -- and the
 * directory returns them in full. An entry has to hold the whole name: a
 * truncated one loses its ".mod" extension (so it is never recognised) and
 * cannot be opened afterwards either.
 */
#define PL_BANK      1      /* entries occupy PL_BANK..PL_BANK+PL_BANKS-1 */
/*
 * 16 banks = 2048 entries. Four (512) then eight (1024) were both too few for
 * real collections -- a folder that ran out partway through the letter M
 * silently lost everything after it, with no way to reach those files:
 * playlist_more() is capped at PL_MAX too, so it just returned 0.
 *
 * Each bank buys 128 more entries for 8 KB of the 2 MB that would otherwise
 * hold sample data. At 16 banks the list costs 128 KB, or 6% of a 2 MB
 * machine -- worth it to see the whole folder. On a 512 KB machine it is a
 * much bigger share, but modules that then do not fit are already reported as
 * too big rather than failing obscurely.
 */
#define PL_BANKS_MAX 16
#define PL_ENTRY    64      /* 1 type byte + up to 62 chars + terminator */
#define PL_PER_BANK (8192 / PL_ENTRY)
#define PL_MAX_CAP  (PL_PER_BANK * PL_BANKS_MAX)   /* 2048 entries */

/*
 * A CBM directory is a sequential stream: entry 51 cannot be reached without
 * reading 1..50 first. So the scan is bounded rather than random-access --
 * it stops once it has enough to fill the visible pages, and playlist_more()
 * extends it when the user scrolls past what is loaded. Extending re-reads
 * from the start, which is still far cheaper than parsing a thousand-entry
 * directory before showing anything.
 */
#define PL_FIRST_BATCH 96             /* ~2 screens; enough to start browsing */

/*
 * First bank NOT used by the playlist -- where module sample data starts.
 * Derived rather than written out again: when these were two independent
 * constants, growing the playlist meant remembering to move the loader too,
 * and forgetting would have had a big module quietly overwrite the file list.
 */
/*
 * One bank holds the sort order: PL_MAX 16-bit indices, 4 KB of the 8.
 *
 * The list is PRESENTED in sorted order but the entries are never moved --
 * sorting permutes this index instead. That is what makes sorting a large
 * folder tractable: a shift moves two bytes inside one bank rather than a
 * 64-byte entry across the window.
 */
#define PL_ORD_BANK_MAX  (PL_BANK + PL_BANKS_MAX)
#define PL_BANK_END_MAX  (PL_ORD_BANK_MAX + 1)

/*
 * Size the list to the machine, and return the first bank left for sample
 * data. Call once, before the first scan.
 *
 * A sixteenth of banked RAM goes to the file list: 4 banks (512 entries) on a
 * 512 KB machine, 16 (2048) on a 2 MB one. A fixed 16 banks would have been
 * 25% of a 512 KB machine's RAM spent on filenames, which is the wrong trade
 * when what is scarce there is room for the module itself.
 */
unsigned char playlist_init(unsigned char ram_banks);
unsigned int  playlist_capacity(void);

/*
 * Shuffle bag: which entries have been heard since the folder was opened, so
 * that shuffle can play everything once before repeating anything. Cleared by
 * every scan.
 */
void          playlist_played_clear(void);
void          playlist_played_set(unsigned int i);
unsigned char playlist_played(unsigned int i);

unsigned int  playlist_scan(void);            /* -> number of entries found */
unsigned int  playlist_scan_limit(unsigned int max_entries);
unsigned int  playlist_more(void);            /* extend; 0 if nothing further */
unsigned char playlist_truncated(void);       /* more entries remain on disk */
unsigned char playlist_error(void);           /* non-zero if the scan failed */
unsigned int  playlist_count(void);

/*
 * Track numbering. The list index is NOT the track number: sorting puts ".."
 * and any subdirectories first, so in a folder with one parent entry the
 * first module sits at index 1. These report position among the MODULES.
 */
unsigned int  playlist_first_file(void);      /* index of the first module */
unsigned int  playlist_file_count(void);      /* modules, excluding folders */
unsigned int  playlist_track_no(unsigned int i);  /* 1-based, 0 if a folder */
const char   *playlist_name(unsigned int i);  /* valid until the next call */
#define PL_FILE  0
#define PL_DIR   1
#define PL_BIG   2      /* a module too large for this machine's RAM */

unsigned char playlist_is_dir(unsigned int i);
unsigned char playlist_type(unsigned int i);
void          playlist_mark(unsigned int i, unsigned char type);

/* Change directory and rescan. Returns the new entry count. */
unsigned int  playlist_chdir(const char *name);

#endif
