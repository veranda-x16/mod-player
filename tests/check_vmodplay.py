#!/usr/bin/env python3
"""
The Veranda MOD Player's screen, from an x16emu VRAM dump, against a
reviewed golden picture.

    check_vmodplay.py <rom.bin> <vram2png.py> <dump.bin> <shot> <golden.png>
                      [--update]

The dump is composed as VERA shows it (the SDK's vram2png.py: the bitmap
over the colour map) and compared with tests/golden/vmodplay/<shot>.png
pixel for pixel, except where the picture depends on the music's timing
(the emulator's audio runs on the host's clock, so two runs differ there):

  - the values of position, pattern, row, speed and BPM, the time and the
    progress bar's cards; the menu bar's clock (Veranda's kernel draws the
    date and time at the bar's right: the emulated minutes);
  - the VU meters' segments: checked instead for their form: each
    segment unlit (chrome) or in its zone's colour (green to -12 dB,
    yellow to -4 dB, red), lit from the left with at most one held peak
    beyond (under a dialog box or a menu: the normal paper or the box's
    shadow there); dark when nothing plays, some lit while it plays;
  - in the pattern view the pattern's rows: read back card for card in the
    ROM's 8 x 8 font (the program writes them with PutCards) and checked
    for their form: the row numbers in order (00-1F or 20-3F), each
    channel's cell a note (C-1 .. B-3 or ---), an instrument (two hex
    digits or ..), an effect (three hex digits); the current row's band
    of colour (the selection blue) on one row.

The colours are Veranda Look 2's palette entries (Veranda's sdk/lib/theme.h,
DECISIONS V74): chrome 2, select 4, yellow 6, red 7, green 8.

    check_vmodplay.py --look <vram2png.py> <dump.bin>

checks Veranda Look 2's look instead (DECISIONS V74, V77, V79): palette
entries 0-15 Look 2's light ones (no settings file: the theme Veranda);
the kernel's menu bar (card rows 0-1: the mark's two cards cyan, the rest
chrome; the mark's and the name's ink; its line at y 15); the window's
chrome (vrwin.h: the title band, card rows 3-4, on the accent, its line at
y 39 and the close glyph at its right; the status bar, rows 57-58, chrome,
its line at y 456; the frame's sides; the dither shadow right of it); the
desk's colour round the window; the panels' chrome heads; the meters'
unlit segments chrome; the body paper.

--update writes the composed picture as the golden one (to be reviewed).
"""
import importlib.util
import re
import sys

VU_COL, VU_W, VU_ROW0 = 5, 72, 44
PROG = (352, 176, 615, 183)
MASKS = [
    (416, 105, 479, 133),       # position, row
    (552, 105, 615, 147),       # pattern, speed, BPM
    (416, 155, 615, 167),       # the time
    PROG,
    (VU_COL * 8, VU_ROW0 * 8, (VU_COL + VU_W) * 8 - 1, (VU_ROW0 + 11) * 8 - 1),
    (496, 0, 639, 14),          # the bar's clock (the kernel's: V74)
]
PATTERN_MASK = (16, 72, 327, 327)
C_OFF, C_GREEN, C_AMBER, C_RED, C_ROW = 2, 8, 6, 7, 4      # (Look 2: chrome, green, yellow, red, select)
LOOK2 = [0xFFF, 0x223, 0xDDE, 0xEEF, 0x9CF, 0xACE, 0xFD5, 0xF87,
         0x8D9, 0xB9F, 0xFA5, 0x6CE, 0x36A, 0x8BE, 0x5A7, 0xDAC]
CHROME, TITLE, CYAN, SKY2 = 2, 5, 11, 13
WIN_X, WIN_Y, WIN_W, WIN_H = 1, 3, 78, 56
BOXES = ('open', 'openroot', 'about', 'menu')


def rom_font(rom):
    """glyphs 32-127 of the ROM's CP437 set (bank 6, the 2 KB block whose
    glyph 1 is the smiley), as the player copies them"""
    smiley = bytes([0x7E, 0x81, 0xA5, 0x81, 0xBD, 0x99, 0x81, 0x7E])
    bank = rom[6 * 16384:7 * 16384]
    for a in range(0, 16384, 2048):
        if bank[a + 8:a + 16] == smiley:
            return bank[a:a + 2048]
    return bank[0x800:0x1000]


def glyph_table(font):
    t = {}
    for c in range(32, 127):
        g = font[8 * c:8 * c + 8]
        bold = bytes((b | (b >> 1)) & 0xFF for b in g)
        for v in (g, bold):
            t.setdefault(bytes(v), chr(c))
    t[bytes(8)] = ' '
    return t


def card_text(v, table, cy, cx0, n):
    s = ''
    for cx in range(cx0, cx0 + n):
        g = bytes(v[(cy * 8 + r) * 80 + cx] for r in range(8))
        s += table.get(g, '\x7f')
    return s


def zone(i):
    return C_GREEN if i < 54 else C_AMBER if i < 66 else C_RED


def look(tool, dump):
    """Veranda Look 2's bar and window (--look)"""
    spec = importlib.util.spec_from_file_location('vram2png', tool)
    vp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(vp)
    v = vp.section(dump)
    img = vp.compose(v)
    pal = [vp.rgb(v, i) for i in range(16)]
    bad = []

    def want(what, got, exp):
        if got != exp:
            bad.append('%s: %r, want %r' % (what, got, exp))

    def rgb12(c):
        return '%X%X%X' % tuple(int(x) * 15 // 255 for x in c)

    want('the palette', [rgb12(c) for c in pal], ['%03X' % c for c in LOOK2])
    colour = {tuple(c): i for i, c in reversed(list(enumerate(pal)))}

    def paper(cx, cy):
        """a card's paper: the colour of its pixels that are not ink"""
        ink = tuple(pal[1])
        for yy in range(8):
            for xx in range(8):
                c = tuple(img[cy * 8 + yy][cx * 8 + xx])
                if c != ink:
                    return colour.get(c)
        return None

    def cards(x0, x1, y0, y1):
        return {paper(x, y) for x in range(x0, x1 + 1) for y in range(y0, y1 + 1)} - {None}

    def inked(x0, x1, y0, y1):
        ink = tuple(pal[1])
        return sum(1 for y in range(y0, y1) for x in range(x0 * 8, x1 * 8 + 8) if tuple(img[y][x]) == ink)

    r, b = WIN_X + WIN_W - 1, WIN_Y + WIN_H - 1
    want('the bar\'s mark (cards 0-1, rows 0-1)', cards(0, 1, 0, 1), {CYAN})
    want('the bar (cards 2-79, rows 0-1)', cards(2, 79, 0, 1), {CHROME})
    want('the bar\'s line (y 15)', inked(0, 79, 15, 16), 640)
    if inked(0, 1, 2, 14) < 20:
        bad.append('no Veranda mark in the bar')
    if inked(3, 12, 2, 14) < 60:
        bad.append('no program name in the bar')
    want('the desk (column 0, rows 2-59)', cards(0, 0, 2, 59), {SKY2})
    want('the desk (column 79, rows 2-59)', cards(79, 79, 2, 59), {SKY2})
    want('the title band (rows 3-4)', cards(WIN_X, r, WIN_Y, WIN_Y + 1), {TITLE})
    want('the title band\'s line (y 39)', inked(WIN_X, r, 39, 40), 8 * WIN_W)
    want('the status bar (rows 57-58)', cards(WIN_X, r, b - 1, b), {CHROME})
    want('the status bar\'s line (y 456)', inked(WIN_X, r, 456, 457), 8 * WIN_W)
    want('the panels\' heads (rows 6-7)', cards(2, 41, 6, 7) | cards(43, 77, 6, 7), {CHROME})
    want('the body (card 30, row 30)', paper(30, 30), 0)
    if inked(r - 1, r, 8 * WIN_Y + 4, 8 * WIN_Y + 12) < 10:
        bad.append('no close glyph at the title band\'s right')
    sides = (sum(1 for y in range(8 * WIN_Y, 8 * b + 8) if tuple(img[y][8 * WIN_X]) == tuple(pal[1])),
             sum(1 for y in range(8 * WIN_Y, 8 * b + 8) if tuple(img[y][8 * r + 7]) == tuple(pal[1])))
    want('the frame\'s sides (ink rows)', sides, (8 * WIN_H, 8 * WIN_H))
    sh = inked(r + 1, r + 1, 8 * WIN_Y + 8, 8 * b + 8)
    if not 300 < sh < 1400:
        bad.append('the shadow right of the window: %d ink pixels' % sh)
    for f in bad:
        print('FAIL  look: %s' % f)
    if bad:
        return 1
    print('ok    the look: Look 2\'s palette, the menu bar (mark, name, chrome), the title band, the close glyph, '
          'the status bar, the frame, the shadow, the desk, the panels\' heads')
    return 0


def main():
    if sys.argv[1] == '--look':
        return look(sys.argv[2], sys.argv[3])
    rom, tool, dump, shot, golden = sys.argv[1:6]
    spec = importlib.util.spec_from_file_location('vram2png', tool)
    vp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(vp)
    v = vp.section(dump)
    img = vp.compose(v)
    pal = {i: tuple(vp.rgb(v, i)) for i in range(16)}
    fails = []

    masks = list(MASKS)
    if shot == 'pattern':
        masks.append(PATTERN_MASK)

    def masked(x, y):
        return any(a <= x <= c and b <= y <= d for a, b, c, d in masks)

    from PIL import Image
    if '--update' in sys.argv:
        out = Image.new('RGB', (640, 480))
        out.putdata([tuple(img[y][x]) for y in range(480) for x in range(640)])
        out.save(golden)
        print('      %s: golden written (%s)' % (shot, golden))
    g = Image.open(golden).convert('RGB').load()
    bad, box = 0, None
    for y in range(480):
        for x in range(640):
            if g[x, y] != tuple(img[y][x]) and not masked(x, y):
                bad += 1
                box = (min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y)) if box else (x, y, x, y)
    if bad:
        fails.append('%d pixels differ from the golden picture, in %s' % (bad, box))

    # the VU meters: a paper pixel in the middle of each segment's card
    lit_total = 0
    for k in range(4):
        y = (VU_ROW0 + 3 * k) * 8 + 4
        segs = []
        for i in range(VU_W):
            c = tuple(img[y][(VU_COL + i) * 8 + 4])
            if c == pal[C_OFF]:
                segs.append(0)
            elif c == pal[zone(i)]:
                segs.append(1)
            elif shot in BOXES and c in (pal[0], pal[1]):
                segs.append(None)       # under the box or its shadow (normal paper: V33)
            else:
                fails.append('VU %d segment %d: colour %s (neither unlit nor its zone\'s)' % (k + 1, i, c))
                segs.append(1)
        if None not in segs:
            n = 0
            while n < VU_W and segs[n]:
                n += 1
            beyond = sum(segs[n:])
            if beyond > 1:
                fails.append('VU %d: %d segments lit after the bar\'s end (one held peak at most)' % (k + 1, beyond))
        lit_total += sum(1 for x in segs if x)
    if shot in ('root', 'folder') and lit_total:
        fails.append('%d VU segments lit with nothing playing' % lit_total)
    if shot in ('playing', 'pattern', 'list', 'menu') and not lit_total:
        fails.append('the VU meters are dark while it plays')

    if shot == 'pattern':
        table = glyph_table(rom_font(open(rom, 'rb').read()))
        cell = re.compile(r'^(---|[A-G][-#][1-3])([0-9A-F]{2}|\.\.)[0-9A-F]{3} $')
        first = None
        for r in range(32):
            line = card_text(v, table, 9 + r, 2, 39)
            try:
                n = int(line[:2], 16)
            except ValueError:
                fails.append('pattern row %d reads "%s"' % (r, line))
                break
            if first is None:
                first = n
            if n != first + r or first not in (0, 32):
                fails.append('pattern row %d is numbered %02X' % (r, n))
                break
            for c in range(4):
                if not cell.match(line[3 + 9 * c:12 + 9 * c]):
                    fails.append('pattern row %d channel %d reads "%s"' % (r, c + 1, line[3 + 9 * c:12 + 9 * c]))
                    break
        bands = [r for r in range(32)
                 if any(tuple(img[(9 + r) * 8 + yy][x]) == pal[C_ROW] for yy in range(8) for x in range(16, 328))]
        if len(bands) != 1:
            fails.append('the pattern\'s current row: %d bands of colour' % len(bands))

    for f in fails:
        print('FAIL  %s: %s' % (shot, f))
    if fails:
        return 1
    print('ok    %s: as the golden picture (outside the timing\'s fields); %d VU segments lit, in their zones'
          % (shot, lit_total))
    return 0


if __name__ == '__main__':
    sys.exit(main())
