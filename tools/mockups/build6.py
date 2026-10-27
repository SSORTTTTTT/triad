# Everything put together for Thomas's yes or no (2026-10-26 evening), pictures only:
# BANDS without outlines on the band curves; the plug-in menu with FAVORITES (all) and FAVORITES BY TYPE; the gear
# menu in the new look (with the new "Favorites by type" setting); the LFO tab with ROUTING at the side (neater, scrolls);
# its shape menu; the MATRIX tab in the new look with a destination menu open.
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from build import C, box, text, button, panel, heart, FONT
import build5
from build5 import (W, menu, MENU_BG, ROW, HEAD, SEP, CURSOR, TYPES, bands_now, triad_header, dim_overlay, lfo_page, page)
from build4 import BAND, led

build5.OUTLINES = False

# ---- The plug-in menu: FAVORITES (all) and the favorites by type ----------------------------------------------
def plugin_menu():
    out = bands_now() + dim_overlay()
    items = [('title', 'Band 2 &nbsp;/&nbsp; Slot 1'), ('search', 'Search plug-ins&hellip;'), ('head', '&#9829; FAVORITES'),
             ('item', '&#9829; FAVORITES', '20 &nbsp;&#9656;', set())]
    for name, plugs in TYPES:
        items.append(('item', name, str(len(plugs)) + ' &nbsp;&#9656;', {'hover'} if name == 'DELAY' else set()))
    items += [('sep',), ('head', 'BUILT IN'), ('item', 'Triad Time', 'TRIAD', set()), ('sep',), ('head', 'ALL PLUG-INS'),
              ('item', 'VST3', '214 &nbsp;&#9656;', set()), ('item', 'Audio Units', '187 &nbsp;&#9656;', set()), ('item', 'Not checked', '3 &nbsp;&#9656;', set()),
              ('sep',), ('item', 'Browse for a plug-in file&hellip;', None, set()), ('item', 'Refresh plug-in list', None, set()),
              ('item', 'Rescan all plug-ins', None, set())]
    x, y, w = 300, 56, 330
    m, spots = menu(x, y, w, items)
    out += m
    _, dy, _ = spots['DELAY']
    sub = [('head', 'DELAY')] + [('item', n, 'VST3', {'hearton', 'hover'} if n == 'Delay TAPE-201' else {'hearton'}) for n in TYPES[0][1]]
    sm, _ = menu(x + w - 4, dy - 30, 250, sub, cursor_on='Delay TAPE-201')
    out += sm
    out += box(x + w + 4, dy + 252, 250, 54, 'With "Favorites by type" off (gear menu), FAVORITES lists them all, A&ndash;Z, as now.',
               'font-size:10.5px;font-weight:600;color:#1b1030;background:#f4eefc;border-radius:7px;padding:8px 10px;box-sizing:border-box;line-height:1.35;')
    return out

# ---- The gear menu in the new look ------------------------------------------------------------------------
def chips(x, y, w, labels, chosen):
    out, cx = '', x
    cw = (w - 6 * (len(labels) - 1)) / len(labels)
    for lab in labels:
        sel = lab == chosen
        out += box(cx, y, cw, 20, lab, f'background:{C["lav"] if sel else "#3a2a54"};color:{"#1b1030" if sel else C["text"]};border-radius:5px;'
                   'font-size:10.5px;font-weight:800;display:flex;align-items:center;justify-content:center;')
        cx += cw + 6
    return out

def gear_menu():
    out = bands_now() + dim_overlay()
    items = [('head', 'EFFECT LIST'), ('item', 'Sorting, folders and rescans', '&#9656;', {'hover'}), ('sep',),
             ('head', 'FAVORITES'), ('item', 'Favorites by type (DELAY, DISTORTION &hellip;)', None, {'check'}), ('sep',),
             ('head', 'WINDOW SIZE'), ('chipsrow', None, None, set()), ('sep',),
             ('head', 'COLOURS'), ('item', 'Theme', 'Plum &nbsp;&#9656;', set()), ('item', 'Edit the colours&hellip;', None, set()), ('sep',),
             ('head', 'LFO TAB: BEHIND THE GRID'), ('item', 'Bands on TRIGGER: SONG', 'Nothing &#9656;', set()),
             ('item', 'Bands on TRIGGER: AUDIO', 'Waveform &#9656;', set()), ('sep',),
             ('head', 'TRIAD TIME'), ('item', 'Behind the grid', 'Nothing &#9656;', set()), ('item', 'Pitch labels on the TIME lines', 'Dragging &#9656;', set()), ('sep',),
             ('head', 'PRESETS'), ('item', 'Startup preset', 'Init &#9656;', set()), ('item', 'Use the current settings as the startup preset', None, set()),
             ('item', 'Show presets folder', None, set()), ('item', 'Show chain presets folder', None, set())]
    # the menu helper has no chips row: draw it as an empty item and put the chips over it
    flat = [it if it[0] != 'chipsrow' else ('item', '', None, set()) for it in items]
    x, y, w = 664, 52, 380
    m, spots = menu(x, y, w, flat)
    out += m
    yy = y + 6
    for it in items:
        if it[0] == 'chipsrow':
            out += chips(x + 14, yy + 2, w - 28, ['75%', '100%', '125%', '150%', '200%'], '100%')
        yy += {'head': HEAD, 'sep': SEP, 'search': 44, 'title': 34}.get(it[0], ROW)
    # wider labels on the right column: values already in the menu; EFFECT LIST opened to the side (to the left here)
    _, ey, _ = spots['Sorting, folders and rescans']
    sub = [('item', 'Sort by company', None, set()), ('item', 'Sort A&ndash;Z by name', None, {'sel', 'check'}),
           ('item', 'Look for new plug-ins each time Triad opens', None, {'check'}), ('sep',),
           ('item', 'Rescan for new plug-ins', None, {'hover'}), ('item', 'Rescan everything', None, set()), ('sep',),
           ('item', 'Add a plug-in folder&hellip;', None, set()), ('item', 'Remove a plug-in folder', '&#9656;', set()), ('sep',),
           ('item', '412 effects &middot; list up to date', None, set())]
    sm, _ = menu(x - 336, ey - 6, 340, sub, cursor_on='Rescan for new plug-ins')
    out += sm
    # the gear itself, lit
    out += box(878, 8, 38, 38, '', 'background:#4e3c6e;border-radius:8px;border:1.5px solid #c4b2ff;box-sizing:border-box;')
    from build import gear
    out += gear(886, 16, 22)
    return out

# ---- LFO: ROUTING at the side, neater, and it scrolls ------------------------------------------------------
def routing_side_neat(x, y, w, h, colour):
    out = panel(x, y, w, h, radius=12)
    out += text(x + 14, y + 12, 'ROUTING', 11, C['pink'], 800)
    out += text(x + 14, y + 28, 'Band 1 &middot; LFO 1 &rarr;', 10.5, C['dim'], 700)
    routes = [('Band Mix', '100%', 1.0, True, False), ('Delay TAPE-201 &middot; Feedback', '45%', 0.45, True, False),
              ('Saturation &middot; Drive', '70%', 0.7, False, True)]
    top, bottom = y + 48, y + h - 82
    out += box(x + 6, top, w - 12, bottom - top, '', 'overflow:hidden;')
    clip_html = ''
    for i, (name, val, frac, on, inv) in enumerate(routes):
        cy = top + i * 60
        card = box(x + 8, cy, w - 22, 54, '', 'background:#3a2a54;border-radius:8px;')
        card += led(x + 14, cy + 8, on)
        card += text(x + 34, cy + 7, name, 10.5, C['text'] if on else C['dim'], 800, f'width:{w - 74}px;overflow:hidden;text-overflow:ellipsis;')
        card += box(x + w - 30, cy + 5, 14, 16, '&times;', f'font-size:13px;font-weight:700;color:{C["dim"]};text-align:center;')
        card += box(x + 14, cy + 27, 94, 22, '', 'background:#1d1430;border-radius:5px;')
        card += box(x + 14, cy + 27, 94, 14, val, 'text-align:center;font-size:11px;font-weight:800;color:#f4eefc;')
        card += box(x + 20, cy + 43, 82, 2.5, '', 'background:rgba(255,255,255,0.13);border-radius:2px;')
        card += box(x + 20, cy + 43, 82 * frac, 2.5, '', f'background:{colour};border-radius:2px;')
        card += button(x + 114, cy + 28, 40, 20, 'INV', C['pink'] if inv else '#2b1f42', '#2a0d24' if inv else C['dim'], 9, 4)
        clip_html += card
    # clipped to the list (the third card is cut: the list scrolls)
    out += f'<div style="position:absolute;left:0;top:0;width:{W}px;height:{bottom}px;overflow:hidden;clip-path:inset({top}px 0 0 0);">{clip_html}</div>'
    out += box(x + w - 10, top + 2, 4, bottom - top - 4, '', 'background:rgba(255,255,255,0.08);border-radius:2px;')
    out += box(x + w - 10, top + 2, 4, (bottom - top) * 0.62, '', 'background:rgba(196,178,255,0.45);border-radius:2px;')
    out += button(x + 8, y + h - 74, w - 16, 28, '+ ROUTE TO&hellip;', '#4a3768', C['text'], 12, 7)
    out += button(x + 8, y + h - 40, w - 16, 28, 'LEARN', C['tile'], C['pink'], 12, 7, C['pink'])
    return out

build5.routing_side = routing_side_neat

# ---- MATRIX in the new look --------------------------------------------------------------------------------
def pill(x, y, w, h, label, colour=None):
    return box(x, y, w, h, f'<span style="flex:1;text-align:center;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;padding-left:6px">{label}</span><span style="opacity:0.7;font-size:9px;padding-right:7px">&#9662;</span>',
               f'background:#2b1f42;border:1px solid rgba(196,178,255,0.22);border-radius:6px;box-sizing:border-box;display:flex;align-items:center;'
               f'font-size:11.5px;font-weight:800;color:{colour or C["text"]};')

def matrix_panel(x, y, w, h, title, rng, colour, rows):
    out = box(x, y, w, h, '', 'background:#2b1f42;border-radius:12px;')
    out += box(x + 14, y + 14, 4, 20, '', f'background:{colour};border-radius:2px;')
    out += text(x + 26, y + 14, title + ' &nbsp;MATRIX', 15, colour, 800)
    out += box(x + w - 230, y + 16, 216, 18, rng, f'text-align:right;font-size:11.5px;font-weight:600;color:{C["dim"]};')
    hy = y + 48
    out += box(x + 10, hy, w - 20, 24, '', 'background:#1d1430;border-radius:6px;')
    cols = [(46, 92, 'SOURCE'), (146, 132, 'AMOUNT'), (286, 40, 'INV'), (334, w - 334 - 46, 'DESTINATION')]
    for cx, cw, lab in cols:
        out += box(x + cx, hy + 6, cw, 12, lab, f'text-align:center;font-size:9.5px;font-weight:800;color:{C["caps"]};letter-spacing:0.5px;')
    ry = hy + 32
    for src, val, frac, inv, dest, on in rows:
        out += box(x + 10, ry, w - 20, 40, '', 'background:#3a2a54;border-radius:8px;')
        out += led(x + 20, ry + 13, on)
        out += pill(x + 46, ry + 8, 92, 24, src)
        out += box(x + 146, ry + 6, 132, 28, '', 'background:#1d1430;border-radius:6px;')
        out += box(x + 146, ry + 8, 132, 14, val, 'text-align:center;font-size:11.5px;font-weight:800;color:#f4eefc;')
        out += box(x + 154, ry + 26, 116, 3, '', 'background:rgba(255,255,255,0.13);border-radius:2px;')
        out += box(x + 154, ry + 26, 116 * frac, 3, '', f'background:{colour};border-radius:2px;')
        out += button(x + 286, ry + 9, 40, 22, 'INV', C['pink'] if inv else '#2b1f42', '#2a0d24' if inv else C['dim'], 9.5, 5)
        out += pill(x + 334, ry + 8, w - 334 - 46, 24, dest)
        out += box(x + w - 38, ry + 11, 18, 18, '&times;', f'font-size:14px;font-weight:700;color:{C["dim"]};text-align:center;')
        ry += 46
    out += button(x + 10, ry + 4, 140, 30, '+ ADD ROUTE', '#4a3768', C['text'], 12.5, 7)
    return out, ry

def matrix_page():
    out = triad_header('MATRIX')
    out += box(16, 58, 1068, 18, 'Every LFO and where it goes, band by band. Scroll sideways for the other bands, PRE and POST.',
               f'font-size:11.5px;font-weight:500;color:{C["dim"]};')
    pw = (1068 - 12) / 2
    p1, _ = matrix_panel(16, 84, pw, 560, 'PRE', 'full range, before the bands', '#d9dcd6',
                         [('LFO 1', '100%', 1.0, False, 'Slot 1 Mix', True)])
    p2, ry = matrix_panel(16 + pw + 12, 84, pw, 560, 'BAND 1', '20 Hz &nbsp;&rarr;&nbsp; 250 Hz', BAND[0],
                          [('LFO 1', '100%', 1.0, False, 'Band Mix', True), ('LFO 2', '45%', 0.45, False, 'Delay TAPE-201 &middot; Feedback', True),
                           ('LFO 3', '70%', 0.7, True, 'Saturation &middot; Drive', False), ('LFO 4', '100%', 1.0, False, 'Slot 3 Mix', True)])
    out += p1 + p2
    # sideways scroll bar
    out += box(16, 654, 1068, 6, '', 'background:rgba(255,255,255,0.06);border-radius:3px;')
    out += box(16, 654, 420, 6, '', 'background:rgba(196,178,255,0.45);border-radius:3px;')
    out += text(24, 670, 'Click SOURCE or DESTINATION to choose; drag an amount box left or right; INV turns the LFO upside down.', 11.5, C['dim'], 500)
    # The destination menu of the second route, open
    dx = 16 + pw + 12 + 334
    dy = 84 + 48 + 32 + 46 + 8 + 24 + 4
    items = [('head', 'BAND 1'), ('item', 'Band Mix', None, set()), ('item', 'Band Volume', None, set()), ('sep',), ('head', 'SLOTS'),
             ('item', 'Slot 1 &middot; Triad Time', '&#9656;', set()), ('item', 'Slot 2 &middot; Delay TAPE-201', '&#9656;', {'hover'}),
             ('item', 'Slot 3 &middot; Saturation', '&#9656;', set()), ('sep',), ('item', 'LEARN: move a knob in an effect', None, set())]
    m, spots = menu(1084 - 270, dy, 270, items)
    out += m
    _, sy, _ = spots['Slot 2 &middot; Delay TAPE-201']
    sub = [('head', 'DELAY TAPE-201'), ('item', 'Mix', None, set()), ('item', 'Time', None, set()), ('item', 'Feedback', None, {'sel', 'check'}),
           ('item', 'Tone', None, set()), ('item', 'Wow and flutter', None, {'hover'}), ('item', 'Saturation', None, set())]
    sm, _ = menu(1084 - 270 - 196, sy - 30, 200, sub, cursor_on='Wow and flutter')   # opens to the left: no room on the right
    out += sm
    return out

if __name__ == '__main__':
    from playwright.sync_api import sync_playwright
    out_dir = sys.argv[1]
    shots = [('1 BANDS', bands_now(), 700), ('2 BANDS - plug-in menu', plugin_menu(), 700), ('3 BANDS - gear menu', gear_menu(), 700),
             ('4 LFO', lfo_page('routing at the side'), 760), ('5 LFO - shape menu', lfo_page('routing at the side', menu_open=True), 760),
             ('6 MATRIX', matrix_page(), 700)]
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
