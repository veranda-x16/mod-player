/*
 * mod_x16.c -- the stand-alone player's loader glue: a MOD file read through
 * the KERNAL (src/kread.s) into the library's loader (src/mod_bankload.c).
 *
 * Everything target-specific about loading on a bare X16 is here: the file
 * name, the LOAD channel, MACPTR, and the banks -- from the one after the
 * playlist's (playlist_init sets mod_first_bank) to bank 255.
 */
#include "x16mod.h"
#include "playlist.h"

#include <string.h>

/* src/kread.s -- KERNAL file I/O; see the note there on why not cbm_read. */
extern unsigned char kfile_open(void);
extern void          kfile_read(void);
extern void          kfile_close(void);
extern unsigned int  kf_name;
extern unsigned char kf_namelen;
extern unsigned int  kf_dest;
extern unsigned int  kf_count;
extern unsigned int  kf_read;
extern unsigned char kf_eof;
extern unsigned char kf_sa;
extern unsigned char kf_slow;

/* src/mod_bankload.c */
extern unsigned char mod_first_bank;

#define RAM_BANK_REG (*(volatile unsigned char *)0x0000)
#define LAST_BANK  255

/* The library calls again after a short count; the KERNAL is not asked to
   read past the end it has already reported. */
unsigned int mod_kread(void *dest, unsigned int count)
{
    if (kf_eof)
        return 0;
    kf_dest = (unsigned int)dest;
    kf_count = count;
    kfile_read();
    return kf_read;
}

#define kread mod_kread

/* Open a file for mod_kread, as the loader does: 0 on success. */
unsigned char mod_kopen(const char *filename)
{
    kf_name = (unsigned int)filename;
    kf_namelen = (unsigned char)strlen(filename);
    kf_sa = 0;
    kf_slow = 0;
    return kfile_open();
}

void mod_kclose(void)
{
    kfile_close();
}

int mod_last_read = 0;          /* diagnostic: what the header read returned */

mod_err_t mod_load_file(const char *filename, mod_info_t *m, unsigned char *hdrbuf)
{
    mod_err_t e;

    /* 0 = KERNAL/DOS, then the playlist banks, then sample data. A build
       without a playlist (the trace and chunk harnesses) starts clear of
       where one would have been. */
    if (mod_first_bank == 0)
        mod_first_bank = PL_BANK_END_MAX;

    kf_name = (unsigned int)filename;
    kf_namelen = (unsigned char)strlen(filename);
    /*
     * Open exactly the way the directory read does: the LOAD channel
     * (secondary address 0). Secondary address 2 hung on a real CMDR-DOS
     * device. -DSLOW_LOAD forces byte-at-a-time reads instead of MACPTR if a
     * device turns out not to handle it.
     */
    kf_sa = 0;
#ifdef SLOW_LOAD
    kf_slow = 1;
#else
    kf_slow = 0;
#endif
    if (kfile_open() != 0) {
        kf_slow = 0;
        return MOD_ERR_OPEN;
    }

    e = mod_open(kread, m, hdrbuf);
    mod_last_read = (e == MOD_ERR_SHORT) ? 0 : MOD_HEADER_SIZE;
    if (e == MOD_OK)
        e = mod_load(kread, m, mod_first_bank,
                     (unsigned char)(LAST_BANK - mod_first_bank + 1));

    kfile_close();
    kf_slow = 0;
    /* The stand-alone player has always left the module's first bank mapped
       after a load; the library restores the caller's, this keeps the old
       behaviour for the code that grew up with it. */
    if (e == MOD_OK)
        RAM_BANK_REG = mod_first_bank;
    return e;
}
