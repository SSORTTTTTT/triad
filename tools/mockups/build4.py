# Triad's BANDS tab with Triad Time's value boxes instead of round knobs (Thomas, 2026-10-26): every mix shows its
# value with a line meter under it that fills from left to right, in a small box. Pictures only.
import math, os, sys
sys.path.insert(0, os.path.dirname(__file__))
from build import C, box, text, button, panel, gear, FONT

W, H = 1100, 700
BAND = ['#9bd85c', '#67c6b8', '#a39ce8']

def mix_box(x, y, w, h, value, frac, colour, label=None, big=False):
    out = box(x, y, w, h, '', 'background:#1d1430;border:1px solid rgba(255,255,255,0.08);border-radius:6px;box-sizing:border-box;')
    if label:
        out += box(x, y + 6, w, 10, label, f'text-align:center;font-size:8.5px;font-weight:800;color:{C["caps"]};letter-spacing:0.4px;')
    vs = 19 if big else 10.5
    vy = y + (18 if label else 3)
    out += box(x, vy, w, vs + 4, value, f'text-align:center;font-size:{vs}px;font-weight:800;color:{C["text"]};')
    mh = 4 if big else 2.5
    my = y + h - (9 if big else 6)
    out += box(x + 6, my, w - 12, mh, '', 'background:rgba(255,255,255,0.13);border-radius:2px;')
    out += box(x + 6, my, (w - 12) * frac, mh, '', f'background:{colour};border-radius:2px;')
    return out

def led(x, y, on=True):
    col = C['pink'] if on else '#4a3a5e'
    glow = f'box-shadow:0 0 8px {C["pink"]};' if on else ''
    return box(x, y, 14, 14, '', f'background:{col};border-radius:7px;border:2px solid rgba(255,255,255,0.25);box-sizing:border-box;{glow}')

def lfo_icon(x, y, colour='#cdbfe6'):
    return (f'<svg style="position:absolute;left:{x}px;top:{y}px" width="20" height="12" viewBox="0 0 20 12">'
            f'<path d="M1 6 C4 0, 7 0, 10 6 S16 12, 19 6" fill="none" stroke="{colour}" stroke-width="1.8"/></svg>')

def slot_row(x, y, w, name, value, frac, colour, latency=None, italic=False):
    out = box(x, y, w, 34, '', 'background:#4a3768;border-radius:6px;')
    out += text(x + 10, y + 11, '&#9656;', 11, C['text'])
    style = 'font-style:italic;' if italic else ''
    out += text(x + 26, y + 9, name, 14, C['text'] if not italic else C['caps'], 700, style)
    right = x + w
    out += led(right - 24, y + 11)
    out += mix_box(right - 86, y + 4, 54, 26, value, frac, colour)
    out += lfo_icon(right - 114, y + 12)
    if latency:
        out += box(right - 166, y + 10, 44, 16, latency, 'background:#5a3f2c;color:#f0b25c;border-radius:4px;font-size:10px;font-weight:800;'
                   'display:flex;align-items:center;justify-content:center;')
    return out

def band_strip(x, y, w, h, b, lo, hi, slots, volume_frac, mix_value, mix_frac, slot_text):
    colour = BAND[b]
    out = box(x, y, w, h, '', 'background:#2b1f42;border-radius:12px;')
    out += box(x + 14, y + 16, 4, 20, '', f'background:{colour};border-radius:2px;')
    out += text(x + 26, y + 16, f'BAND {b + 1}', 17, colour, 800)
    out += button(x + w - 78, y + 12, 32, 28, '&middot;&middot;&middot;', '#4a3768', C['text'], 12, 6)
    out += button(x + w - 42, y + 12, 30, 28, '&times;', '#4a3768', C['text'], 16, 6)
    out += text(x + 14, y + 50, lo, 15, C['text'], 600)
    out += text(x + w / 2 - 8, y + 50, '&rarr;', 14, C['text'], 600)
    out += box(x + w - 112, y + 50, 98, 18, hi, f'text-align:right;font-size:15px;font-weight:600;color:{C["text"]};')
    out += box(x + 12, y + 76, w - 24, 2, '', f'background:{colour};opacity:0.7;')
    yy = y + 86
    for s in slots:
        out += slot_row(x + 10, yy, w - 20, *s[:3], colour, *s[3:])
        yy += 40
    out += box(x + 10, yy, w - 20, 30, '+&nbsp; ADD SLOT', f'border:1.5px dashed {colour}88;border-radius:6px;box-sizing:border-box;'
               f'display:flex;align-items:center;justify-content:center;font-size:12.5px;font-weight:700;color:{C["caps"]};')
    # Footer: volume, mute, and BAND MIX as a box with its meter
    fy = y + h - 104
    out += box(x + 12, fy, w - 24, 1, '', f'background:{colour};opacity:0.35;')
    out += text(x + 14, fy + 14, 'VOLUME', 11.5, C['caps'], 800)
    out += text(x + w - 170, fy + 14, '0.0 dB', 12, C['text'], 700, 'width:50px;text-align:right;')
    sx, sw = x + 14, w - 140
    out += box(sx, fy + 42, sw, 5, '', 'background:#1b1329;border-radius:3px;')
    out += box(sx, fy + 42, sw * volume_frac, 5, '', f'background:{colour};border-radius:3px;')
    out += box(sx + sw * volume_frac - 7, fy + 34, 14, 20, '', 'background:#ece4f7;border-radius:4px;')
    out += button(x + 14, fy + 64, 62, 28, 'MUTE', '#4a3768', C['text'], 12, 6)
    out += text(x + 86, fy + 66, slot_text, 11, C['text'], 600)
    out += text(x + w - 186, fy + 70, '-inf', 11, C['dim'], 600, 'width:50px;text-align:right;')
    out += mix_box(x + w - 112, fy + 22, 98, 68, mix_value, mix_frac, colour, 'BAND MIX', big=True)
    return out

def hill(cx, width, top, base, colour):
    return (f'<path d="M{cx - width},{base} C{cx - width * 0.45},{base} {cx - width * 0.45},{top} {cx},{top} '
            f'C{cx + width * 0.45},{top} {cx + width * 0.45},{base} {cx + width},{base} Z" fill="{colour}" fill-opacity="0.32"/>')

def crossovers(x, y, w, h):
    out = box(x, y, w, h, '', 'background:#1d1430;border-radius:14px;')
    out += text(x + 18, y + 18, 'BAND CROSSOVERS', 12, C['text'], 800)
    out += text(x + 230, y + 19, 'Drag a divider &nbsp;/&nbsp; Upper dot: slope &nbsp;/&nbsp; Double-click: add a band &nbsp;/&nbsp; Right-click: menu', 11, C['dim'], 500)
    for i, lab in enumerate(['PRE', 'POST', '+ BAND']):
        out += button(x + 656 + i * 92, y + 12, 84 if i < 2 else 112, 30, lab, '#4a3768', C['text'], 13, 7)
    gx, gy, gw, gh = x + 18, y + 56, 808, 150
    svg = [f'<svg style="position:absolute;left:0;top:0" width="{W}" height="{H}"><defs><clipPath id="g"><rect x="{gx}" y="{gy - 10}" width="{gw}" height="{gh + 10}"/></clipPath></defs>']
    for i in range(1, 6):
        xx = gx + gw * i / 6
        svg.append(f'<line x1="{xx}" y1="{gy}" x2="{xx}" y2="{gy + gh}" stroke="rgba(222,206,255,0.06)"/>')
    d1, d2 = gx + gw * 0.36, gx + gw * 0.70
    svg.append('<g clip-path="url(#g)">' + hill(gx + gw * 0.12, gw * 0.30, gy + 18, gy + gh, BAND[0])
               + hill(gx + gw * 0.53, gw * 0.24, gy + 6, gy + gh, BAND[1]) + hill(gx + gw * 0.86, gw * 0.26, gy + 22, gy + gh, BAND[2]) + '</g>')
    for d in (d1, d2):
        svg.append(f'<line x1="{d}" y1="{gy - 4}" x2="{d}" y2="{gy + gh}" stroke="#ffffff" stroke-dasharray="5 5" stroke-width="1.6"/>')
        svg.append(f'<circle cx="{d}" cy="{gy + 38}" r="8" fill="#1d1430" stroke="#ffffff" stroke-width="3"/>')
    base = gy + gh + 4
    svg.append(f'<line x1="{gx}" y1="{base}" x2="{d1}" y2="{base}" stroke="{BAND[0]}" stroke-width="4"/>')
    svg.append(f'<line x1="{d1}" y1="{base}" x2="{d2}" y2="{base}" stroke="{BAND[1]}" stroke-width="4"/>')
    svg.append(f'<line x1="{d2}" y1="{base}" x2="{gx + gw}" y2="{base}" stroke="{BAND[2]}" stroke-width="4"/>')
    for d in (d1, d2):
        svg.append(f'<circle cx="{d}" cy="{base}" r="9" fill="#ffffff" stroke="#1d1430" stroke-width="2"/>')
    svg.append('</svg>')
    out += ''.join(svg)
    out += text(d1 + 12, gy - 2, 'BAND 2', 12.5, BAND[1], 800) + text(d2 + 12, gy - 2, 'BAND 3', 12.5, BAND[2], 800)
    out += text(gx + 6, gy - 2, 'BAND 1', 12.5, BAND[0], 800)
    for d in (d1, d2):
        out += button(d + 14, gy + 26, 84, 24, '24 dB/oct &#8964;', '#24193a', C['text'], 11.5, 5)
    out += button(d1 - 48, gy + 102, 96, 30, '250 Hz', '#140d1f', C['text'], 14, 6)
    out += button(d2 - 48, gy + 102, 96, 30, '2.5 kHz', '#140d1f', C['text'], 14, 6)
    for f, lab in [(0.0, '20 Hz'), (0.22, '100 Hz'), (0.55, '1 kHz'), (0.86, '10 kHz'), (0.97, '24 kHz')]:
        out += text(gx + gw * f, base + 14, lab, 11, C['dim'], 600)
    # Meters
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

def bands_tab():
    out = text(24, 14, 'TRIAD', 28, C['pink'], 800, 'letter-spacing:0.5px;')
    for i, lab in enumerate(['BANDS', 'LFO', 'MATRIX']):
        sel = i == 0
        out += box(130 + i * 98, 10, 90, 36, lab, f'display:flex;align-items:center;justify-content:center;font-size:15px;font-weight:800;'
                   f'color:{C["pink"] if sel else C["text"]};' + (f'background:#3a2a54;border-radius:8px;' if sel else ''))
        if sel: out += box(140, 44, 70, 3, '', f'background:{C["pink"]};border-radius:2px;')
    out += box(440, 8, 340, 38, '', 'background:#1d1430;border-radius:8px;')
    out += text(456, 20, 'PRESET', 11.5, C['caps'], 800) + text(520, 18, 'Init', 15, C['text'], 800) + text(756, 18, '&#9662;', 13, C['text'])
    out += button(792, 8, 38, 38, '&#8630;', '#3a2a54', C['dim'], 15) + button(836, 8, 38, 38, '&#8631;', '#3a2a54', C['dim'], 15)
    out += gear(886, 16, 22)
    out += text(930, 21, 'VST3 + AU &nbsp;/&nbsp; V14 &nbsp;0.15.0', 11.5, C['caps'], 700)
    out += crossovers(16, 58, 1068, 238)
    sw = (1068 - 2 * 12) / 3
    out += band_strip(16, 302, sw, 352, 0, '20 Hz', '250 Hz',
                      [('Triad Time', '100%', 1.0, '1.5ms'), ('Saturation', '65%', 0.65, None), ('Slot 3', '100%', 1.0, None, True)],
                      0.62, '100%', 1.0, '3 slots<br><span style="color:#f0b25c">1.5 ms latency</span>')
    out += band_strip(16 + sw + 12, 302, sw, 352, 1, '250 Hz', '2.5 kHz',
                      [('Slot 1', '100%', 1.0, None, True)], 0.62, '80%', 0.8, '1 slot')
    out += band_strip(16 + 2 * (sw + 12), 302, sw, 352, 2, '2.5 kHz', '24 kHz',
                      [('Reverb', '30%', 0.3, None), ('Slot 2', '100%', 1.0, None, True)], 0.62, '100%', 1.0, '2 slots')
    out += text(24, 670, 'Click a slot to load an effect, + to add a slot, ~ for its LFO. Drag a mix box left or right (or up and down) to set it; double-click to type a value.',
                11.5, C['dim'], 500)
    return out

def page(body):
    return (f'<!doctype html><html><head><meta charset="utf-8"><style>html,body{{margin:0;background:#140c1f}}'
            f'#win{{position:relative;width:{W}px;height:{H}px;overflow:hidden;font-family:{FONT};'
            f'background:linear-gradient(180deg,{C["bg1"]} 0%,{C["bg2"]} 55%,#251a3a 100%)}}</style></head>'
            f'<body><div id="win">{body}</div></body></html>')

if __name__ == '__main__':
    from playwright.sync_api import sync_playwright
    out_dir = sys.argv[1]
    with sync_playwright() as p:
        browser = p.chromium.launch()
        pg = browser.new_page(viewport={'width': W, 'height': H}, device_scale_factor=2)
        html = os.path.join(out_dir, 'bands.html')
        open(html, 'w').write(page(bands_tab()))
        pg.goto('file://' + html)
        pg.wait_for_timeout(200)
        pg.locator('#win').screenshot(path=os.path.join(out_dir, 'Triad bands - mix boxes.png'))
        # A close-up of band 1
        pg.screenshot(path=os.path.join(out_dir, 'Triad bands - mix boxes close-up.png'), clip={'x': 16, 'y': 302, 'width': 348, 'height': 352})
        browser.close()
    print('ok')
