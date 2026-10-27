# Mock-ups (Thomas, 2026-10-26 evening), pictures only:
#  1. BANDS with mix boxes, the crossover display drawn as the plug-in draws it now (each band's real response).
#  2. Menus in the look of Triad Time's category menu: the plug-in menu, and the same with FAVORITES by type.
#  3. Triad's LFO tab laid out as Triad Time (grid, toolbar, presets, GENERATE), ROUTING small (next to SAVE, or at
#     the side), points selected and moved with the arrows, and the shape menu in the new look.
import math, os, random, sys
sys.path.insert(0, os.path.dirname(__file__))
from build import C, box, text, button, panel, gear, heart, FONT
import build4
from build4 import BAND, mix_box, band_strip, led

W, H = 1100, 760
CURSOR = ('<svg style="position:absolute;left:{x}px;top:{y}px" width="16" height="22" viewBox="0 0 16 22">'
          '<path d="M1 1 L1 17 L5 13 L8 20 L11 19 L8 12 L14 12 Z" fill="#ffffff" stroke="#111" stroke-width="1.3"/></svg>')

# ---- 1. BANDS: the crossover display as it is now -------------------------------------------------------------
def fx(f, gx, gw): return gx + gw * math.log(f / 20.0) / math.log(24000.0 / 20.0)

def crossovers_now(x, y, w, h):
    out = box(x, y, w, h, '', 'background:#1d1430;border-radius:14px;')
    out += text(x + 18, y + 18, 'BAND CROSSOVERS', 12, C['text'], 800)
    out += text(x + 230, y + 19, 'Drag a divider &nbsp;/&nbsp; Upper dot: slope &nbsp;/&nbsp; Double-click: add a band &nbsp;/&nbsp; Right-click: menu', 11, C['dim'], 500)
    for i, lab in enumerate(['PRE', 'POST', '+ BAND']):
        out += button(x + 656 + i * 92, y + 12, 84 if i < 2 else 112, 30, lab, '#4a3768', C['text'], 13, 7)
    gx, gy, gw, gh = x + 18, y + 52, 808, 158
    cuts = [250.0, 2500.0]
    top, bottom = gy + 30, gy + gh - 3
    svg = [f'<svg style="position:absolute;left:0;top:0" width="{W}" height="{H}">']
    for f in (100, 1000, 10000):
        xx = fx(f, gx, gw)
        svg.append(f'<line x1="{xx}" y1="{gy}" x2="{xx}" y2="{gy + gh}" stroke="rgba(226,210,234,0.07)"/>')
    for b in range(3):
        pts = []
        for i in range(0, 405):
            xx = gx + gw * i / 404
            f = 20.0 * (24000.0 / 20.0) ** (i / 404)
            m = 1.0
            if b > 0: r = (f / cuts[b - 1]) ** 4; m *= r / (1 + r)
            if b < 2: r = (f / cuts[b]) ** 4; m *= 1 / (1 + r)
            db = max(-30.0, min(0.0, 20 * math.log10(max(m, 1e-9))))
            pts.append((xx, top + (bottom - top) * (-db / 30.0)))
        line = ' '.join(f'{a:.1f},{c:.1f}' for a, c in pts)
        svg.append(f'<polygon points="{gx},{bottom} {line} {gx + gw},{bottom}" fill="{BAND[b]}" fill-opacity="0.13"/>')
        svg.append(f'<polyline points="{line}" fill="none" stroke="{BAND[b]}" stroke-opacity="0.6" stroke-width="1.3"/>')
    base = gy + gh + 6
    d = [fx(c, gx, gw) for c in cuts]
    for dd in d:
        svg.append(f'<line x1="{dd}" y1="{gy + 2}" x2="{dd}" y2="{base}" stroke="#ffffff" stroke-opacity="0.85" stroke-dasharray="5 5" stroke-width="1.5"/>')
        svg.append(f'<circle cx="{dd}" cy="{gy + 40}" r="7.5" fill="#1d1430" stroke="#ffffff" stroke-width="2.6"/>')
    svg.append(f'<line x1="{gx}" y1="{base}" x2="{d[0]}" y2="{base}" stroke="{BAND[0]}" stroke-width="4"/>')
    svg.append(f'<line x1="{d[0]}" y1="{base}" x2="{d[1]}" y2="{base}" stroke="{BAND[1]}" stroke-width="4"/>')
    svg.append(f'<line x1="{d[1]}" y1="{base}" x2="{gx + gw}" y2="{base}" stroke="{BAND[2]}" stroke-width="4"/>')
    for dd in d:
        svg.append(f'<circle cx="{dd}" cy="{base}" r="9" fill="#ffffff" stroke="#1d1430" stroke-width="2"/>')
    svg.append('</svg>')
    out += ''.join(svg)
    out += text(gx + 8, gy + 4, 'BAND 1', 12.5, BAND[0], 800)
    out += text(d[0] + 12, gy + 4, 'BAND 2', 12.5, BAND[1], 800) + text(d[1] + 12, gy + 4, 'BAND 3', 12.5, BAND[2], 800)
    for dd in d:
        out += button(dd + 14, gy + 28, 84, 24, '24 dB/oct &#8964;', '#24193a', C['text'], 11.5, 5)
    out += button(d[0] - 48, gy + 102, 96, 30, '250 Hz', '#140d1f', C['text'], 14, 6)
    out += button(d[1] - 48, gy + 102, 96, 30, '2.5 kHz', '#140d1f', C['text'], 14, 6)
    for f, lab in [(20, '20 Hz'), (100, '100 Hz'), (1000, '1 kHz'), (10000, '10 kHz'), (24000, '24 kHz')]:
        out += text(fx(f, gx, gw) - (0 if f == 20 else 18 if f < 24000 else 40), base + 14, lab, 11, C['dim'], 600)
    mx = x + w - 200
    out += box(mx - 10, y + 56, 1, h - 76, '', 'background:rgba(255,255,255,0.08);')
    out += button(mx + 100, y + 12, 70, 28, 'LIVE', '#4a3768', C['text'], 12, 7)
    for i, (lab, val, fill) in enumerate([('IN', '-inf', 0), ('OUT', '-inf', 0), ('LAT', '0 ms', 0.05), ('CPU', '0%', 0.02)]):
        cx = mx + 10 + i * 46
        out += text(cx, y + 50, lab, 11, C['caps'], 800)
        out += box(cx + 2, y + 70, 14, h - 120, '', 'background:#140d1f;border-radius:4px;')
        if fill:
            out += box(cx + 2, y + 70 + (h - 120) * (1 - fill), 14, (h - 120) * fill, '', f'background:{BAND[0]};border-radius:4px;')
        out += text(cx - 2, y + h - 42, val, 10.5, BAND[0] if lab == 'LAT' else C['dim'], 700)
    return out

def triad_header(tab):
    out = text(24, 14, 'TRIAD', 28, C['pink'], 800, 'letter-spacing:0.5px;')
    for i, lab in enumerate(['BANDS', 'LFO', 'MATRIX']):
        sel = lab == tab
        out += box(130 + i * 98, 10, 90, 36, lab, f'display:flex;align-items:center;justify-content:center;font-size:15px;font-weight:800;'
                   f'color:{C["pink"] if sel else C["text"]};' + ('background:#3a2a54;border-radius:8px;' if sel else ''))
        if sel: out += box(140 + i * 98, 44, 70, 3, '', f'background:{C["pink"]};border-radius:2px;')
    out += box(440, 8, 340, 38, '', 'background:#1d1430;border-radius:8px;')
    out += text(456, 20, 'PRESET', 11.5, C['caps'], 800) + text(520, 18, 'Init', 15, C['text'], 800) + text(756, 18, '&#9662;', 13, C['text'])
    out += button(792, 8, 38, 38, '&#8630;', '#3a2a54', C['dim'], 15) + button(836, 8, 38, 38, '&#8631;', '#3a2a54', C['dim'], 15)
    out += gear(886, 16, 22)
    out += text(930, 21, 'VST3 + AU &nbsp;/&nbsp; V14 &nbsp;0.15.0', 11.5, C['caps'], 700)
    return out

def bands_now():
    out = triad_header('BANDS')
    out += crossovers_now(16, 58, 1068, 238)
    sw = (1068 - 2 * 12) / 3
    out += band_strip(16, 302, sw, 352, 0, '20 Hz', '250 Hz',
                      [('Triad Time', '100%', 1.0, '1.5ms'), ('Saturation', '65%', 0.65, None), ('Slot 3', '100%', 1.0, None, True)],
                      0.62, '100%', 1.0, '3 slots<br><span style="color:#f0b25c">1.5 ms latency</span>')
    out += band_strip(16 + sw + 12, 302, sw, 352, 1, '250 Hz', '2.5 kHz', [('Slot 1', '100%', 1.0, None, True)], 0.62, '80%', 0.8, '1 slot')
    out += band_strip(16 + 2 * (sw + 12), 302, sw, 352, 2, '2.5 kHz', '24 kHz',
                      [('Reverb', '30%', 0.3, None), ('Slot 2', '100%', 1.0, None, True)], 0.62, '100%', 1.0, '2 slots')
    out += text(24, 670, 'Click a slot to load an effect, + to add a slot, ~ for its LFO. Drag a mix box left or right (or up and down) to set it; double-click to type a value.',
                11.5, C['dim'], 500)
    return out

# ---- 2. Menus in the new look ------------------------------------------------------------------------------
MENU_BG = 'background:#2d2147;border:1px solid rgba(196,178,255,0.35);border-radius:10px;box-shadow:0 14px 36px rgba(0,0,0,0.6);box-sizing:border-box;'
ROW, HEAD, SEP = 25, 24, 9

def menu(x, y, w, items, cursor_on=None):
    """items: ('head', text) | ('sep',) | ('search', text) | ('title', text) | ('item', label, right, opts)."""
    def height(it):
        return {'head': HEAD, 'sep': SEP, 'search': 44, 'title': 34}.get(it[0], ROW)
    hgt = 12 + sum(height(i) for i in items)
    out = box(x, y, w, hgt, '', MENU_BG)
    yy = y + 6
    spots = {}
    for it in items:
        kind = it[0]
        if kind == 'title':
            out += text(x + 14, yy + 8, it[1], 15, C['text'], 800)
        elif kind == 'search':
            out += box(x + 10, yy + 6, w - 20, 30, '', 'background:#170f24;border:1px solid rgba(196,178,255,0.25);border-radius:8px;box-sizing:border-box;')
            out += (f'<svg style="position:absolute;left:{x + 20}px;top:{yy + 13}px" width="16" height="16" viewBox="0 0 16 16">'
                    f'<circle cx="6.5" cy="6.5" r="4.8" fill="none" stroke="{C["pink"]}" stroke-width="2"/><line x1="10" y1="10" x2="14.5" y2="14.5" stroke="{C["pink"]}" stroke-width="2.2"/></svg>')
            out += text(x + 44, yy + 13, it[1], 12.5, C['dim'], 600)
            out += text(x + w - 92, yy + 14, 'or just type', 10.5, C['dim'], 400)
        elif kind == 'head':
            out += text(x + 14, yy + 8, it[1], 10, C['pink'], 800, 'letter-spacing:0.6px;')
        elif kind == 'sep':
            out += box(x + 12, yy + SEP / 2, w - 24, 1, '', 'background:rgba(255,255,255,0.10);')
        else:
            _, label, right, opts = it
            sel, hov = 'sel' in opts, 'hover' in opts
            if sel or hov:
                out += box(x + 5, yy + 1, w - 10, ROW - 2, '', f'background:{C["lav"] if sel else "rgba(196,178,255,0.18)"};border-radius:6px;')
            lx = x + 16
            colour = '#1b1030' if sel else C['text']
            if 'heart' in opts or 'hearton' in opts:
                out += heart(lx - 1, yy + 6, 12, 'hearton' in opts)
                lx += 18
            if 'check' in opts:
                out += text(lx, yy + 6, '&#10003;', 12, colour)
                lx += 16
            out += text(lx, yy + 6, label, 12.5, colour, 700 if 'bold' in opts else 600)
            if right:
                rcol = '#1b1030' if sel else C['dim']
                if right in ('VST3', 'AU', 'TRIAD'):
                    out += box(x + w - 58, yy + 5, 44, 15, right, f'border:1px solid {"#1b1030" if sel else "rgba(196,178,255,0.35)"};border-radius:4px;'
                               f'font-size:9px;font-weight:800;color:{rcol};display:flex;align-items:center;justify-content:center;box-sizing:border-box;')
                else:
                    out += text(x + w - 70, yy + 7, right, 10.5 if right != '&#9656;' else 12, rcol, 600, 'width:56px;text-align:right;')
            spots[label] = (x, yy, w)
        yy += height(it)
    if cursor_on and cursor_on in spots:
        sx, sy, sw = spots[cursor_on]
        out += CURSOR.format(x=sx + sw - 44, y=sy + 10)
    return out, spots

FAVS = [('Triad Time', 'TRIAD'), ('Objeq Delay 2', 'VST3'), ('Delay BRIGADE', 'VST3'), ('Delay ETERNITY', 'VST3'), ('Delay TAPE-201', 'VST3'),
        ('Dist COLDFIRE', 'VST3'), ('Dist OPAMP-21', 'VST3'), ('Dist TUBE-CULTURE', 'VST3'), ('ShaperBox 3', 'VST3'), ('Repeater', 'VST3'),
        ('Sigmund 2', 'VST3'), ('Infiltrator', 'VST3'), ('kHs Delay', 'VST3'), ('kHs Dual Delay', 'VST3'), ('MStereoSpread', 'VST3'),
        ('Cluster Delay', 'VST3'), ('Rift', 'VST3'), ('Bite', 'VST3'), ('Dirt', 'VST3'), ('Raum', 'VST3')]

def dim_overlay():
    return box(0, 0, W, H, '', 'background:rgba(10,4,20,0.45);')

def plugin_menu_two_columns():
    out = bands_now() + dim_overlay()
    x, y, w = 300, 40, 560
    # header, search, then FAVORITES in two columns
    out += box(x, y, w, 636, '', MENU_BG)
    out += text(x + 14, y + 12, 'Band 2 &nbsp;/&nbsp; Slot 1', 15, C['text'], 800)
    out += box(x + 10, y + 40, w - 20, 30, '', 'background:#170f24;border:1px solid rgba(196,178,255,0.25);border-radius:8px;box-sizing:border-box;')
    out += (f'<svg style="position:absolute;left:{x + 20}px;top:{y + 47}px" width="16" height="16" viewBox="0 0 16 16">'
            f'<circle cx="6.5" cy="6.5" r="4.8" fill="none" stroke="{C["pink"]}" stroke-width="2"/><line x1="10" y1="10" x2="14.5" y2="14.5" stroke="{C["pink"]}" stroke-width="2.2"/></svg>')
    out += text(x + 44, y + 47, 'Search plug-ins&hellip;', 12.5, C['dim'], 600) + text(x + w - 92, y + 48, 'or just type', 10.5, C['dim'], 400)
    out += heart(x + 14, y + 84, 12, True) + text(x + 32, y + 83, 'FAVORITES', 10, C['pink'], 800, 'letter-spacing:0.6px;')
    out += text(x + w - 150, y + 83, 'A&ndash;Z &middot; 20 plug-ins', 10, C['dim'], 600)
    colw = (w - 30) / 2
    for i, (name, fmt) in enumerate(FAVS):
        c, r = divmod(i, 10)
        cx, ry = x + 10 + c * (colw + 10), y + 104 + r * 26
        if name == 'Delay ETERNITY':
            out += box(cx, ry, colw, 24, '', 'background:rgba(196,178,255,0.18);border-radius:6px;')
            out += CURSOR.format(x=cx + colw - 70, y=ry + 9)
        out += heart(cx + 8, ry + 6, 12, True) + text(cx + 26, ry + 5, name, 12.5, C['text'], 600)
        out += box(cx + colw - 48, ry + 5, 42, 15, fmt, 'border:1px solid rgba(196,178,255,0.35);border-radius:4px;font-size:9px;font-weight:800;'
                   f'color:{C["dim"]};display:flex;align-items:center;justify-content:center;box-sizing:border-box;')
    sy = y + 372
    out += box(x + 12, sy, w - 24, 1, '', 'background:rgba(255,255,255,0.10);')
    out += text(x + 14, sy + 12, 'BUILT IN', 10, C['pink'], 800, 'letter-spacing:0.6px;')
    out += text(x + 24, sy + 34, 'Triad Time &nbsp;<span style="color:#b9aad2;font-weight:600">Thomas Ross &middot; time and volume gate</span>', 12.5, C['text'], 600)
    out += box(x + 12, sy + 60, w - 24, 1, '', 'background:rgba(255,255,255,0.10);')
    out += text(x + 14, sy + 72, 'ALL PLUG-INS', 10, C['pink'], 800, 'letter-spacing:0.6px;')
    for i, (lab, n) in enumerate([('VST3', '214'), ('Audio Units', '187'), ('Not checked', '3')]):
        ry = sy + 92 + i * 26
        out += text(x + 24, ry + 5, lab, 12.5, C['text'], 600) + text(x + w - 92, ry + 6, n, 10.5, C['dim'], 600, 'width:40px;text-align:right;')
        out += text(x + w - 38, ry + 4, '&#9656;', 12, C['dim'])
    sy2 = sy + 176
    out += box(x + 12, sy2, w - 24, 1, '', 'background:rgba(255,255,255,0.10);')
    for i, lab in enumerate(['Browse for a plug-in file&hellip;', 'Refresh plug-in list', 'Rescan all plug-ins']):
        out += text(x + 24, sy2 + 10 + i * 26, lab, 12.5, C['caps'], 600)
    return out

TYPES = [('DELAY', ['Objeq Delay 2', 'Delay BRIGADE', 'Delay ETERNITY', 'Delay TAPE-201', 'Sigmund 2', 'kHs Delay', 'kHs Dual Delay', 'Cluster Delay']),
         ('DISTORTION', ['Dist COLDFIRE', 'Dist OPAMP-21', 'Dist TUBE-CULTURE', 'Rift', 'Bite', 'Dirt']),
         ('RHYTHM + TIME', ['Triad Time', 'ShaperBox 3', 'Repeater', 'Infiltrator']),
         ('REVERB', ['Raum']), ('STEREO', ['MStereoSpread'])]

def plugin_menu_by_type():
    out = bands_now() + dim_overlay()
    items = [('title', 'Band 2 &nbsp;/&nbsp; Slot 1'), ('search', 'Search plug-ins&hellip;'), ('head', '&#9829; FAVORITES BY TYPE')]
    for name, plugs in TYPES:
        items.append(('item', name, str(len(plugs)) + ' &nbsp;&#9656;', {'hover'} if name == 'DELAY' else set()))
    items += [('item', 'All favorites, A&ndash;Z', '20 &nbsp;&#9656;', set()), ('sep',), ('head', 'BUILT IN'),
              ('item', 'Triad Time', 'TRIAD', set()), ('sep',), ('head', 'ALL PLUG-INS'),
              ('item', 'VST3', '214 &nbsp;&#9656;', set()), ('item', 'Audio Units', '187 &nbsp;&#9656;', set()), ('item', 'Not checked', '3 &nbsp;&#9656;', set()),
              ('sep',), ('item', 'Browse for a plug-in file&hellip;', None, set()), ('item', 'Refresh plug-in list', None, set()),
              ('item', 'Rescan all plug-ins', None, set())]
    x, y, w = 300, 70, 330
    m, spots = menu(x, y, w, items)
    out += m
    # DELAY opened to the side
    dx, dy, _ = spots['DELAY']
    sub = [('head', 'DELAY')] + [('item', n, 'VST3', {'hearton', 'hover'} if n == 'Delay TAPE-201' else {'hearton'}) for n in TYPES[0][1]]
    sm, sspots = menu(x + w - 4, dy - 30, 250, sub, cursor_on='Delay TAPE-201')
    out += sm
    # The type comes from the plug-in itself (VST3 / AU category), and can be changed: right-click a favorite
    out += box(x + w + 4, dy + 250, 246, 52, 'Each type comes from the plug-in (its VST3 or AU category). Right-click a favorite to move it to another type.',
               'font-size:10.5px;font-weight:600;color:#1b1030;background:#f4eefc;border-radius:7px;padding:7px 10px;box-sizing:border-box;line-height:1.35;')
    return out

# ---- 3. The LFO tab laid out as Triad Time ---------------------------------------------------------------
def lfo_points():
    # An LFO on BAND 1 (triplet steps and ramps), 0..1 across the bar, 0..1 up
    return [(0.0, 1.0), (0.0833, 1.0), (0.0833, 0.35), (0.1667, 0.35), (0.1667, 0.85), (0.3333, 0.2), (0.3333, 0.75), (0.4167, 0.75),
            (0.4167, 0.4), (0.5, 0.4), (0.5, 1.0), (0.5833, 1.0), (0.6667, 0.3), (0.75, 0.3), (0.75, 0.9), (0.8333, 0.9), (0.8333, 0.5),
            (1.0, 0.5)]

def lfo_grid(x, y, w, h, colour, selection=None):
    gx, gy, gw, gh = x + 46, y + 24, w - 62, h - 38
    s = [f'<svg style="position:absolute;left:0;top:0" width="{W}" height="{H}">']
    for i in range(17):
        xx = gx + gw * i / 16
        s.append(f'<line x1="{xx:.1f}" y1="{gy}" x2="{xx:.1f}" y2="{gy + gh}" stroke="{C["major"] if i % 4 == 0 else C["line"]}"/>')
    for j in range(9):
        yy = gy + gh * j / 8
        s.append(f'<line x1="{gx}" y1="{yy:.1f}" x2="{gx + gw}" y2="{yy:.1f}" stroke="{C["major"] if j % 2 == 0 else C["line"]}"/>')
    pts = lfo_points()
    P = [(gx + px * gw, gy + (1 - py) * gh) for px, py in pts]
    s.append(f'<polygon points="{gx},{gy + gh} ' + ' '.join(f'{a:.1f},{b:.1f}' for a, b in P) + f' {gx + gw},{gy + gh}" fill="{colour}" fill-opacity="0.10"/>')
    s.append(f'<polyline points="{" ".join(f"{a:.1f},{b:.1f}" for a, b in P)}" fill="none" stroke="{colour}" stroke-width="3" stroke-linejoin="round"/>')
    sel = set()
    if selection:
        x0, x1 = selection
        sel = {i for i, (px, py) in enumerate(pts) if x0 <= px <= x1}
        sx0, sx1 = gx + gw * x0 - 8, gx + gw * x1 + 8
        s.append(f'<rect x="{sx0}" y="{gy + 8}" width="{sx1 - sx0}" height="{gh - 16}" fill="rgba(255,107,208,0.08)" stroke="{C["pink"]}" stroke-dasharray="6 4" stroke-width="1.5" rx="6"/>')
    for i, (a, b) in enumerate(P):
        if i in sel:
            s.append(f'<circle cx="{a:.1f}" cy="{b:.1f}" r="7.5" fill="{C["pink"]}" stroke="#ffffff" stroke-width="2.5"/>')
        else:
            s.append(f'<circle cx="{a:.1f}" cy="{b:.1f}" r="5.5" fill="#ffffff"/>')
    s.append('</svg>')
    out = box(x, y, w, h, '', f'background:{C["grid"]};border-radius:12px;') + ''.join(s)
    for i, lab in enumerate(['0', '1/4', '2/4', '3/4', '4/4']):
        out += text(gx + gw * i / 4 - (4 if i == 0 else 12 if i < 4 else 24), y + 5, lab, 11.5, C['text'])
    for j, lab in enumerate(['100%', '75%', '50%', '25%', '0%']):
        out += text(x + 8, gy + gh * j / 4 - 7, lab, 11, C['text'])
    if selection:
        out += box(gx + gw * selection[0] - 8, gy + gh - 4, 150, 20, '4 points selected', f'background:{C["pink"]};color:#2a0d24;border-radius:5px;'
                   'font-size:10.5px;font-weight:800;display:flex;align-items:center;justify-content:center;')
    return out

def lfo_shape(k):
    r = random.Random(1000 + k)
    pts, steps = [], 6 if k % 2 == 0 else 12
    for i in range(steps):
        x0, x1 = i / steps, (i + 1) / steps
        kind = r.random()
        a, b = r.choice([0.15, 0.3, 0.5, 0.7, 0.9, 1.0]), r.choice([0.1, 0.25, 0.5, 0.75, 1.0])
        if kind < 0.45: pts += [(x0, a), (x1, a)]
        elif kind < 0.8: pts += [(x0, a), (x1, b)]
        else: pts += [(x0, b), ((x0 + x1) / 2, a), (x1, b)]
    return pts

def lfo_tile(x, y, w, h, k, selected=False, fav=False, colour=None):
    pts = lfo_shape(k)
    edge = f'border:2px solid {C["lavLight"]};box-shadow:0 0 0 3px rgba(155,125,255,0.25);' if selected else 'border:1px solid rgba(255,255,255,0.05);'
    out = box(x, y, w, h, '', f'background:{C["tileHi"] if selected else C["tile"]};border-radius:9px;box-sizing:border-box;{edge}')
    sh = h - 16
    line = ' '.join(f'{x + 10 + px * (w - 20):.1f},{y + 8 + (1 - py) * (sh - 14):.1f}' for px, py in pts)
    out += f'<svg style="position:absolute;left:0;top:0" width="{W}" height="{H}"><polyline points="{line}" fill="none" stroke="{colour if selected else C["white"]}" stroke-width="2" stroke-linejoin="round"/></svg>'
    out += heart(x + 8, y + h - 16, 12, fav) + text(x + 24, y + h - 17, f'Triplets {k + 1:02d}', 10, C['dim'], 700)
    return out

def page_nav(x, y, label):
    return (button(x, y, 28, 26, '&#9664;', size=10) + box(x + 28, y + 6, 70, 16, label, f'text-align:center;font-size:12.5px;font-weight:700;color:{C["text"]};')
            + button(x + 98, y, 28, 26, '&#9654;', size=10))

def lfo_browser(bx, by, bw, bh, colour, open_button=False):
    out = panel(bx, by, bw, bh)
    out += button(bx + 12, by + 12, 170, 28, 'GEN: TRIPLETS &nbsp;' + ('&#9652;' if open_button else '&#9662;'),
                  C['tileHi'] if open_button else C['tile'], C['text'], 12, 7, C['lavLight'] if open_button else None)
    out += button(bx + 188, by + 12, 122, 28, '&#9829; FAVORITES', C['tile'], C['text'], 11)
    out += box(bx + 318, by + 12, 220, 28, '&#128269;&nbsp; Search shapes', f'background:#1b1329;border-radius:7px;display:flex;align-items:center;'
               f'padding-left:10px;box-sizing:border-box;font-size:11.5px;color:{C["dim"]};')
    out += page_nav(bx + bw - 138, by + 13, '1 / 5')
    tw, th = (bw - 24 - 4 * 10) / 5, (bh - 50 - 12 - 6) / 2
    for k in range(10):
        r, c = divmod(k, 5)
        out += lfo_tile(bx + 12 + c * (tw + 10), by + 50 + r * (th + 6), tw, th, k, selected=(k == 3), fav=(k in (1, 6)), colour=colour)
    return out

def generate_lfo(x, y, w, h):
    out = panel(x, y, w, h)
    out += text(x + 16, y + 16, 'GENERATE', 14, C['pink'], 800)
    for i, lab in enumerate(['HARD', 'MIX', 'SOFT']):
        out += button(x + w - 16 - (3 - i) * 58 + 4, y + 12, 54, 24, lab, size=11)
    out += text(x + 16, y + 54, 'FROM', 12, C['caps'])
    out += button(x + 90, y + 48, 104, 26, 'INFILTRATOR', C['pink'], '#2a0d24', 11) + button(x + 200, y + 48, w - 216, 26, 'SHAPERBOX', size=11)
    from build import slider
    out += slider(x + 16, y + 92, w - 20, 0.98, C['pink'], 'ANGULAR', '100%')
    out += slider(x + 16, y + 116, w - 20, 0.49, C['teal'], 'SOFT', '49%')
    out += slider(x + 16, y + 140, w - 20, 0.62, C['yellow'], 'CHAOS', '62%')
    by = y + h - 76
    out += button(x + 16, by, w - 32, 32, '&#9861;&nbsp; NEW SHAPE', C['pink'], '#2a0d24', 14)
    out += button(x + 16, by + 37, (w - 40) / 2, 28, '&#10003;', '#1c2a20', C['green'], 15, 7, C['green'])
    out += button(x + 24 + (w - 40) / 2, by + 37, (w - 40) / 2, 28, '&#10007;', '#2c1418', C['red'], 15, 7, C['red'])
    return out

def lfo_rows(colour):
    out = ''
    for i, (lab, col) in enumerate([('PRE', C['text']), ('BAND 1', '#1b1030'), ('BAND 2', BAND[1]), ('BAND 3', BAND[2]), ('POST', C['text'])]):
        fill = BAND[0] if lab == 'BAND 1' else '#3a2a54'
        out += button(16 + i * 102, 56, 96, 30, lab, fill, col, 13, 7)
    out += text(560, 58, 'Double-click: add point &middot; Drag: move &middot; Double-click a point: delete', 10.5, C['dim'], 400)
    out += text(560, 74, 'Drag the small dot: curve &middot; Drag across points: select &middot; Right-click: more', 10.5, C['dim'], 400)
    # The LFOs of this band, and how the LFO starts
    out += box(16, 94, 210, 40, '', f'background:#3a2a54;border-radius:7px;border-bottom:3px solid {colour};box-sizing:border-box;')
    out += text(26, 99, 'LFO 1 &middot; BAND', 10, colour, 800) + text(26, 113, 'Band Mix', 13, C['text'], 800)
    out += box(232, 94, 210, 40, '', 'background:#3a2a54;border-radius:7px;')
    out += text(242, 99, 'LFO 2 &middot; SLOT 1', 10, C['caps'], 800) + text(242, 113, 'Delay TAPE-201', 13, C['text'], 800)
    out += button(448, 94, 40, 40, '+', '#3a2a54', C['text'], 20, 7)
    out += box(904, 94, 180, 40, '', 'background:#3a2a54;border-radius:7px;')
    out += text(914, 99, 'TRIGGER', 10, C['caps'], 800) + text(914, 113, 'SONG', 13, C['text'], 800) + text(1066, 108, '&#9662;', 11, C['text'])
    return out

def routing_box(x, y, w, h, colour):
    out = panel(x, y, w, h, radius=9)
    out += text(x + 8, y + 5, 'ROUTING &middot; BAND 1 &middot; LFO 1 &rarr;', 7.5, C['pink'], 800, 'letter-spacing:0.3px;')
    ry = y + 16
    out += text(x + 8, ry + 4, 'Band Mix', 10.5, C['text'], 800)
    out += box(x + 76, ry, 62, 18, '', 'background:#1d1430;border-radius:4px;')
    out += box(x + 76, ry + 1, 62, 11, '100%', 'text-align:center;font-size:9.5px;font-weight:800;color:#f4eefc;')
    out += box(x + 81, ry + 13, 52, 2.5, '', f'background:{colour};border-radius:2px;')
    out += button(x + 142, ry, 30, 18, 'INV', '#3a2a54', C['dim'], 8.5, 4)
    out += led(x + 178, ry + 2)
    out += button(x + w - 22, ry, 16, 18, '&times;', 'transparent', C['dim'], 12, 4)
    out += button(x + 6, y + h - 22, 116, 18, '+ ROUTE TO&hellip;', '#4a3768', C['text'], 9.5, 4)
    out += button(x + 128, y + h - 22, w - 134, 18, 'LEARN', C['tile'], C['pink'], 9.5, 4, C['pink'])
    return out

def routing_side(x, y, w, h, colour):
    out = panel(x, y, w, h, radius=12)
    out += text(x + 14, y + 14, 'ROUTING', 11, C['pink'], 800)
    out += text(x + 14, y + 30, 'Band 1 &middot; LFO 1 &rarr;', 11, C['text'], 700)
    for i, (name, val, frac, on) in enumerate([('Band Mix', '100%', 1.0, True), ('Delay TAPE-201: Feedback', '45%', 0.45, True)]):
        cy = y + 54 + i * 70
        out += box(x + 8, cy, w - 16, 62, '', 'background:#3a2a54;border-radius:8px;')
        out += text(x + 16, cy + 7, name, 10.5, C['text'], 800, f'width:{w - 50}px;overflow:hidden;text-overflow:ellipsis;')
        out += button(x + w - 28, cy + 4, 16, 16, '&times;', 'transparent', C['dim'], 12, 4)
        out += box(x + 16, cy + 26, 74, 28, '', 'background:#1d1430;border-radius:5px;')
        out += box(x + 16, cy + 27, 74, 16, val, 'text-align:center;font-size:12px;font-weight:800;color:#f4eefc;')
        out += box(x + 22, cy + 46, 62 * frac, 3, '', f'background:{colour};border-radius:2px;')
        out += box(x + 22 + 62 * frac, cy + 46, 62 * (1 - frac), 3, '', 'background:rgba(255,255,255,0.13);border-radius:2px;')
        out += button(x + 96, cy + 30, 34, 20, 'INV', '#2b1f42', C['dim'], 9, 4)
        out += led(x + 138, cy + 33, on)
    out += text(x + 10, y + 194, 'LEARN, then turn a knob in an effect', 9, C['dim'], 500)
    out += button(x + 8, y + h - 104, w - 16, 28, '+ ROUTE TO&hellip;', '#4a3768', C['text'], 12, 7)
    out += button(x + 8, y + h - 70, w - 16, 28, 'LEARN', C['tile'], C['pink'], 12, 7, C['pink'])
    return out

def toolbar_lfo(y, hover_right=True):
    out = button(16, y, 104, 36, '1 Bar') + button(126, y, 34, 36, '&#9664;', size=11) + button(164, y, 34, 36, '&#9654;', size=11)
    out += button(208, y, 112, 36, 'GRID 1/16') + button(326, y, 76, 36, 'SNAP', C['pink'], '#2a0d24') + button(408, y, 76, 36, 'SAVE')
    items = [(32, '&#128465;'), (38, '2x'), (38, '1/2'), (30, 'L'), (30, 'R'), (34, '&#8644;'), (34, '&#8645;'), (32, '&#8630;'), (32, '&#8631;')]
    r, places = 1084, []
    for w_, lab in reversed(items):
        r -= w_
        places.append((r, w_, lab))
        r -= 5
    for bx, bw, lab in places:
        if lab in ('L', 'R'):
            icon = ('<svg width="18" height="14" viewBox="0 0 18 14">' + ('<path d="M2 7 L9 1 L9 13 Z" fill="#f4eefc"/><rect x="10" y="5.5" width="7" height="3" rx="1" fill="#f4eefc"/>' if lab == 'L'
                    else '<path d="M16 7 L9 1 L9 13 Z" fill="#f4eefc"/><rect x="1" y="5.5" width="7" height="3" rx="1" fill="#f4eefc"/>') + '</svg>')
            hov = hover_right and lab == 'R'
            out += button(bx, y, bw, 36, icon, C['tileHi'] if hov else C['tile'])
            if hov:
                tip = 'Move the 4 selected points right. Shift-click for fine adjustment.'
                out += box(bx + bw / 2 - 210, y - 34, 360, 24, tip, 'background:#f4eefc;color:#1b1030;border-radius:6px;font-size:11px;font-weight:700;'
                           'display:flex;align-items:center;justify-content:center;box-shadow:0 4px 14px rgba(0,0,0,0.45);')
                out += box(bx + bw / 2 - 5, y - 11, 10, 6, '', 'background:#f4eefc;clip-path:polygon(0 0,100% 0,50% 100%);')
                out += CURSOR.format(x=bx + bw - 9, y=y + 22)
        else:
            out += button(bx, y, bw, 36, lab, size=14 if len(lab) > 3 else 13)
    return out, 484, places[-1][0]

def lfo_page(variant, menu_open=False):
    colour = BAND[0]
    out = triad_header('LFO') + lfo_rows(colour)
    if variant == 'routing next to SAVE':
        out += lfo_grid(16, 140, 1068, 302, colour, selection=None if menu_open else (0.40, 0.52))
        tb, le, rs = toolbar_lfo(458, hover_right=not menu_open)
        out += tb + routing_box(le + 10, 448, rs - le - 20, 56, colour)
    else:
        out += lfo_grid(16, 140, 880, 302, colour, selection=None if menu_open else (0.40, 0.52))
        out += routing_side(906, 140, 178, 302, colour)
        tb, _, _ = toolbar_lfo(458, hover_right=not menu_open)
        out += tb
    out += lfo_browser(16, 512, 748, 232, colour, open_button=menu_open)
    out += generate_lfo(776, 512, 308, 232)
    if menu_open:
        items = [('head', 'EVERYTHING'), ('item', 'ALL', '11,049', set()), ('item', '&#9829; FAVORITES', '13', set()), ('sep',),
                 ('item', 'SHAPES BY TYPE', '7 &nbsp;&#9656;', set()), ('item', 'GENERATED (FROM THE DICE)', '6 &nbsp;&#9656;', {'hover'}),
                 ('item', 'EXTRA (EVERY PREVIEW SHAPE)', '355', set()), ('item', 'TRIAD TIME: PITCH AND CHOPS', '5 &nbsp;&#9656;', set()), ('sep',),
                 ('head', 'YOUR SHAPES'), ('item', 'USER', '16', set()), ('item', 'Save this shape to USER&hellip;', None, set()),
                 ('item', 'Show the USER folder', None, set())]
        hgt = 12 + sum({'head': HEAD, 'sep': SEP}.get(i[0], ROW) for i in items)
        m, spots = menu(28, 520 - hgt, 300, items)
        out += m
        gx_, gy_, _ = spots['GENERATED (FROM THE DICE)']
        sub = [('item', 'GEN: SQUARES', '44', set()), ('item', 'GEN: ANGLES', '44', set()), ('item', 'GEN: HARD+CURVE', '44', set()),
               ('item', 'GEN: SOFT', '44', set()), ('item', 'GEN: TRIPLETS', '44', {'sel', 'check'}), ('item', 'GEN: CHAOS', '44', {'hover'})]
        sm, _ = menu(28 + 296, gy_ - 6, 220, sub, cursor_on='GEN: CHAOS')
        out += sm
    return out

def page(body, h=H):
    return (f'<!doctype html><html><head><meta charset="utf-8"><style>html,body{{margin:0;background:#140c1f}}'
            f'#win{{position:relative;width:{W}px;height:{h}px;overflow:hidden;font-family:{FONT};'
            f'background:linear-gradient(180deg,{C["bg1"]} 0%,{C["bg2"]} 55%,#251a3a 100%)}}</style></head>'
            f'<body><div id="win">{body}</div></body></html>')

if __name__ == '__main__':
    from playwright.sync_api import sync_playwright
    out_dir = sys.argv[1]
    shots = [('Triad bands - mix boxes, crossovers as now', bands_now(), 700),
             ('Triad menus - plug-in menu', plugin_menu_two_columns(), 700),
             ('Triad menus - favorites by type', plugin_menu_by_type(), 700),
             ('Triad LFO - routing next to SAVE', lfo_page('routing next to SAVE'), 760),
             ('Triad LFO - routing at the side', lfo_page('routing at the side'), 760),
             ('Triad LFO - shape menu open', lfo_page('routing next to SAVE', menu_open=True), 760)]
    with sync_playwright() as p:
        browser = p.chromium.launch()
        for name, body, h in shots:
            pg = browser.new_page(viewport={'width': W, 'height': h}, device_scale_factor=2)
            html = os.path.join(out_dir, name + '.html')
            open(html, 'w').write(page(body, h))
            pg.goto('file://' + html)
            pg.wait_for_timeout(200)
            pg.locator('#win').screenshot(path=os.path.join(out_dir, name + '.png'))
            pg.close()
        browser.close()
    print('ok')
