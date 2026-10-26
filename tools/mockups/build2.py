# Option B, refined (Thomas, 2026-10-26): MIX AND SMOOTHING shrunk to a small box, in the top-right corner or next to
# SAVE; each with mini knobs or with value boxes; and buttons that move the wave (or a selection) left and right.
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from build import (C, W, H, box, text, button, panel, heart, tile, knob, gear, header, grid, generate, page_nav, page,
                   KNOBS, FOLDER0, FAV)

SHORT = ['TIME', 'VOLUME', 'SMOOTH', 'ATTACK', 'RELEASE']

def mix_box(x, y, w, h, style):
    out = panel(x, y, w, h, radius=9)
    out += text(x + 8, y + 5, 'MIX AND SMOOTHING', 7.5, C['pink'], 800, 'letter-spacing:0.3px;')
    cw = (w - 10) / 5
    for i, (v, lab, col, frac) in enumerate(KNOBS):
        cx = x + 5 + cw * (i + 0.5)
        if style == 'knobs':
            out += knob(cx, y + 30, 23, '', None, col, frac)
            out += box(cx - cw / 2, y + h - 13, cw, 10, SHORT[i], f'text-align:center;font-size:7px;font-weight:700;color:{C["caps"]};')
        else:
            cx0, cy0, cwid, chei = cx - cw / 2 + 2, y + 16, cw - 4, h - 21
            out += box(cx0, cy0, cwid, chei, '', f'background:{C["tile"]};border-radius:5px;')
            out += box(cx0, cy0 + 3, cwid, 9, SHORT[i], f'text-align:center;font-size:6.5px;font-weight:700;color:{C["caps"]};')
            out += box(cx0, cy0 + 13, cwid, 14, v, f'text-align:center;font-size:10.5px;font-weight:800;color:{C["text"]};')
            out += box(cx0 + 4, cy0 + chei - 6, cwid - 8, 3, '', 'background:rgba(255,255,255,0.12);border-radius:2px;')
            out += box(cx0 + 4, cy0 + chei - 6, (cwid - 8) * frac, 3, '', f'background:{col};border-radius:2px;')
    return out

CURSOR = ('<svg style="position:absolute;left:{x}px;top:{y}px" width="16" height="22" viewBox="0 0 16 22">'
          '<path d="M1 1 L1 17 L5 13 L8 20 L11 19 L8 12 L14 12 Z" fill="#ffffff" stroke="#111" stroke-width="1.3"/></svg>')

def toolbar2(y, compact=False):
    """The toolbar with the new pair of buttons that move the wave, right of 1/2; returns (html, left end, right start)."""
    out = button(16, y, 104, 36, '1 Bar') + button(126, y, 34, 36, '&#9664;', size=11) + button(164, y, 34, 36, '&#9654;', size=11)
    out += button(208, y, 112, 36, 'GRID 1/64') + button(326, y, 76, 36, 'SNAP', C['pink'], '#2a0d24') + button(408, y, 76, 36, 'SAVE')
    if compact:
        items = [(32, '&#128465;'), (38, '2x'), (38, '1/2'), (30, 'L'), (30, 'R'), (34, '&#8644;'), (34, '&#8645;'), (32, '&#8630;'), (32, '&#8631;')]
        gap = 5
    else:
        items = [(40, '&#128465;'), (46, '2x'), (46, '1/2'), (36, 'L'), (36, 'R'), (40, '&#8644;'), (40, '&#8645;'), (36, '&#8630;'), (36, '&#8631;')]
        gap = 6
    r = 1084
    places = []
    for w, lab in reversed(items):
        r -= w
        places.append((r, w, lab))
        r -= gap
    shift_left = None
    for x, w, lab in places:
        if lab in ('L', 'R'):
            # Move the wave (or the selection) left or right: a little arrow with a line, unlike the length arrows
            icon = ('<svg width="18" height="14" viewBox="0 0 18 14">' + ('<path d="M2 7 L9 1 L9 13 Z" fill="#f4eefc"/><rect x="10" y="5.5" width="7" height="3" rx="1" fill="#f4eefc"/>' if lab == 'L'
                    else '<path d="M16 7 L9 1 L9 13 Z" fill="#f4eefc"/><rect x="1" y="5.5" width="7" height="3" rx="1" fill="#f4eefc"/>') + '</svg>')
            out += button(x, y, w, 36, icon, C['tileHi'] if lab == 'L' else C['tile'])
            if lab == 'L': shift_left = (x, w)
        else:
            out += button(x, y, w, 36, lab, size=14 if len(lab) > 3 else 13)
    # Hovering the left one: what it does (as ShaperBox's status line), and the pointer
    sx, sw = shift_left
    tip = 'Move wave or selection left. Shift-click for fine adjustment.'
    out += box(sx + sw / 2 - 170, y - 34, 340, 24, tip, 'background:#f4eefc;color:#1b1030;border-radius:6px;font-size:11px;font-weight:700;'
               'display:flex;align-items:center;justify-content:center;box-shadow:0 4px 14px rgba(0,0,0,0.45);')
    out += box(sx + sw / 2 - 5, y - 11, 10, 6, '', 'background:#f4eefc;clip-path:polygon(0 0,100% 0,50% 100%);')
    out += CURSOR.format(x=sx + sw - 9, y=y + 22)
    return out, 484, places[-1][0]

def presets(bx, by, bw, bh):
    out = panel(bx, by, bw, bh)
    out += text(bx + 14, by + 12, 'PRESETS', 12, C['pink'], 800)
    cats = [('&#9829; FAVORITES', '12'), ('USER', '4'), ('OCTAVE DOWN', '2,000'), ('OCTAVE UP', '2,000'), ('REVERSE', '2,000'), ('CHOPS', '2,000'), ('PITCH + CHOPS', '2,000'), ('LFO SHAPES &#9656;', '')]
    step = (bh - 46) / len(cats)
    for i, (lab, n) in enumerate(cats):
        yy = by + 34 + i * step
        sel = lab == 'OCTAVE DOWN'
        out += box(bx + 8, yy, 156, 21, '', f'background:{C["lav"] if sel else "transparent"};border-radius:6px;')
        out += text(bx + 16, yy + 4, lab, 11, '#1b1030' if sel else C['text'])
        out += text(bx + 120, yy + 4, n, 10, '#1b1030' if sel else C['dim'], 400, 'width:36px;text-align:right;')
    out += box(bx + 172, by + 12, 1, bh - 24, '', 'background:rgba(255,255,255,0.08);')
    tx0 = bx + 184
    tw, th = (bw - 184 - 12 - 4 * 8) / 5, (bh - 24 - 8 - 40) / 2
    for k in range(10):
        r, c = divmod(k, 5)
        out += tile(tx0 + c * (tw + 8), by + 12 + r * (th + 8), tw, th, FOLDER0[k], selected=(k == 2), fav=(k in FAV), name=True)
    out += page_nav(tx0 + (bw - 184 - 12) / 2 - 85, by + bh - 36, 'Page 1 of 200', 170)
    return out

HINTS = 'Double-click: add point &middot; Drag: move &middot; Double-click a point: delete &middot; Drag the small dot: curve &middot; Right-click: more'

def corner(style):
    out = header(hints=False)
    out += text(24, 70, HINTS, 10.5, C['dim'], 400)
    out += gear(1058, 12, 22)
    out += mix_box(874, 47, 210, 56, style)          # the rectangle Thomas drew in the top-right corner
    out += grid(16, 108, 1068, 340)
    tb, _, _ = toolbar2(458)
    out += tb
    out += presets(16, 506, 748, 238)
    out += generate(776, 506, 308, 238)
    return out

def by_save(style):
    out = header(hints=False)
    out += text(636, 18, 'Double-click: add point &middot; Drag: move &middot; Double-click a point: delete', 10.5, C['dim'], 400)
    out += text(636, 36, 'Drag the small dot: curve &middot; Right-click: more', 10.5, C['dim'], 400)
    out += gear(1058, 22, 22)
    out += grid(16, 72, 1068, 370)
    tb, left_end, right_start = toolbar2(458, compact=True)
    out += tb
    out += mix_box(left_end + 10, 448, right_start - left_end - 20, 56, style)   # the rectangle next to SAVE
    out += presets(16, 512, 748, 232)
    out += generate(776, 512, 308, 232)
    return out

if __name__ == '__main__':
    from playwright.sync_api import sync_playwright
    out_dir = sys.argv[1]
    shots = [('B corner - mini knobs', lambda: corner('knobs')), ('B corner - value boxes', lambda: corner('values')),
             ('B next to SAVE - mini knobs', lambda: by_save('knobs')), ('B next to SAVE - value boxes', lambda: by_save('values'))]
    with sync_playwright() as p:
        browser = p.chromium.launch()
        pg = browser.new_page(viewport={'width': W, 'height': H}, device_scale_factor=2)
        for name, make in shots:
            html = os.path.join(out_dir, name + '.html')
            open(html, 'w').write(page(make()))
            pg.goto('file://' + html)
            pg.wait_for_timeout(200)
            pg.locator('#win').screenshot(path=os.path.join(out_dir, name + '.png'))
        browser.close()
    print('ok')
