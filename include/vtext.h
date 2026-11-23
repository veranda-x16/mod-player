/*
 * vtext.h -- direct VERA text output (src/vtext.s).
 *
 * conio's cputc costs ~110 cycles a character and cprintf thousands a call.
 * These write straight to the text screen's VRAM, one address setup per
 * call and a few dozen cycles a cell, which is what makes redrawing the
 * pattern view affordable while the mixer is feeding the PCM FIFO.
 *
 * Position, colour and width are globals (see src/mixer_api.s for why); the
 * one argument is passed fastcall. Characters are PETSCII, as cc65 produces
 * for C literals, converted exactly as conio converts them. Every call
 * advances vt_x by the cells it wrote, so consecutive calls run along a line.
 */
#ifndef VTEXT_H
#define VTEXT_H

extern unsigned char vt_x, vt_y;        /* cell position */
extern unsigned char vt_col;            /* fg | bg << 4, as conio's colour byte */
extern unsigned char vt_rvs;            /* VT_RVS for reverse video, else 0 */
extern unsigned char vt_w;              /* width for vt_pad / vt_fill / vt_write / vt_blit */
extern const unsigned char *vt_cols;    /* per-cell colours for vt_blit */

#define VT_RVS 0x80

void __fastcall__ vt_put(char c);           /* one character */
void __fastcall__ vt_puts(const char *s);   /* NUL-terminated string */
void __fastcall__ vt_pad(const char *s);    /* vt_w cells: s then spaces; s is CUT at vt_w */
void __fastcall__ vt_fill(char c);          /* vt_w copies of c */
void __fastcall__ vt_write(const char *s);  /* vt_w characters, one colour */
void __fastcall__ vt_blit(const char *s);   /* vt_w characters, colours from vt_cols[] */

#endif
