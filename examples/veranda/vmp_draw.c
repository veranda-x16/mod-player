/*
 * vmp_draw.c -- the Veranda MOD Player's drawing helpers, in overlay 3
 * with the UI (vmp_ui.c, their only user; vmp.h). Text goes to the kernel from sb (main
 * RAM).
 *
 * MIT licence (see LICENSE).
 */
#include "vmp.h"

#pragma code-name ("OVERLAY3")          /* (only vmp_ui.c uses them) */

/*---------------------------------------------------------------
  drawing helpers
 ---------------------------------------------------------------*/
void clear(unsigned int l, unsigned int t, unsigned int r, unsigned int b)
{
    SetPattern(0);
    WRectangle(l, t, r, b);
}

unsigned int strw(const char *s)
{
    unsigned int w = 0;
    unsigned char b = currentMode & SET_BOLD ? 1 : 0;
    while (*s)
        w += GetCharWidth(*s++) + b;
    return w;
}

/* s into sb, cut to w pixels ("..." at the end if it was cut) */
void fit(const char *s, unsigned int w)
{
    unsigned char i = 0, j;
    unsigned char b = currentMode & SET_BOLD ? 1 : 0;
    unsigned int x = 0, dots = 3 * (GetCharWidth('.') + b);
    while (s[i] && i < 70) {
        sb[i] = s[i];
        x += GetCharWidth(s[i]) + b;
        ++i;
    }
    sb[i] = 0;
    if (x <= w)
        return;
    while (i && x + dots > w) {
        --i;
        x -= GetCharWidth(sb[i]) + b;
    }
    for (j = 0; j < 3; ++j)
        sb[i++] = '.';
    sb[i] = 0;
}

/* s at (x, baseline y), cut to w pixels, the area cleared first */
void field_text(const char *s, unsigned int x, unsigned int y, unsigned int w, unsigned char bold)
{
    currentMode = bold ? SET_BOLD : 0;
    clear(x, y - 7, x + w - 1, y + 2);
    fit(s, w);
    WPutString(sb, x, y);
    currentMode = 0;
}

void label(const char *s, unsigned int x, unsigned int y)
{
    strcpy(sb, s);
    WPutString(sb, x, y);
}

/* v as decimal, no padding, into p; returns the end */
char *dec(char *p, unsigned int v)
{
    char t[6];
    unsigned char n = 0;
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        *p++ = t[--n];
    *p = 0;
    return p;
}

/* m:ss */
char *mmss(char *p, unsigned int s)
{
    p = dec(p, s / 60);
    *p++ = ':';
    *p++ = (char)('0' + (s % 60) / 10);
    *p++ = (char)('0' + s % 10);
    *p = 0;
    return p;
}

