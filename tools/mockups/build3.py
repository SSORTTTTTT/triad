# The combination Thomas picked (2026-10-26): the "next to SAVE" layout (its grid, header, toolbar and the MIX AND
# SMOOTHING value boxes with their little meters), with option C's preset area (category menu, FAVORITES, search,
# pages, 10 named shapes); the category menu lists option B's categories, LFO SHAPES opening to the side.
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from build import C, W, H, box, text, button, panel, tile, header, grid, generate, page_nav, page, FOLDER0, FAV, gear
from build2 import mix_box, toolbar2, CURSOR

CATS = [('&#9829; FAVORITES', '12'), ('USER', '4'), None, ('OCTAVE DOWN', '2,000'), ('OCTAVE UP', '2,000'), ('REVERSE', '2,000'),
        ('CHOPS', '2,000'), ('PITCH + CHOPS', '2,000'), None, ('LFO SHAPES', '&#9656;')]
LFO = ['BASIC', 'SQUARES', 'STEPS', 'TRIANGLES', 'ANGLES', 'HARD + CURVE', 'SOFT']

def menu(x, bottom, hover, lfo_open):
    """The category menu, opening upwards from the category button (its bottom edge at `bottom`)."""
    row, sep, pad, w = 27, 9, 7, 236
    h = pad * 2 + sum(sep if c is None else row for c in CATS)
    y = bottom - h
    out = box(x, y, w, h, '', f'background:#2d2147;border:1px solid rgba(196,178,255,0.35);border-radius:10px;'
              'box-shadow:0 12px 32px rgba(0,0,0,0.55);box-sizing:border-box;')
    yy = y + pad
    hover_xy = None
    lfo_row_y = None
    for c in CATS:
        if c is None:
            out += box(x + 12, yy + sep / 2, w - 24, 1, '', 'background:rgba(255,255,255,0.10);')
            yy += sep
            continue
        lab, n = c
        sel, hov = lab == 'OCTAVE DOWN', lab == hover
        if sel or hov:
            out += box(x + 5, yy + 1, w - 10, row - 2, '', f'background:{C["lav"] if sel else "rgba(196,178,255,0.18)"};border-radius:6px;')
        col = '#1b1030' if sel else C['text']
        out += text(x + 16, yy + 7, ('&#10003;&nbsp; ' if sel else '') + lab, 12, col)
        out += text(x + w - 64, yy + 8, n, 10.5 if n != '&#9656;' else 12, '#1b1030' if sel else C['dim'], 400 if n != '&#9656;' else 700,
                    'width:48px;text-align:right;')
        if hov: hover_xy = (x + w - 60, yy + 12)
        if lab == 'LFO SHAPES': lfo_row_y = yy
        yy += row
    if lfo_open:
        sw, sh = 168, pad * 2 + row * len(LFO)
        sx, sy = x + w - 4, lfo_row_y + row - sh + pad
        out += box(sx, sy, sw, sh, '', f'background:#2d2147;border:1px solid rgba(196,178,255,0.35);border-radius:10px;'
                   'box-shadow:0 12px 32px rgba(0,0,0,0.55);box-sizing:border-box;')
        for i, lab in enumerate(LFO):
            ry = sy + pad + i * row
            if lab == 'TRIANGLES':
                out += box(sx + 5, ry + 1, sw - 10, row - 2, '', 'background:rgba(196,178,255,0.18);border-radius:6px;')
                hover_xy = (sx + 110, ry + 12)
            out += text(sx + 16, ry + 7, lab, 12, C['text'])
    if hover_xy:
        out += CURSOR.format(x=hover_xy[0], y=hover_xy[1])
    return out

def presets_c(bx, by, bw, bh, open_menu=False):
    out = panel(bx, by, bw, bh)
    pressed = open_menu
    out += button(bx + 12, by + 12, 170, 28, 'OCTAVE DOWN &nbsp;' + ('&#9652;' if pressed else '&#9662;'),
                  C['tileHi'] if pressed else C['tile'], C['text'], 12, 7, C['lavLight'] if pressed else None)
    out += button(bx + 188, by + 12, 122, 28, '&#9829; FAVORITES', C['tile'], C['text'], 11)
    out += box(bx + 318, by + 12, 220, 28, '&#128269;&nbsp; Search patterns', f'background:#1b1329;border-radius:7px;display:flex;align-items:center;'
               f'padding-left:10px;box-sizing:border-box;font-size:11.5px;color:{C["dim"]};')
    out += page_nav(bx + bw - 138, by + 13)
    tw, th = (bw - 24 - 4 * 10) / 5, (bh - 50 - 12 - 6) / 2
    for k in range(10):
        r, c = divmod(k, 5)
        out += tile(bx + 12 + c * (tw + 10), by + 50 + r * (th + 6), tw, th, FOLDER0[k], selected=(k == 2), fav=(k in FAV), name=True)
    return out

def combined(state):
    out = header(hints=False)
    out += text(636, 18, 'Double-click: add point &middot; Drag: move &middot; Double-click a point: delete', 10.5, C['dim'], 400)
    out += text(636, 36, 'Drag the small dot: curve &middot; Right-click: more', 10.5, C['dim'], 400)
    out += gear(1058, 22, 22)
    out += grid(16, 72, 1068, 370)                       # the grid of the "next to SAVE" version
    tb, left_end, right_start = toolbar2(458, compact=True, tip=(state == 'closed'))
    out += tb
    out += mix_box(left_end + 10, 448, right_start - left_end - 20, 56, 'values')
    out += presets_c(16, 512, 748, 232, open_menu=(state != 'closed'))
    out += generate(776, 512, 308, 232)
    if state != 'closed':
        out += menu(28, 520, 'OCTAVE UP' if state == 'menu' else 'LFO SHAPES', state == 'lfo')
    return out

if __name__ == '__main__':
    from playwright.sync_api import sync_playwright
    out_dir = sys.argv[1]
    shots = [('1 - closed', 'closed'), ('2 - category menu open', 'menu'), ('3 - LFO SHAPES to the side', 'lfo')]
    with sync_playwright() as p:
        browser = p.chromium.launch()
        pg = browser.new_page(viewport={'width': W, 'height': H}, device_scale_factor=2)
        for name, state in shots:
            html = os.path.join(out_dir, name + '.html')
            open(html, 'w').write(page(combined(state)))
            pg.goto('file://' + html)
            pg.wait_for_timeout(200)
            pg.locator('#win').screenshot(path=os.path.join(out_dir, name + '.png'))
        browser.close()
    print('ok')
