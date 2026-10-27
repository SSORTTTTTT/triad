# Every grey menu Thomas photographed (2026-10-26), in the new look: two sheets of pictures, the menus as they would open.
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from build import C, box, text, FONT
from build5 import W, menu, MENU_BG, CURSOR

THEMES = {'Triad': ['#252729', '#2e3230', '#9bd85c', '#6fb7e9'], 'Midnight': ['#1b2130', '#262e40', '#40e0ff', '#8f6bff'],
          'Graphite': ['#2b2b2b', '#383838', '#ff5c5c', '#00e5ff'], 'Black': ['#0b0b0b', '#1c1c1c', '#f5f5f5', '#00e5ff'],
          'Plum': ['#2a1f30', '#382a40', '#ff6bd0', '#8f6bff'], 'Forest': ['#1f2a23', '#2b3a30', '#ffd84d', '#40e0ff']}

def swatches(name):
    return ''.join(f'<span style="display:inline-block;width:10px;height:10px;border-radius:5px;background:{c};border:1px solid rgba(255,255,255,0.35)"></span>'
                   for c in THEMES[name])

def caption(x, y, s):
    return text(x, y, s, 11, C['dim'], 800, 'letter-spacing:0.5px;')

def columns_menu(x, y, cols, chosen, hover):
    """A menu in columns (the LFO length menu): [(title, [items])]."""
    cw, row = 132, 25
    h = 14 + 24 + row * max(len(items) for _, items in cols)
    out = box(x, y, cw * len(cols) + 12, h, '', MENU_BG)
    for i, (title, items) in enumerate(cols):
        cx = x + 6 + i * cw
        out += text(cx + 10, y + 12, title, 10, C['pink'], 800, 'letter-spacing:0.6px;')
        for j, it in enumerate(items):
            ry = y + 32 + j * row
            if it == chosen:
                out += box(cx + 2, ry + 1, cw - 8, row - 2, '', f'background:{C["lav"]};border-radius:6px;')
            elif it == hover:
                out += box(cx + 2, ry + 1, cw - 8, row - 2, '', 'background:rgba(196,178,255,0.18);border-radius:6px;')
                out += CURSOR.format(x=cx + cw - 40, y=ry + 10)
            out += text(cx + 12, ry + 6, ('&#10003; ' if it == chosen else '') + it, 12.5, '#1b1030' if it == chosen else C['text'], 700)
    return out

def sheet1():
    out = ''
    # Triad Time's gear menu
    out += caption(24, 18, 'TRIAD TIME &middot; GEAR MENU')
    items = [('head', 'PITCH LABELS (TIME)'), ('item', 'Off', None, set()), ('item', 'While dragging a point', None, set()),
             ('item', 'Always, on every line', None, {'sel', 'check'}), ('item', 'FWD / REV: which way it plays; st: semitones', None, {'dim'}), ('sep',),
             ('head', 'BEHIND THE GRID'), ('item', 'Nothing', None, {'check'}), ('item', 'The waveform (the sound coming in)', None, {'hover'}),
             ('item', 'The spectrum (as Triad&rsquo;s, Pro-Q style)', None, set()), ('sep',), ('head', 'THEME')]
    for t in THEMES:
        items.append(('item', t, swatches(t), {'sel', 'check'} if t == 'Plum' else set()))
    items += [('item', 'My themes', '&#9656;', set()), ('item', 'Custom', None, set()), ('item', 'Edit colours&hellip;', None, set())]
    m, _ = menu(24, 38, 360, items, cursor_on='The waveform (the sound coming in)')
    out += m
    # The preset menu
    out += caption(412, 18, 'TRIAD &middot; PRESET MENU')
    m, _ = menu(412, 38, 300, [('head', 'TRIAD PRESETS'), ('item', 'Save as new preset&hellip;', None, set()),
                                ('item', 'Initialize (an empty Triad)', None, set()), ('sep',), ('item', 'SSO 2', None, set()),
                                ('item', 'SSO 3 KV', None, {'hover'}), ('item', 'SSORT FIRST PRESET', None, set()), ('sep',),
                                ('item', 'Show presets folder', None, set())], cursor_on='SSO 3 KV')
    out += m
    # A band's right-click menu (on the crossover display)
    out += caption(412, 318, 'TRIAD &middot; RIGHT-CLICK A BAND')
    m, _ = menu(412, 338, 300, [('head', 'BAND 1'), ('item', 'Copy band state', None, set()), ('item', 'Paste band state', None, {'dim'}), ('sep',),
                                 ('item', 'Remove band 1', None, set()), ('item', 'Add a band here (62 Hz)', None, {'hover'}),
                                 ('item', 'Edit LFO for band 1 mix&hellip;', None, set()), ('sep',), ('head', 'ALL DIVIDERS'),
                                 ('item', 'Hard wall', '96 dB/oct', set()), ('item', 'Default', '24 dB/oct', set()), ('item', 'Soft', '6 dB/oct', set())],
                    cursor_on='Add a band here (62 Hz)')
    out += m
    # The matrix SOURCE menu
    out += caption(740, 18, 'TRIAD &middot; MATRIX: SOURCE')
    m, _ = menu(740, 38, 230, [('head', 'SOURCE'), ('item', 'LFO 1 (Slot 1)', None, {'sel', 'check'}), ('item', 'LFO 2 (Band Mix)', None, set()),
                                ('sep',), ('item', 'Edit this LFO&hellip;', None, {'hover'})], cursor_on='Edit this LFO&hellip;')
    out += m
    # Plug-in windows: right-click a slot (as an example of the slot menu)
    out += caption(740, 214, 'TRIAD &middot; A SLOT&rsquo;S MENU')
    m, _ = menu(740, 234, 320, [('head', 'BAND 2 &middot; SLOT 1 &middot; DELAY TAPE-201'), ('item', 'Open its window', None, set()),
                                 ('item', 'Replace&hellip;', '&#9656;', set()), ('item', 'Bypass', None, set()), ('sep',),
                                 ('item', 'Copy plugin', None, {'hover'}), ('item', 'Paste plugin', None, {'dim'}), ('sep',),
                                 ('item', 'Edit its LFOs&hellip;', None, set()), ('item', '&#9829; Add to FAVORITES', None, set()), ('sep',),
                                 ('item', 'Remove', None, set())], cursor_on='Copy plugin')
    out += m
    return out

def sheet2():
    out = ''
    out += caption(24, 18, 'LFO &middot; RIGHT-CLICK THE GRID')
    m, _ = menu(24, 38, 250, [('head', 'WAVE'), ('item', 'Select all points', None, set()), ('item', 'Move points to grid', None, set()),
                               ('item', 'Flip horizontal', None, set()), ('item', 'Flip vertical', None, set()), ('item', 'Copy wave', None, {'hover'}),
                               ('item', 'Paste wave', None, set()), ('item', 'Delete selected points', None, set()), ('sep',), ('head', 'GRID'),
                               ('item', 'Triplet', None, set()), ('item', 'Snap to grid', None, {'check'}), ('item', 'Grid 1/4', None, set()),
                               ('item', 'Grid 1/8', None, set()), ('item', 'Grid 1/16', None, {'sel', 'check'}), ('item', 'Grid 1/32', None, set()),
                               ('item', 'Grid 1/64', None, set())], cursor_on='Copy wave')
    out += m
    out += caption(300, 18, 'LFO &middot; + ROUTE TO')
    m, _ = menu(300, 38, 320, [('head', 'ROUTE LFO 1 TO'), ('item', 'Band Mix', None, set()), ('item', 'Band Volume', None, set()),
                                ('item', 'Slot 1 Mix', None, {'hover'}), ('item', 'Slot 1 &middot; Delay TAPE-201', '&#9656;', set()), ('sep',),
                                ('item', 'LEARN: move a knob in an effect&rsquo;s window&hellip;', None, set())], cursor_on='Slot 1 Mix')
    out += m
    out += caption(300, 248, 'LFO &middot; TRIGGER')
    m, _ = menu(300, 268, 320, [('head', 'TRIGGER &middot; BAND 1 &middot; ALL ITS LFOS'), ('item', 'SONG: follow the song (as before)', None, {'sel', 'check'}),
                                 ('item', 'AUDIO: start when sound comes in', None, {'hover'}), ('sep',), ('head', 'LISTEN TO'),
                                 ('item', 'This band&rsquo;s sound', None, {'check'}), ('item', 'The full sound (all bands)', None, set()), ('sep',),
                                 ('head', 'A NEW SOUND RESTARTS THEM'), ('item', 'On every hit', None, {'check'}),
                                 ('item', 'After a pause (a beat of quiet)', None, set()), ('item', 'Threshold', '&minus;24 dB &#9656;', set())],
                    cursor_on='AUDIO: start when sound comes in')
    out += m
    out += caption(648, 18, 'LFO &middot; GRID BUTTON')
    m, _ = menu(648, 38, 200, [('head', 'GRID'), ('item', '1/4', None, set()), ('item', '1/8', None, set()), ('item', '1/16', None, {'sel', 'check'}),
                                ('item', '1/32', None, {'hover'}), ('item', '1/64', None, set()), ('sep',), ('item', 'Triplet', None, set()),
                                ('item', 'Snap to grid', None, {'check'})], cursor_on='1/32')
    out += m
    out += caption(648, 330, 'LFO &middot; LENGTH (1 BAR)')
    out += columns_menu(520, 350, [('TRIPLET', ['1/16T', '1/8T', '1/4T', '1/2T']),
                                   ('STRAIGHT', ['1/128', '1/64', '1/32', '1/16', '1/8', '1/4', '1/2', '1 Bar', '2 Bars', '4 Bars', '8 Bars', '16 Bars', '32 Bars']),
                                   ('DOTTED', ['1/16d', '1/8d', '1/4d', '3/4', '1.5 Bars', '3 Bars']),
                                   ('FREE (Hz)', ['0.1 Hz', '0.25 Hz', '0.5 Hz', '1 Hz', '2 Hz', '4 Hz', '8 Hz', '16 Hz'])], '1 Bar', '2 Bars')
    return out

def page(body, h):
    return (f'<!doctype html><html><head><meta charset="utf-8"><style>html,body{{margin:0;background:#140c1f}}'
            f'#win{{position:relative;width:{W}px;height:{h}px;overflow:hidden;font-family:{FONT};'
            f'background:linear-gradient(180deg,{C["bg1"]} 0%,{C["bg2"]} 55%,#251a3a 100%)}}</style></head>'
            f'<body><div id="win">{body}</div></body></html>')

if __name__ == '__main__':
    from playwright.sync_api import sync_playwright
    out_dir = sys.argv[1]
    with sync_playwright() as p:
        browser = p.chromium.launch()
        for name, body, h in [('Menus 1', sheet1(), 720), ('Menus 2', sheet2(), 760)]:
            pg = browser.new_page(viewport={'width': W, 'height': h}, device_scale_factor=2)
            html = os.path.join(out_dir, name + '.html')
            open(html, 'w').write(page(body, h))
            pg.goto('file://' + html)
            pg.wait_for_timeout(200)
            pg.locator('#win').screenshot(path=os.path.join(out_dir, name + '.png'))
            pg.close()
        browser.close()
    print('ok')
