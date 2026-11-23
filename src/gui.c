/*
 * gui.c -- widget drawing and hit testing.
 *
 * Kept free of any mouse or player state so the hit-test logic can be unit
 * tested natively (tests/test_gui.c) -- clicking cannot be automated in the
 * headless emulator, so the arithmetic is verified on the host instead.
 */
#include "gui.h"

#ifdef __CC65__
#include <conio.h>
#endif

int gui_hit(const gui_button_t *b, unsigned char n,
            unsigned char cx, unsigned char cy)
{
    unsigned char i;
    for (i = 0; i < n; i++) {
        if (cy == b[i].y && cx >= b[i].x && cx < (unsigned char)(b[i].x + b[i].w))
            return (int)i;
    }
    return -1;
}

#ifdef __CC65__

void gui_hline(unsigned char x, unsigned char y, unsigned char w)
{
    unsigned char i;
    gotoxy(x, y);
    for (i = 0; i < w; i++)
        cputc('-');
}

void gui_draw_button(const gui_button_t *b, unsigned char active)
{
    unsigned char i, len = 0;
    const char *p = b->label;
    while (*p++) len++;

    gotoxy(b->x, b->y);
    revers(active ? 0 : 1);
    cputc(' ');
    cputs(b->label);
    for (i = (unsigned char)(len + 1); i < b->w - 1; i++)
        cputc(' ');
    cputc(' ');
    revers(0);
}

void gui_box(unsigned char x, unsigned char y, unsigned char w,
             unsigned char h, const char *title)
{
    unsigned char i;
    gui_hline(x, y, w);
    gui_hline(x, (unsigned char)(y + h - 1), w);
    for (i = 1; i < h - 1; i++) {
        gotoxy(x, (unsigned char)(y + i));
        cputc('|');
        gotoxy((unsigned char)(x + w - 1), (unsigned char)(y + i));
        cputc('|');
    }
    if (title) {
        gotoxy((unsigned char)(x + 2), y);
        cputc(' ');
        cputs(title);
        cputc(' ');
    }
}

#endif
