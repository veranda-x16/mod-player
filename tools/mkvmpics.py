#!/usr/bin/env python3
"""The Veranda MOD Player's pictures (examples/veranda/vmp_pics.h).

    tools/mkvmpics.py [--png out.png]

Writes examples/veranda/vmp_pics.c and vmp_pics.h: GEOS compacted bitmaps (BitmapUp's format: a count of 1-127
repeats the next byte, $80 + n is n literal bytes) as C arrays:

  the transport buttons, 4 cards by 24 rows: previous, play, pause, stop,
      next (DoIcons; Veranda's own system icons' style, tools/mkicons.py
      in the Veranda tree: a frame with the corners cut and a shadow line
      inside the bottom)
  the "Play all" button of the open box, 6 cards by 16 rows, in Veranda's
      system font (its tools/mkfont.py, from VERANDA, default
      ~/Projects/geos)
  8 x 8 marks for the lists: a folder, a module (a note), "up" (..), the
      playing track; the scroll bar's arrows
  the application's icon (vmodplay.pbm, 3 cards by 21 rows), for the
      about box

MIT licence (see LICENSE).
"""
import os
import sys

VERANDA = os.environ.get('VERANDA', os.path.expanduser('~/Projects/geos'))
sys.path.insert(0, os.path.join(VERANDA, 'tools'))
import mkfont  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'examples', 'veranda', 'vmp_pics.c')


def frame(w, h):
    px = [[0] * w for _ in range(h)]
    for x in range(2, w - 2):
        px[0][x] = px[h - 1][x] = 1
    for y in range(2, h - 2):
        px[y][0] = px[y][w - 1] = 1
    px[1][1] = px[1][w - 2] = px[h - 2][1] = px[h - 2][w - 2] = 1
    for x in range(3, w - 3):
        px[h - 2][x] = 1
    return px


def stamp(px, art, x0, y0):
    for y, row in enumerate(art):
        for x, c in enumerate(row):
            if c == '#':
                px[y0 + y][x0 + x] = 1


def art(*rows):
    return list(rows)


TRI = art(               # play: 7 wide, 11 high
    '#......',
    '##.....',
    '###....',
    '####...',
    '#####..',
    '######.',
    '#####..',
    '####...',
    '###....',
    '##.....',
    '#......')
TRI_L = [r[::-1] for r in TRI]
SYMBOLS = {
    'prev': [('##.', 0), (TRI_L, 3), (TRI_L, 9)],
    'play': [(TRI, 0)],
    'pause': [('###', 0), ('###', 6)],
    'stop': [('#########', 0)],
    'next': [(TRI, 0), (TRI, 6), ('##.', 13)],
}


def button(name):
    w, h = 32, 24
    px = frame(w, h)
    parts = SYMBOLS[name]
    width = 0
    for a, x in parts:
        cols = len(a) if isinstance(a, str) else len(a[0])
        width = max(width, x + cols)
    x0 = (w - width) // 2
    y0 = (h - 1 - 11) // 2
    for a, x in parts:
        if isinstance(a, str):          # a bar: the string's # columns, 11 high
            stamp(px, [a.replace('.', '.')] * (9 if name == 'stop' else 11), x0 + x, y0 + (1 if name == 'stop' else 0))
        else:
            stamp(px, a, x0 + x, y0)
    return px


def word_button(word, w=48, h=16):
    px = frame(w, h)
    width = sum(len(mkfont.G[c][0]) for c in word) - 1
    x0, top = (w - width) // 2, 3
    for c in word:
        g = mkfont.G[c]
        for r in range(mkfont.H):
            for i, ch in enumerate(g[r]):
                if ch == '#' and top + r < h - 2:
                    px[top + r][x0 + i] = 1
        x0 += len(g[0])
    return px


MARKS = {
    'folder': art(
        '........',
        '.###....',
        '#...####',
        '#......#',
        '#......#',
        '#......#',
        '########',
        '........'),
    'note': art(
        '....##..',
        '....#.#.',
        '....#..#',
        '....#...',
        '..###...',
        '.####...',
        '..##....',
        '........'),
    'up': art(
        '...#....',
        '..###...',
        '.#.#.#..',
        '...#....',
        '...#....',
        '...####.',
        '........',
        '........'),
    'playing': art(
        '.#......',
        '.##.....',
        '.###....',
        '.####...',
        '.###....',
        '.##.....',
        '.#......',
        '........'),
    'arrow_up': art(
        '########',
        '#......#',
        '#..##..#',
        '#.####.#',
        '#......#',
        '#......#',
        '#......#',
        '########'),
    'arrow_down': art(
        '########',
        '#......#',
        '#......#',
        '#......#',
        '#.####.#',
        '#..##..#',
        '#......#',
        '########'),
}


def to_px(a):
    return [[1 if c == '#' else 0 for c in row] for row in a]


def pack(px):
    h, w = len(px), len(px[0])
    data = [sum(px[y][x * 8 + b] << (7 - b) for b in range(8)) for y in range(h) for x in range(w // 8)]
    out, i = [], 0
    while i < len(data):
        j = i
        while j < len(data) and data[j] == data[i] and j - i < 127:
            j += 1
        if j - i >= 3:
            out += [j - i, data[i]]
            i = j
            continue
        k = i
        while k < len(data) and k - i < 91:
            if k + 2 < len(data) and data[k] == data[k + 1] == data[k + 2]:
                break
            k += 1
        out += [0x80 + k - i] + data[i:k]
        i = k
    return out


def pbm(path):
    t = open(path).read().split()
    assert t[0] == 'P1'
    w, h = int(t[1]), int(t[2])
    v = [int(c) for c in t[3:3 + w * h]]
    return [v[y * w:(y + 1) * w] for y in range(h)]


def main():
    pics = [('pic_' + n, button(n)) for n in ('prev', 'play', 'pause', 'stop', 'next')]
    pics += [('pic_' + n, to_px(a)) for n, a in MARKS.items()]
    pics.append(('pic_app', pbm(os.path.join(os.path.dirname(OUT), 'vmodplay.pbm'))))
    head = ['/* generated by tools/mkvmpics.py: the MOD Player\'s pictures, GEOS',
            '   compacted bitmaps (BitmapUp), in main RAM. Do not edit. */']
    lines, decls = list(head), list(head) + ['#ifndef VMP_PICS_H', '#define VMP_PICS_H']
    for name, px in pics:
        b = pack(px)
        decls.append('extern const unsigned char %s[%d];    /* %d x %d */' % (name, len(b), len(px[0]), len(px)))
        lines.append('const unsigned char %s[%d] = {' % (name, len(b)))
        for i in range(0, len(b), 16):
            lines.append('    ' + ', '.join('0x%02X' % v for v in b[i:i + 16]) + ',')
        lines.append('};')
    decls.append('#endif')
    open(OUT, 'w').write('\n'.join(lines) + '\n')
    open(OUT[:-1] + 'h', 'w').write('\n'.join(decls) + '\n')
    if '--png' in sys.argv:
        from PIL import Image
        W = max(len(p[0]) for _, p in pics)
        H = sum(len(p) + 4 for _, p in pics)
        img = Image.new('1', (W, H), 1)
        y0 = 0
        for _, px in pics:
            for y, row in enumerate(px):
                for x, v in enumerate(row):
                    if v:
                        img.putpixel((x, y0 + y), 0)
            y0 += len(px) + 4
        img.resize((W * 4, H * 4)).save(sys.argv[sys.argv.index('--png') + 1])


if __name__ == '__main__':
    main()
