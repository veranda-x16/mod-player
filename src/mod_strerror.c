/*
 * mod_strerror.c -- the stand-alone player's error texts.
 *
 * Kept out of the library: cc65 compiles string literals for the target's
 * character set (PETSCII for -t cx16), so a library object holding them would
 * print wrong under a host with another one. A host prints its own.
 */
#include "x16mod.h"

const char *mod_strerror(mod_err_t e)
{
    switch (e) {
    case MOD_OK:            return "ok";
    case MOD_ERR_OPEN:      return "cannot open file";
    case MOD_ERR_SHORT:     return "file too short for a MOD header";
    case MOD_ERR_SIGNATURE: return "not a supported 4-channel 31-sample MOD";
    case MOD_ERR_ORDER:     return "bad song length";
    case MOD_ERR_NOMEM:     return "not enough banked RAM";
    }
    return "unknown error";
}
