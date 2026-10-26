# Mock-ups of Triad Time layouts (pictures only). Usage: python3 tools/mockups/build.py <output folder> (needs Playwright).
# Layout mock-ups for Triad Time's next interface (pictures only: nothing is built into the plug-in).
import math, sys, os
sys.path.insert(0, os.path.dirname(__file__))
from presets import entries, shape

W, H = 1100, 760
C = dict(
    bg1='#3b2b5a', bg2='#2b1f43', panel='#24193a', panelEdge='rgba(255,255,255,0.07)', tile='#43335f', tileHi='#4e3c6e',
    grid='#1a1228', line='rgba(222,206,255,0.13)', major='rgba(222,206,255,0.30)', text='#f4eefc', dim='#b9aad2',
    caps='#dccff3', pink='#ff6bd0', lav='#9b7dff', lavLight='#c4b2ff', white='#efe6ff', heart='#e5484d',
    teal='#5fd6c4', yellow='#f5c84c', green='#7bd88f', red='#ff6b6b')
FONT = "'Liberation Sans', Arial, sans-serif"

def box(x, y, w, h, inner='', style=''):
    return f'<div style="position:absolute;left:{x}px;top:{y}px;width:{w}px;height:{h}px;{style}">{inner}</div>'

def text(x, y, s, size=12, color=None, weight=700, extra=''):
    return f'<div style="position:absolute;left:{x}px;top:{y}px;font-size:{size}px;font-weight:{weight};color:{color or C["text"]};white-space:nowrap;{extra}">{s}</div>'

def button(x, y, w, h, label, fill=None, color=None, size=13, radius=7, border=None):
    fill = fill or C['tile']
    b = f'border:1.5px solid {border};' if border else ''
    return box(x, y, w, h, label, f'background:{fill};border-radius:{radius}px;display:flex;align-items:center;justify-content:center;'
               f'font-size:{size}px;font-weight:700;color:{color or C["text"]};{b}box-sizing:border-box;')

def panel(x, y, w, h, inner='', radius=12):
    return box(x, y, w, h, inner, f'background:{C["panel"]};border:1px solid {C["panelEdge"]};border-radius:{radius}px;box-sizing:border-box;')

HEART = 'M12 21s-7.5-4.6-9.6-9.2C.9 8.6 2.6 5 6.1 4.6c2-.2 3.9.9 5.9 3.1 2-2.2 3.9-3.3 5.9-3.1 3.5.4 5.2 4 3.7 7.2C19.5 16.4 12 21 12 21z'
def heart(x, y, s=12, on=False):
    fill = C['heart'] if on else 'none'
    stroke = C['heart'] if on else 'rgba(239,230,255,0.85)'
    return (f'<svg style="position:absolute;left:{x}px;top:{y}px" width="{s}" height="{s}" viewBox="0 0 24 24">'
            f'<path d="{HEART}" fill="{fill}" stroke="{stroke}" stroke-width="2.2"/></svg>')

def poly(pts, x, y, w, h, pad=6):
    return ' '.join(f'{x + pad + px * (w - 2 * pad):.1f},{y + pad + (1 - py) * (h - 2 * pad):.1f}' for px, py, _ in pts)

def tile(x, y, w, h, idx, selected=False, fav=False, name=None):
    n, f, pts = shape(idx)
    edge = f'border:2px solid {C["lavLight"]};box-shadow:0 0 0 3px rgba(155,125,255,0.25);' if selected else f'border:1px solid rgba(255,255,255,0.05);'
    fill = C['tileHi'] if selected else C['tile']
    sh = h - (16 if name else 0)
    out = box(x, y, w, h, '', f'background:{fill};border-radius:9px;box-sizing:border-box;{edge}')
    stroke = C['lavLight'] if selected else C['white']
    out += (f'<svg style="position:absolute;left:{x}px;top:{y}px" width="{w}" height="{sh}">'
            f'<polyline points="{poly(pts, 0, 2, w, sh - 4, 10)}" fill="none" stroke="{stroke}" stroke-width="2" stroke-linejoin="round"/></svg>')
    if name:
        label = n.split(':')[0]
        out += text(x + 24, y + h - 17, label, 10, C['dim'], 700)
        out += heart(x + 8, y + h - 16, 12, fav)
    else:
        out += heart(x + 7, y + h - 18, 13, fav)
    return out

def knob(cx, cy, d, value, label, colour, fill_frac, label_below=True, size=None):
    r = d / 2 - 3
    a0, a1 = math.radians(135), math.radians(135 + 270 * fill_frac)
    def pt(a): return cx + r * math.cos(a), cy + r * math.sin(a)
    def arc(a_from, a_to, col, wid):
        (x0, y0), (x1, y1) = pt(a_from), pt(a_to)
        large = 1 if (a_to - a_from) > math.pi else 0
        return f'<path d="M{x0:.1f},{y0:.1f} A{r:.1f},{r:.1f} 0 {large} 1 {x1:.1f},{y1:.1f}" fill="none" stroke="{col}" stroke-width="{wid}" stroke-linecap="round"/>'
    svg = (f'<svg style="position:absolute;left:0;top:0" width="{W}" height="{H}">'
           f'<circle cx="{cx}" cy="{cy}" r="{r - 4}" fill="#130c1f"/>'
           + arc(math.radians(135), math.radians(405), 'rgba(255,255,255,0.13)', max(3, d * 0.07))
           + arc(a0, a1, colour, max(3, d * 0.07)) + '</svg>')
    fs = size or max(10, int(d * 0.24))
    out = svg + box(cx - d / 2, cy - fs / 2 - 1, d, fs + 2, value, f'display:flex;align-items:center;justify-content:center;font-size:{fs}px;font-weight:700;color:{C["text"]};')
    if label:
        ly = cy + d / 2 + 4 if label_below else cy - d / 2 - 16
        out += box(cx - 50, ly, 100, 12, label, f'text-align:center;font-size:9.5px;font-weight:700;color:{C["caps"]};letter-spacing:0.3px;')
    return out

KNOBS = [('100%', 'TIME MIX', C['lav'], 1.0), ('100%', 'VOLUME MIX', C['white'], 1.0), ('8.0 ms', 'SMOOTHING', C['lav'], 0.35),
         ('2.0 ms', 'ATTACK', C['white'], 0.2), ('12 ms', 'RELEASE', C['white'], 0.3)]

def gear(x, y, s=22):
    teeth = ''.join(f'<rect x="10.5" y="1" width="3" height="5" rx="1" fill="{C["dim"]}" transform="rotate({a} 12 12)"/>' for a in range(0, 360, 45))
    return (f'<svg style="position:absolute;left:{x}px;top:{y}px" width="{s}" height="{s}" viewBox="0 0 24 24">{teeth}'
            f'<circle cx="12" cy="12" r="7" fill="none" stroke="{C["dim"]}" stroke-width="3"/></svg>')

def header(hints=True, hints_xy=(560, 30)):
    out = text(24, 20, 'TRIAD TIME', 28, C['text'], 800, 'letter-spacing:0.5px;')
    out += text(212, 31, 'by Thomas Ross', 14, C['dim'], 400)
    out += button(338, 16, 132, 38, 'TIME', C['lav'], '#1b1030', 15)
    out += button(478, 16, 132, 38, 'VOLUME', C['tile'], C['white'], 15)
    if hints:
        out += text(hints_xy[0], hints_xy[1], 'Double-click: add point &middot; Drag: move &middot; Double-click a point: delete &middot; Drag the small dot: curve &middot; Right-click: more', 10.5, C['dim'], 400)
    return out

def grid(x, y, w, h, idx=2):
    n, f, pts = shape(idx)
    gx, gy, gw, gh = x + 34, y + 22, w - 48, h - 34
    s = [f'<svg style="position:absolute;left:0;top:0" width="{W}" height="{H}">',
         '<defs><pattern id="hatch" width="9" height="9" patternUnits="userSpaceOnUse" patternTransform="rotate(45)">'
         '<line x1="0" y1="0" x2="0" y2="9" stroke="rgba(222,206,255,0.10)" stroke-width="3"/></pattern></defs>']
    for i in range(33):
        xx = gx + gw * i / 32
        s.append(f'<line x1="{xx:.1f}" y1="{gy}" x2="{xx:.1f}" y2="{gy + gh}" stroke="{C["major"] if i % 8 == 0 else C["line"]}" stroke-width="1"/>')
    for j in range(17):
        yy = gy + gh * j / 16
        s.append(f'<line x1="{gx}" y1="{yy:.1f}" x2="{gx + gw}" y2="{yy:.1f}" stroke="{C["major"] if j % 4 == 0 else C["line"]}" stroke-width="1"/>')
    s.append(f'<polygon points="{gx},{gy} {gx + gw},{gy + gh} {gx},{gy + gh}" fill="url(#hatch)"/>')
    s.append(f'<line x1="{gx}" y1="{gy}" x2="{gx + gw}" y2="{gy + gh}" stroke="rgba(222,206,255,0.25)" stroke-width="1"/>')
    P = [(gx + px * gw, gy + (1 - py) * gh) for px, py, _ in pts]
    s.append(f'<polyline points="{" ".join(f"{a:.1f},{b:.1f}" for a, b in P)}" fill="none" stroke="{C["lav"]}" stroke-width="3" stroke-linejoin="round"/>')
    labels = []
    for (x0, y0, _), (x1, y1, _), (a, b), (c, d) in zip(pts, pts[1:], P, P[1:]):
        if x1 - x0 > 1e-6:
            rate = 1 + (y1 - y0) / (x1 - x0)
            mx, my = (a + c) / 2, (b + d) / 2
            s.append(f'<circle cx="{mx:.1f}" cy="{my:.1f}" r="4.5" fill="{C["grid"]}" stroke="{C["lav"]}" stroke-width="2"/>')
            if abs(rate - 1) > 0.02:
                st = 0 if abs(rate) < 1e-6 else round(12 * math.log2(abs(rate)))
                lab = 'FREEZE' if abs(rate) < 1e-6 else ('FWD' if rate > 0 else 'REV') + f' {"+" if st > 0 else ""}{st} st'.replace('-', '&minus;')
                labels.append(button(mx - 44, my - 32, 88, 24, lab, '#2a1d44', C['white'], 12, 6, C['lav']))
    for a, b in P:
        s.append(f'<circle cx="{a:.1f}" cy="{b:.1f}" r="6" fill="#ffffff"/>')
    s.append('</svg>')
    out = box(x, y, w, h, '', f'background:{C["grid"]};border-radius:12px;')
    out += ''.join(s) + ''.join(labels)
    for i, lab in enumerate(['0', '1/4', '2/4', '3/4', '4/4']):
        out += text(gx + gw * i / 4 - (4 if i == 0 else 12), y + 4, lab, 11.5, C['text'])
    out += text(x + 8, gy - 6, '0', 11, C['text']) + text(x + 4, gy + gh / 2 - 7, '&minus;2/4', 11, C['text']) + text(x + 4, gy + gh - 12, '&minus;4/4', 11, C['text'])
    return out

def toolbar(y, x0=16, x1=1084):
    out = button(x0, y, 104, 36, '1 Bar') + button(x0 + 110, y, 34, 36, '&#9664;', size=11) + button(x0 + 148, y, 34, 36, '&#9654;', size=11)
    out += button(x0 + 192, y, 112, 36, 'GRID 1/64') + button(x0 + 310, y, 76, 36, 'SNAP', C['pink'], '#2a0d24') + button(x0 + 392, y, 76, 36, 'SAVE')
    r = x1
    for w, lab in reversed([(40, '&#128465;'), (46, '2x'), (46, '1/2'), (40, '&#8644;'), (40, '&#8645;'), (36, '&#8630;'), (36, '&#8631;')]):
        r -= w
        out += button(r, y, w, 36, lab, size=14 if len(lab) > 3 else 13)
        r -= 6
    return out

def slider(x, y, w, frac, col, label, value):
    out = text(x, y - 7, label, 11.5, C['text'])
    sx, sw = x + 92, w - 140
    out += box(sx, y, sw, 4, '', 'background:rgba(255,255,255,0.12);border-radius:2px;')
    out += box(sx, y, sw * frac, 4, '', f'background:{col};border-radius:2px;')
    out += box(sx + sw * frac - 7, y - 8, 14, 20, '', 'background:#e9e2f5;border-radius:4px;')
    out += text(x + w - 38, y - 7, value, 11.5, C['text'])
    return out

def generate(x, y, w, h):
    out = panel(x, y, w, h)
    out += text(x + 16, y + 16, 'GENERATE', 14, C['pink'], 800)
    for i, lab in enumerate(['HARD', 'MIX', 'SOFT']):
        out += button(x + w - 16 - (3 - i) * 58 + 4, y + 12, 54, 24, lab, size=11)
    out += text(x + 16, y + 54, 'FROM', 12, C['caps'])
    out += button(x + 90, y + 48, 104, 26, 'GROSS BEAT', C['pink'], '#2a0d24', 11) + button(x + 200, y + 48, w - 216, 26, 'TIME PRESETS', size=11)
    out += slider(x + 16, y + 92, w - 20, 0.98, C['pink'], 'ANGULAR', '100%')
    out += slider(x + 16, y + 116, w - 20, 0.49, C['teal'], 'SOFT', '49%')
    out += slider(x + 16, y + 140, w - 20, 0.62, C['yellow'], 'CHAOS', '62%')
    by = y + h - 76
    out += button(x + 16, by, w - 32, 32, '&#9861;&nbsp; NEW SHAPE', C['pink'], '#2a0d24', 14)
    out += button(x + 16, by + 37, (w - 40) / 2, 28, '&#10003; 5', '#1c2a20', C['green'], 15, 7, C['green'])
    out += button(x + 24 + (w - 40) / 2, by + 37, (w - 40) / 2, 28, '&#10007;', '#2c1418', C['red'], 15, 7, C['red'])
    return out

def page_nav(x, y, label='1 / 200', w=126):
    return (button(x, y, 28, 26, '&#9664;', size=10) + box(x + 28, y + 6, w - 56, 16, label, f'text-align:center;font-size:12.5px;font-weight:700;color:{C["text"]};')
            + button(x + w - 28, y, 28, 26, '&#9654;', size=10))

FOLDER0 = [i for i, e in enumerate(entries) if e[1] == 0]
FAV = {1, 4, 8}

def option_a():
    out = header(hints=False)
    out += text(24, 70, 'Double-click: add point &middot; Drag: move &middot; Double-click a point: delete &middot; Drag the small dot: curve &middot; Right-click: more', 10.5, C['dim'], 400)
    # MIX AND SMOOTHING: a small panel in the top-right corner
    px, py, pw, ph = 612, 10, 432, 84
    out += panel(px, py, pw, ph, radius=10)
    out += text(px + 12, py + 24, 'MIX AND', 11, C['pink'], 800) + text(px + 12, py + 38, 'SMOOTHING', 11, C['pink'], 800)
    for i, (v, lab, col, frac) in enumerate(KNOBS):
        out += knob(px + 126 + i * 70, py + 34, 46, v, lab, col, frac, size=9)
    out += gear(1054, 22)
    out += grid(16, 104, 1068, 344)
    out += toolbar(458)
    # PRESETS: categories as tabs, 10 bigger shapes a page
    bx, by, bw, bh = 16, 506, 748, 238
    out += panel(bx, by, bw, bh)
    cx = bx + 12
    for lab in ['&#9829; FAVORITES', 'OCTAVE DOWN', 'OCTAVE UP', 'REVERSE', 'CHOPS', 'PITCH + CHOPS', 'USER']:
        wdt = int(len(lab.replace('&#9829;', 'x')) * 6.9 + 16)
        sel = lab == 'OCTAVE DOWN'
        out += button(cx, by + 12, wdt, 26, lab, C['lav'] if sel else C['tile'], '#1b1030' if sel else C['text'], 10.5, 13)
        cx += wdt + 6
    out += page_nav(bx + bw - 138, by + 12)
    tw, th = (bw - 24 - 4 * 10) / 5, 84
    for k in range(10):
        r, c = divmod(k, 5)
        out += tile(bx + 12 + c * (tw + 10), by + 50 + r * (th + 10), tw, th, FOLDER0[k], selected=(k == 2), fav=(k in FAV))
    out += generate(776, 506, 308, 238)
    return out

def option_b():
    out = header(hints=False)
    out += text(24, 70, 'Double-click: add point &middot; Drag: move &middot; Double-click a point: delete &middot; Drag the small dot: curve &middot; Right-click: more', 10.5, C['dim'], 400)
    # MIX AND SMOOTHING: smaller still, values inside the knobs, in the corner
    px, py, pw, ph = 694, 12, 352, 80
    out += panel(px, py, pw, ph, radius=10)
    out += text(px + 12, py + 7, 'MIX AND SMOOTHING', 10, C['pink'], 800)
    for i, (v, lab, col, frac) in enumerate(KNOBS):
        out += knob(px + 40 + i * 68, py + 42, 38, v, lab, col, frac, size=8.5)
    out += gear(1056, 24)
    out += grid(16, 104, 1068, 344)
    out += toolbar(458)
    # PRESETS: a category list on the left, 10 shapes with their names
    bx, by, bw, bh = 16, 506, 748, 238
    out += panel(bx, by, bw, bh)
    out += text(bx + 14, by + 12, 'PRESETS', 12, C['pink'], 800)
    cats = [('&#9829; FAVORITES', '12'), ('USER', '4'), ('OCTAVE DOWN', '2,000'), ('OCTAVE UP', '2,000'), ('REVERSE', '2,000'), ('CHOPS', '2,000'), ('PITCH + CHOPS', '2,000'), ('LFO SHAPES &#9656;', '')]
    for i, (lab, n) in enumerate(cats):
        yy = by + 34 + i * 24
        sel = lab == 'OCTAVE DOWN'
        out += box(bx + 8, yy, 156, 21, '', f'background:{C["lav"] if sel else "transparent"};border-radius:6px;')
        out += text(bx + 16, yy + 4, lab, 11, '#1b1030' if sel else C['text'])
        out += text(bx + 120, yy + 4, n, 10, '#1b1030' if sel else C['dim'], 400, 'width:36px;text-align:right;')
    out += box(bx + 172, by + 12, 1, bh - 24, '', 'background:rgba(255,255,255,0.08);')
    tx0 = bx + 184
    tw, th = (bw - 184 - 12 - 4 * 8) / 5, 82
    for k in range(10):
        r, c = divmod(k, 5)
        out += tile(tx0 + c * (tw + 8), by + 12 + r * (th + 8), tw, th, FOLDER0[k], selected=(k == 2), fav=(k in FAV), name=True)
    out += page_nav(tx0 + (bw - 184 - 12) / 2 - 85, by + bh - 36, 'Page 1 of 200', 170)
    out += generate(776, 506, 308, 238)
    return out

def option_c():
    out = header(hints=False)
    out += text(636, 18, 'Double-click: add point &middot; Drag: move &middot; Double-click a point: delete', 10.5, C['dim'], 400)
    out += text(636, 36, 'Drag the small dot: curve &middot; Right-click: more', 10.5, C['dim'], 400)
    out += gear(1056, 22)
    # The grid, with MIX AND SMOOTHING as a small window on its right
    out += grid(16, 70, 884, 378)
    px, py, pw, ph = 910, 70, 174, 378
    out += panel(px, py, pw, ph, radius=12)
    out += text(px + 14, py + 14, 'MIX AND SMOOTHING', 11, C['pink'], 800)
    places = [(px + 46, py + 82, 56), (px + 128, py + 82, 56), (px + 46, py + 200, 50), (px + 128, py + 200, 50), (px + 87, py + 308, 50)]
    for (cx, cy, d), (v, lab, col, frac) in zip(places, KNOBS):
        out += knob(cx, cy, d, v, lab, col, frac, size=11)
    out += toolbar(458)
    # PRESETS: one menu for the category, 10 bigger shapes with names
    bx, by, bw, bh = 16, 506, 748, 238
    out += panel(bx, by, bw, bh)
    out += button(bx + 12, by + 12, 170, 28, 'OCTAVE DOWN &nbsp;&#9662;', C['tile'], C['text'], 12)
    out += button(bx + 188, by + 12, 122, 28, '&#9829; FAVORITES', C['tile'], C['text'], 11)
    out += box(bx + 318, by + 12, 220, 28, '&#128269;&nbsp; Search patterns', f'background:#1b1329;border-radius:7px;display:flex;align-items:center;padding-left:10px;box-sizing:border-box;font-size:11.5px;color:{C["dim"]};')
    out += page_nav(bx + bw - 138, by + 13)
    tw, th = (bw - 24 - 4 * 10) / 5, 86
    for k in range(10):
        r, c = divmod(k, 5)
        out += tile(bx + 12 + c * (tw + 10), by + 50 + r * (th + 6), tw, th, FOLDER0[k], selected=(k == 2), fav=(k in FAV), name=True)
    out += generate(776, 506, 308, 238)
    return out

def page(body):
    return (f'<!doctype html><html><head><meta charset="utf-8"><style>html,body{{margin:0;background:#140c1f}}'
            f'#win{{position:relative;width:{W}px;height:{H}px;overflow:hidden;font-family:{FONT};'
            f'background:linear-gradient(180deg,{C["bg1"]} 0%,{C["bg2"]} 55%,#251a3a 100%)}}</style></head>'
            f'<body><div id="win">{body}</div></body></html>')

if __name__ == '__main__':
    from playwright.sync_api import sync_playwright
    out_dir = sys.argv[1]
    options = [('A', option_a), ('B', option_b), ('C', option_c)]
    with sync_playwright() as p:
        browser = p.chromium.launch()
        pg = browser.new_page(viewport={'width': W, 'height': H}, device_scale_factor=2)
        for name, make in options:
            html = os.path.join(out_dir, f'option-{name}.html')
            open(html, 'w').write(page(make()))
            pg.goto('file://' + html)
            pg.wait_for_timeout(200)
            pg.locator('#win').screenshot(path=os.path.join(out_dir, f'option-{name}.png'))
        browser.close()
    print('ok')
