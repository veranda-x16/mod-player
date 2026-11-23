/*
 * gui.h -- minimal widget layer for the player.
 *
 * Text mode rather than bitmap: fixed RAM has under 9 KB spare and the mixer
 * wants most of the CPU, while an 80x60 text screen with per-cell colour
 * draws a panel in a few hundred cycles. The mouse pointer is a VERA hardware
 * sprite the KERNAL owns, so it costs nothing to move over the display.
 */
#ifndef GUI_H
#define GUI_H

#include <stdint.h>

typedef struct {
    unsigned char x, y, w;      /* cell position, width; height is always 1 */
    const char   *label;
} gui_button_t;

/* Hit test in CELL coordinates. Returns the button index or -1. */
int gui_hit(const gui_button_t *b, unsigned char n,
            unsigned char cx, unsigned char cy);

void gui_draw_button(const gui_button_t *b, unsigned char active);
void gui_box(unsigned char x, unsigned char y, unsigned char w,
             unsigned char h, const char *title);
void gui_hline(unsigned char x, unsigned char y, unsigned char w);

#endif
