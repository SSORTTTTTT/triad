# Every menu in Triad, Triad Time and Triad Split in the new look (2026-10-26), with the words the code uses now;
# the COLOURS (Custom) panel in the same layout, cleaner; and BANDS with Plum's own dark band area.
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from build import C, box, text, button, FONT
import build5
from build5 import W, menu, MENU_BG, ROW, HEAD, SEP, CURSOR, TYPES, bands_now, dim_overlay
from build4 import BAND
from build6 import chips
from build7 import THEMES, swatches, columns_menu

build5.OUTLINES = False
def swatches(name):
    # square colour previews (Thomas: squares, not circles)
    return ''.join(f'<span style="display:inline-block;width:11px;height:11px;border-radius:2px;background:{c};border:1px solid rgba(255,255,255,0.35);box-sizing:border-box"></span>'
                   for c in THEMES[name])

_menu = menu
def menu(x, y, w, items, cursor_on=None):
    # the pointer kept clear of the numbers on the right of an item
    html, spots = _menu(x, y, w, items, None)
    if cursor_on and cursor_on in spots:
        sx, sy, sw = spots[cursor_on]
        right = next((it[2] for it in items if it[0] == 'item' and it[1] == cursor_on), None)
        html += CURSOR.format(x=sx + sw - (118 if right else 44), y=sy + 10)
    return html, spots

BANDS10 = ['#9bd85c', '#67c6b8', '#a39ce8', '#e8b15c', '#e87f8a', '#5ca8e8', '#d88ce0', '#c9d65c', '#5cdca2', '#e8955c']

def cap(x, y, s):
    return text(x, y, s, 11, C['dim'], 800, 'letter-spacing:0.5px;')

def note(x, y, w, s):
    return box(x, y, w, 0, s, 'height:auto;font-size:10.5px;font-weight:600;color:#1b1030;background:#f4eefc;border-radius:7px;'
                               'padding:8px 10px;box-sizing:border-box;line-height:1.35;')

def sub(spots, label, x, w, items, cursor_on=None, up=0):
    """A submenu opened to the side of an item: its first row level with the item (or moved up by 'up' rows)."""
    _, sy, _ = spots[label]
    return menu(x, sy - 6 - HEAD - up * ROW, w, items, cursor_on)

def hover(spots, label, s):
    """Mark an item in a parent menu as the one the submenu belongs to (a light box)."""
    x, y, w = spots[label]
    return ''

# ---- 1. Triad: the preset menu and the gear menu --------------------------------------------------------------
def sheet_top():
    out = cap(24, 18, 'TRIAD &middot; PRESET (CLICK THE PRESET NAME)')
    m, _ = menu(24, 38, 300, [('head', 'TRIAD PRESETS'), ('item', 'Save &ldquo;SSO 3 KV&rdquo;', None, set()),
                               ('item', 'Save as new preset&hellip;', None, set()), ('item', 'Initialize (an empty Triad)', None, set()), ('sep',),
                               ('item', 'SSO 2', None, set()), ('item', 'SSO 3 KV', None, {'sel', 'check'}),
                               ('item', 'SSORT FIRST PRESET', None, {'hover'}), ('sep',), ('item', 'Show presets folder', None, set())],
                cursor_on='SSORT FIRST PRESET')
    out += m
    out += cap(24, 330, 'GEAR &#9656; THEME')
    items = [('head', 'THEME')] + [('item', t, swatches(t), {'sel', 'check'} if t == 'Plum' else set()) for t in THEMES]
    items += [('item', 'My themes', '&#9656;', set()), ('item', 'Custom', None, set()), ('item', 'Edit colours&hellip;', None, {'hover'})]
    m, _ = menu(24, 350, 300, items, cursor_on='Edit colours&hellip;')
    out += m
    # the gear menu (as approved), with Startup preset open to the side
    out += cap(360, 18, 'TRIAD &middot; GEAR MENU')
    items = [('head', 'EFFECT LIST'), ('item', 'Sorting, folders and rescans', '&#9656;', set()), ('sep',),
             ('head', 'FAVORITES'), ('item', 'Favorites by type (DELAY, DISTORTION &hellip;)', None, {'check'}), ('sep',),
             ('head', 'WINDOW SIZE'), ('chipsrow', None, None, set()), ('sep',),
             ('head', 'COLOURS'), ('item', 'Theme', 'Plum &nbsp;&#9656;', set()), ('item', 'Edit the colours&hellip;', None, set()), ('sep',),
             ('head', 'LFO TAB: BEHIND THE GRID'), ('item', 'Bands on TRIGGER: SONG', 'Nothing &#9656;', set()),
             ('item', 'Bands on TRIGGER: AUDIO', 'Waveform &#9656;', set()), ('sep',),
             ('head', 'TRIAD TIME'), ('item', 'Behind the grid', 'Nothing &#9656;', set()), ('item', 'Pitch labels on the TIME lines', 'Dragging &#9656;', set()), ('sep',),
             ('head', 'PRESETS'), ('item', 'Startup preset', 'Init &#9656;', {'hover'}), ('item', 'Use the current settings as the startup preset', None, set()),
             ('item', 'Show presets folder', None, set()), ('item', 'Show chain presets folder', None, set())]
    flat = [it if it[0] != 'chipsrow' else ('item', '', None, set()) for it in items]
    x, y, w = 360, 38, 370
    m, spots = menu(x, y, w, flat)
    out += m
    yy = y + 6
    for it in items:
        if it[0] == 'chipsrow':
            out += chips(x + 14, yy + 2, w - 28, ['75%', '100%', '125%', '150%', '200%'], '100%')
        yy += {'head': HEAD, 'sep': SEP}.get(it[0], ROW)
    m, _ = sub(spots, 'Startup preset', x + w - 4, 330, [('head', 'STARTUP PRESET'), ('item', 'Init (an empty Triad)', None, {'sel', 'check'}),
                                                          ('item', 'My startup settings', None, set()), ('head', 'SAVED PRESETS'),
                                                          ('item', 'SSO 2', None, set()), ('item', 'SSO 3 KV', None, {'hover'}),
                                                          ('item', 'SSORT FIRST PRESET', None, set())], 'SSO 3 KV', up=3)
    out += m
    # EFFECT LIST's submenu, shown on its own
    out += cap(760, 18, 'GEAR &#9656; EFFECT LIST')
    m, _ = menu(760, 38, 316, [('head', 'EFFECT LIST'), ('item', 'Sort by company', None, set()), ('item', 'Sort A&ndash;Z by name', None, {'sel', 'check'}),
                                ('item', 'Look for new plug-ins each time Triad opens', None, {'check'}), ('sep',),
                                ('item', 'Rescan for new plug-ins', None, set()), ('item', 'Rescan everything', None, set()), ('sep',),
                                ('item', 'Add a plug-in folder&hellip;', None, set()), ('item', 'Remove a plug-in folder', '&#9656;', set()), ('sep',),
                                ('item', '412 effects &middot; list up to date', None, {'dim'})])
    out += m
    return out, 760

# ---- 2. Triad BANDS: choosing a plug-in, and a slot's menu -------------------------------------------------------
def sheet_plugins():
    out = cap(24, 18, 'TRIAD &middot; CLICK A SLOT: CHOOSE A PLUG-IN')
    items = [('title', 'Band 2 &nbsp;/&nbsp; Slot 1'), ('search', 'Search plug-ins&hellip;'), ('head', '&#9829; FAVORITES'),
             ('item', '&#9829; FAVORITES', '20 &nbsp;&#9656;', set())]
    for name, plugs in TYPES:
        items.append(('item', name, str(len(plugs)) + ' &nbsp;&#9656;', set()))
    items += [('sep',), ('head', 'BUILT IN'), ('item', 'Triad Time', 'TRIAD', set()), ('sep',), ('head', 'ALL PLUG-INS'),
              ('item', 'VST3', '214 &nbsp;&#9656;', {'hover'}), ('item', 'Audio Units', '187 &nbsp;&#9656;', set()), ('item', 'Not checked', '3 &nbsp;&#9656;', set()),
              ('sep',), ('item', 'Add an empty slot', None, set()), ('item', 'Browse for a plug-in file&hellip;', None, set()),
              ('item', 'Refresh plug-in list', None, set()), ('item', 'Rescan all plug-ins', None, set())]
    x, w = 24, 320
    m, spots = menu(x, 38, w, items)
    out += m
    makers = [('Arturia', '6'), ('Baby Audio', '4'), ('Cableguys', '3'), ('D16 Group', '5'), ('Eventide', '9'), ('FabFilter', '7'),
              ('Kilohearts', '14'), ('Soundtoys', '21'), ('Valhalla DSP', '6'), ('Other', '12')]
    m, s2 = sub(spots, 'VST3', x + w - 4, 220, [('head', 'VST3 &middot; BY COMPANY')] +
                [('item', n, c + ' &nbsp;&#9656;', {'hover'} if n == 'Baby Audio' else set()) for n, c in makers], up=5)
    out += m
    m, _ = sub(s2, 'Baby Audio', x + w + 212, 220, [('head', 'BABY AUDIO')] +
               [('item', n, None, {'hover'} if n == 'Spaced Out' else set()) for n in ['Comeback Kid', 'Crystalline', 'Smooth Operator', 'Spaced Out']],
               'Spaced Out')
    out += m
    out += note(x + w + 216, 520, 230, 'Sorted by company here; with &ldquo;Sort A&ndash;Z by name&rdquo; (gear menu) the plug-ins are listed by letter instead.')
    # a slot's menu
    out += cap(820, 18, 'TRIAD &middot; A SLOT&rsquo;S MENU')
    m, _ = menu(820, 38, 256, [('head', 'DELAY TAPE-201'), ('item', 'Open window', None, set()), ('item', 'Replace&hellip;', None, set()),
                                ('item', 'Edit LFO for this mix knob&hellip;', None, set()), ('item', 'Add to FAVORITES', None, {'heart'}), ('sep',),
                                ('item', 'Copy plugin state', None, {'hover'}), ('item', 'Paste plugin state', None, {'dim'}), ('sep',),
                                ('item', 'Move up', None, set()), ('item', 'Move down', None, {'dim'}), ('item', 'Bypass', None, set()), ('sep',),
                                ('item', 'Remove slot', None, set())], 'Copy plugin state')
    out += m
    return out, 760

# ---- 3. Triad BANDS: a band's menu, PRE / POST, the band display and its dividers ----------------------------------
SLOPES = [('6 dB/oct  (softest)', '0.3 ms'), ('12 dB/oct', '0.6 ms'), ('18 dB/oct', '0.9 ms'), ('24 dB/oct  (default)', '1.3 ms'),
          ('30 dB/oct', '1.6 ms'), ('36 dB/oct', '1.9 ms'), ('42 dB/oct', '2.2 ms'), ('48 dB/oct', '2.6 ms'), ('54 dB/oct', '2.9 ms'),
          ('60 dB/oct', '3.2 ms'), ('66 dB/oct', '3.5 ms'), ('72 dB/oct', '3.8 ms'), ('78 dB/oct', '4.2 ms'), ('84 dB/oct', '4.5 ms'),
          ('90 dB/oct', '4.8 ms'), ('96 dB/oct  (hardest)', '5.1 ms')]

def sheet_bands():
    out = cap(24, 18, 'TRIAD &middot; CLICK A BAND&rsquo;S NAME')
    x, w = 24, 330
    m, spots = menu(x, 38, w, [('head', 'BAND 2'), ('item', 'Copy band state', None, set()), ('item', 'Paste band state (Band 1: 3 effects)', None, set()),
                                ('item', 'Paste plugin (Delay TAPE-201) into a new slot', None, set()), ('sep',),
                                ('item', 'Save chain as preset&hellip;', None, set()), ('item', 'Load chain preset (replaces its effects)', '&#9656;', {'hover'}), ('sep',),
                                ('item', 'Edit LFOs for this band&hellip;', None, set()), ('item', 'Move effects to band 1', None, set()),
                                ('item', 'Move effects to band 3', None, set()), ('sep',), ('item', 'Remove band 2', None, set())])
    out += m
    m, _ = sub(spots, 'Load chain preset (replaces its effects)', x + w - 4, 210,
               [('head', 'CHAIN PRESETS'), ('item', 'Wide delays', None, set()), ('item', 'Tape wobble', None, set()),
                ('item', 'Crunch + verb', None, {'hover'}), ('sep',), ('item', 'Show chains folder', None, set())], 'Crunch + verb')
    out += m
    out += cap(24, 380, 'TRIAD &middot; RIGHT-CLICK A BAND (IN THE BAND DISPLAY)')
    m, _ = menu(24, 400, 300, [('head', 'BAND 1'), ('item', 'Copy band state', None, set()), ('item', 'Paste band state', None, {'dim'}), ('sep',),
                                ('item', 'Remove band 1', None, set()), ('item', 'Add a band here (62 Hz)', None, {'hover'}),
                                ('item', 'Edit LFO for band 1 mix&hellip;', None, set()), ('sep',), ('head', 'ALL DIVIDERS'),
                                ('item', 'Hard wall', '96 dB/oct', set()), ('item', 'Default', '24 dB/oct', set()), ('item', 'Soft', '6 dB/oct', set())],
                'Add a band here (62 Hz)')
    out += m
    out += cap(360, 380, 'RIGHT-CLICK A DIVIDER')
    m, _ = menu(360, 400, 260, [('head', 'CROSSOVER 1 &nbsp;&middot;&nbsp; 250 Hz'), ('item', 'Slope', '24 dB/oct &#9656;', set()), ('sep',),
                                 ('item', 'Remove band 1 (below)', None, set()), ('item', 'Remove band 2 (above)', None, {'hover'}),
                                 ('item', 'Reset this crossover', None, set())], 'Remove band 2 (above)')
    out += m
    out += cap(360, 580, 'CLICK PRE OR POST&rsquo;S NAME')
    m, _ = menu(360, 600, 370, [('head', 'PRE &nbsp;(FULL RANGE, BEFORE THE BANDS)'), ('item', 'Paste plugin (Delay TAPE-201) into a new slot', None, set()),
                                 ('item', 'Edit LFOs for PRE&hellip;', None, set()), ('sep',), ('item', 'Remove all PRE effects', None, set()),
                                 ('item', 'Hide PRE', None, {'dim'})])
    out += m
    out += cap(770, 18, 'CLICK A SLOPE (24 dB/oct &#8964;)')
    items = [('head', 'CROSSOVER 1 SLOPE'), ('head', 'RIGHT: 250 HZ COMES OUT LATER BY')]
    for i, (lab, ms) in enumerate(SLOPES):
        if i == 8: items.append(('head', 'HARDER CUTS (NEAR BRICKWALL)'))
        items.append(('item', lab, ms, {'sel', 'check'} if i == 3 else {'hover'} if i == 5 else set()))
    items += [('sep',), ('head', 'ALL DIVIDERS'), ('item', 'Hard wall', '96 dB/oct', set()), ('item', 'Default', '24 dB/oct', set()),
              ('item', 'Soft', '6 dB/oct', set())]
    m, _ = menu(770, 38, 306, items, '36 dB/oct')
    out += m
    return out, 780

# ---- 4. Triad LFO tab: routing (with LEARN), trigger, the grid ---------------------------------------------------
def sheet_lfo():
    out = cap(24, 18, 'TRIAD &middot; LFO: + ROUTE TO')
    x, w = 24, 330
    m, spots = menu(x, 38, w, [('head', 'ROUTE LFO 1 TO'), ('item', 'Band Mix', None, set()), ('item', 'Band Volume', None, set()),
                                ('item', 'Slot 1 Mix &middot; Delay TAPE-201', None, set()), ('item', 'Slot 2 Mix &middot; Reverb', None, set()),
                                ('head', 'KNOBS INSIDE EFFECTS'), ('item', '1 &middot; Delay TAPE-201', '24 &nbsp;&#9656;', set()),
                                ('item', '2 &middot; Reverb', '12 &nbsp;&#9656;', set()), ('sep',),
                                ('item', 'LEARN: move a knob in an effect&rsquo;s window', '&#9656;', {'hover'})])
    out += m
    m, _ = sub(spots, 'LEARN: move a knob in an effect&rsquo;s window', x + w - 4, 300,
               [('head', 'OPEN ITS WINDOW, THEN MOVE A KNOB'), ('item', '1 &middot; Delay TAPE-201', None, {'hover'}),
                ('item', '2 &middot; Reverb', 'open', set())], '1 &middot; Delay TAPE-201')
    out += m
    out += note(x + w, 345, 300, 'NEW: LEARN opens the effect&rsquo;s window if it is closed. If it is open already it is left exactly as it is '
                                 '(the page it is on, its settings). With one effect, LEARN opens it straight away.')
    out += cap(24, 430, 'LFO &middot; TRIGGER')
    m, spots = menu(24, 450, 330, [('head', 'TRIGGER &middot; BAND 1 &middot; ALL ITS LFOS'), ('item', 'SONG: follow the song (as before)', None, set()),
                                    ('item', 'AUDIO: start when sound comes in', None, {'sel', 'check'}), ('head', 'LISTEN TO'),
                                    ('item', 'This band&rsquo;s sound', None, {'check'}), ('item', 'The full sound (all bands)', None, set()),
                                    ('head', 'A NEW SOUND RESTARTS THEM'), ('item', 'On every hit', None, {'check'}),
                                    ('item', 'After a pause (a beat of quiet)', None, set()), ('item', 'Threshold', '&minus;24 dB &#9656;', {'hover'})])
    out += m
    m, _ = sub(spots, 'Threshold', 350, 130, [('head', 'THRESHOLD')] +
               [('item', f'&minus;{d} dB', None, {'sel', 'check'} if d == 24 else {'hover'} if d == 18 else set()) for d in (60, 54, 48, 42, 36, 30, 24, 18, 12, 6, 3)],
               '&minus;18 dB', up=8)
    out += m
    out += cap(700, 18, 'LFO &middot; RIGHT-CLICK THE GRID')
    m, _ = menu(700, 38, 250, [('head', 'WAVE'), ('item', 'Select all points', None, set()), ('item', 'Move points to grid', None, set()),
                                ('item', 'Flip horizontal', None, set()), ('item', 'Flip vertical', None, set()), ('item', 'Copy wave', None, {'hover'}),
                                ('item', 'Paste wave', None, set()), ('item', 'Delete selected points', None, set()), ('sep',), ('head', 'GRID'),
                                ('item', 'Triplet', None, set()), ('item', 'Snap to grid', None, {'check'}), ('item', 'Grid 1/4', None, set()),
                                ('item', 'Grid 1/8', None, set()), ('item', 'Grid 1/16', None, {'sel', 'check'}), ('item', 'Grid 1/32', None, set()),
                                ('item', 'Grid 1/64', None, set())], 'Copy wave')
    out += m
    out += cap(700, 530, 'LFO &middot; THE GRID BUTTON')
    m, _ = menu(700, 550, 200, [('head', 'GRID'), ('item', '1/4', None, set()), ('item', '1/8', None, set()), ('item', '1/16', None, {'sel', 'check'}),
                                 ('item', '1/32', None, {'hover'}), ('item', '1/64', None, set()), ('sep',), ('item', 'Triplet', None, set()),
                                 ('item', 'Snap to grid', None, {'check'})], '1/32')
    out += m
    return out, 800

# ---- 5. The shape browser (Triad's LFO tab and Triad Time) ---------------------------------------------------------
def sheet_shapes():
    out = cap(24, 18, 'LFO AND TRIAD TIME &middot; LENGTH (1 BAR &#8964;)')
    out += columns_menu(24, 38, [('TRIPLET', ['1/16T', '1/8T', '1/4T', '1/2T']),
                                 ('STRAIGHT', ['1/128', '1/64', '1/32', '1/16', '1/8', '1/4', '1/2', '1 Bar', '2 Bars', '4 Bars', '8 Bars', '16 Bars', '32 Bars']),
                                 ('DOTTED', ['1/16d', '1/8d', '1/4d', '3/4', '1.5 Bars', '3 Bars']),
                                 ('FREE (Hz)', ['0.1 Hz', '0.25 Hz', '0.5 Hz', '1 Hz', '2 Hz', '4 Hz', '8 Hz', '16 Hz'])], '1 Bar', '2 Bars')
    out += cap(24, 430, 'CLICK &ldquo;3 / 40&rdquo;: GO TO A PAGE')
    m, _ = menu(24, 450, 260, [('search', 'Page number&hellip;'), ('head', 'JUMP TO A FOLDER'), ('item', 'BASIC', 'p. 1', set()),
                                ('item', 'SQUARES', 'p. 4', set()), ('item', 'STEPS', 'p. 8', {'sel', 'check'}), ('item', 'TRIANGLES', 'p. 12', {'hover'}),
                                ('item', 'ANGLES', 'p. 15', set()), ('item', 'HARD + CURVE', 'p. 19', set()), ('item', 'SOFT', 'p. 22', set())], 'TRIANGLES')
    out += m
    out += cap(310, 430, 'RIGHT-CLICK ONE OF YOUR SHAPES')
    m, _ = menu(310, 450, 240, [('head', 'MY WOBBLE 3'), ('item', 'Rename&hellip;', None, {'hover'}), ('item', 'Delete', None, set()), ('sep',),
                                 ('item', 'Show the USER folder', None, set())], 'Rename&hellip;')
    out += m
    out += cap(310, 600, 'GENERATE &middot; THE THUMBS')
    m, _ = menu(310, 620, 330, [('head', 'YOUR TASTE IN LFO SHAPES: 12 LIKED, 3 DISLIKED'), ('item', 'Forget what I liked and disliked', None, {'hover'})],
                'Forget what I liked and disliked')
    out += m
    out += cap(620, 18, 'LFO &middot; THE FOLDER BUTTON (STEPS &#9662;)')
    x, w = 620, 270
    items = [('item', '&#9829; FAVORITES', '18', set()), ('item', 'USER', '6', set()), ('sep',), ('head', 'LFO SHAPES BY TYPE')]
    for n, c in [('BASIC', '24'), ('SQUARES', '40'), ('STEPS', '36'), ('TRIANGLES', '28'), ('ANGLES', '32'), ('HARD + CURVE', '30'), ('SOFT', '26')]:
        items.append(('item', n, c, {'sel', 'check'} if n == 'STEPS' else set()))
    items += [('head', 'GENERATED (FROM THE DICE)'), ('item', 'TRIPLETS', '120', set()), ('item', 'SWING', '80', set()),
              ('head', 'EXTRA'), ('item', 'EVERY PREVIEW SHAPE', '600', set()), ('sep',),
              ('item', 'TRIAD TIME PATTERNS', '&#9656;', {'hover'}), ('sep',), ('head', 'YOUR SHAPES'),
              ('item', 'Save this shape to USER&hellip;', None, set()), ('item', 'Show the USER folder', None, set())]
    m, spots = menu(x, 38, w, items)
    out += m
    m, _ = sub(spots, 'TRIAD TIME PATTERNS', x + w - 4, 200,
               [('head', 'TRIAD TIME'), ('item', 'OCTAVE DOWN', '2,000', set()), ('item', 'OCTAVE UP', '2,000', set()),
                ('item', 'REVERSE', '2,000', {'hover'}), ('item', 'CHOPS', '2,000', set()), ('item', 'PITCH + CHOPS', '2,000', set())], 'REVERSE', up=2)
    out += m
    out += note(660, 600, 300, 'In Triad Time this button lists its own TIME folders first, with LFO SHAPES &#9656; one menu away.')
    return out, 780

# ---- 6. Triad MATRIX, Triad Split, and the small windows ----------------------------------------------------------
def sheet_matrix_split():
    out = cap(24, 18, 'TRIAD &middot; MATRIX: SOURCE')
    m, _ = menu(24, 38, 230, [('head', 'SOURCE'), ('item', 'LFO 1 (Slot 1)', None, {'sel', 'check'}), ('item', 'LFO 2 (Band Mix)', None, set()),
                               ('sep',), ('item', 'Edit this LFO&hellip;', None, {'hover'})], 'Edit this LFO&hellip;')
    out += m
    out += cap(280, 18, 'MATRIX: DESTINATION')
    x, w = 280, 300
    m, spots = menu(x, 38, w, [('head', 'DESTINATION'), ('item', 'Band Mix', None, set()), ('item', 'Band Volume', None, {'sel', 'check'}),
                                ('item', 'Slot 1 Mix &middot; Delay TAPE-201', None, set()), ('item', 'Slot 2 Mix &middot; Reverb', None, set()),
                                ('head', 'KNOBS INSIDE EFFECTS'), ('item', '1 &middot; Delay TAPE-201', '24 &nbsp;&#9656;', {'hover'}),
                                ('item', '2 &middot; Reverb', '12 &nbsp;&#9656;', set()), ('sep',),
                                ('item', 'LEARN: move a knob in an effect&rsquo;s window', '&#9656;', set())])
    out += m
    m, s2 = sub(spots, '1 &middot; Delay TAPE-201', x + w - 4, 210,
                [('head', 'DELAY TAPE-201'), ('item', 'Mix &hellip; Feedback', '&#9656;', {'hover'}), ('item', 'Tone &hellip; Wow', '&#9656;', set()),
                 ('item', 'Flutter &hellip; Output', '&#9656;', set())])
    out += m
    m, _ = sub(s2, 'Mix &hellip; Feedback', x + w + 202, 190,
               [('head', 'MIX &hellip; FEEDBACK'), ('item', 'Mix', None, set()), ('item', 'Time', None, set()), ('item', 'Time sync', None, set()),
                ('item', 'Feedback', None, {'hover'}), ('item', 'Ping-pong', None, set()), ('item', 'Width', None, set())], 'Feedback')
    out += m
    # Triad Split
    out += cap(24, 380, 'TRIAD SPLIT &middot; GEAR MENU')
    items = [('head', 'THEME')] + [('item', t, swatches(t), {'sel', 'check'} if t == 'Plum' else set()) for t in THEMES]
    items += [('item', 'My themes', '&#9656;', set()), ('item', 'Custom', None, set()), ('item', 'Edit colours&hellip;', None, set())]
    m, _ = menu(24, 400, 280, items)
    out += m
    out += cap(330, 380, 'TRIAD SPLIT &middot; LINK')
    items = [('head', 'LINK: SHARE THESE BANDS WITH'), ('item', 'OFF (this Triad Split keeps its own bands)', None, set()),
             ('item', 'Group A', '1 other', {'sel', 'check'}), ('item', 'Group B', '2 others', {'hover'})]
    items += [('item', 'Group ' + g, None, set()) for g in 'CDEFGH']
    m, _ = menu(330, 400, 330, items, 'Group B')
    out += m
    out += note(330, 700, 330, 'Triad Split&rsquo;s band display has the same right-click menus as Triad&rsquo;s (sheet 3).')
    # the name box (Save as new preset, Save chain, Rename) and a tooltip
    out += cap(690, 380, 'SAVE AS NEW PRESET (AND SAVE CHAIN, RENAME)')
    px, py = 690, 400
    out += box(px, py, 386, 150, '', MENU_BG)
    out += text(px + 18, py + 16, 'SAVE AS NEW PRESET', 10, C['pink'], 800, 'letter-spacing:0.6px;')
    out += box(px + 18, py + 42, 350, 34, '', 'background:#170f24;border:1.5px solid #9b7dff;border-radius:8px;box-sizing:border-box;')
    out += text(px + 30, py + 51, 'SSO 4 wide delays<span style="color:#c4b2ff">|</span>', 13.5, C['text'], 700)
    out += button(px + 196, py + 100, 80, 30, 'CANCEL', '#3a2a54', C['text'], 12, 7)
    out += button(px + 288, py + 100, 80, 30, 'SAVE', C['lav'], '#1b1030', 12.5, 7)
    out += cap(690, 580, 'TOOLTIPS')
    out += box(690, 600, 300, 0, 'Send this LFO to another knob in this band: a mix, the band&rsquo;s volume, or a knob inside one of its effects',
               'height:auto;background:#2d2147;border:1px solid rgba(196,178,255,0.35);border-radius:8px;box-shadow:0 8px 20px rgba(0,0,0,0.5);'
               'padding:8px 11px;box-sizing:border-box;font-size:11.5px;font-weight:600;color:#f4eefc;line-height:1.35;')
    out += note(690, 700, 300, 'The Mac&rsquo;s own windows (Browse for a plug-in file, Add a plug-in folder) stay as they are: macOS draws them.')
    return out, 790

# ---- 7. Triad Time ----------------------------------------------------------------------------------------------
def sheet_time():
    out = cap(24, 18, 'TRIAD TIME &middot; GEAR MENU')
    items = [('head', 'PITCH LABELS (TIME)'), ('item', 'Off', None, set()), ('item', 'While dragging a point', None, set()),
             ('item', 'Always, on every line', None, {'sel', 'check'}), ('item', 'FWD / REV: which way it plays; st: semitones', None, {'dim'}), ('sep',),
             ('head', 'BEHIND THE GRID'), ('item', 'Nothing', None, {'check'}), ('item', 'The waveform (the sound coming in)', None, {'hover'}),
             ('item', 'The spectrum (as Triad&rsquo;s, Pro-Q style)', None, set()), ('sep',), ('head', 'THEME')]
    items += [('item', t, swatches(t), {'sel', 'check'} if t == 'Plum' else set()) for t in THEMES]
    items += [('item', 'My themes', '&#9656;', set()), ('item', 'Custom', None, set()), ('item', 'Edit colours&hellip;', None, set())]
    m, _ = menu(24, 38, 360, items, 'The waveform (the sound coming in)')
    out += m
    out += cap(420, 18, 'TRIAD TIME &middot; THE FOLDER BUTTON (OCTAVE DOWN &#9662;)')
    x, w = 420, 250
    items = [('item', '&#9829; FAVORITES', '12', set()), ('item', 'USER', '4', set()), ('sep',), ('head', 'TIME')]
    for n in ['OCTAVE DOWN', 'OCTAVE UP', 'REVERSE', 'CHOPS', 'PITCH + CHOPS']:
        items.append(('item', n, '2,000', {'sel', 'check'} if n == 'OCTAVE DOWN' else set()))
    items += [('sep',), ('item', 'LFO SHAPES', '&#9656;', {'hover'}), ('sep',), ('head', 'YOUR SHAPES'),
              ('item', 'Save this pattern to USER&hellip;', None, set()), ('item', 'Show the USER folder', None, set())]
    m, spots = menu(x, 38, w, items)
    out += m
    m, _ = sub(spots, 'LFO SHAPES', x + w - 4, 200, [('head', 'LFO SHAPES')] +
               [('item', n, c, {'hover'} if n == 'TRIANGLES' else set()) for n, c in
                [('BASIC', '24'), ('SQUARES', '40'), ('STEPS', '36'), ('TRIANGLES', '28'), ('ANGLES', '32'), ('HARD + CURVE', '30'), ('SOFT', '26'),
                 ('GENERATED', '200'), ('EXTRA', '600')]], 'TRIANGLES', up=4)
    out += m
    out += note(420, 520, 420, 'Triad Time&rsquo;s other menus are the same as the LFO tab&rsquo;s: right-click the grid, GRID, the length, '
                               'go to a page, your shapes, the thumbs (sheets 4 and 5). The VOLUME tab is the same as TIME.')
    return out, 640

# ---- 8. COLOURS (Custom): the same layout, cleaner ----------------------------------------------------------------
ROLES = [('Background', '#2a1f30'), ('Panels', '#1e1624'), ('Buttons and slots', '#382a40'), ('Grids and displays', '#140d18'),
         ('Grid lines', '#e2d2ea'), ('Text', '#f2eaf6'), ('Accent (titles, lit buttons)', '#ff6bd0'), ('Triad Time: TIME line', '#8f6bff'),
         ('Triad Time: VOLUME line', '#ffb0cf')]

def dice(x, y, s=18, col='#f4eefc'):
    dots = [(0.28, 0.28), (0.72, 0.28), (0.5, 0.5), (0.28, 0.72), (0.72, 0.72)]
    d = ''.join(f'<circle cx="{s * a:.1f}" cy="{s * b:.1f}" r="{s * 0.08:.1f}" fill="{col}"/>' for a, b in dots)
    return (f'<svg style="position:absolute;left:{x}px;top:{y}px" width="{s}" height="{s}" viewBox="0 0 {s} {s}">'
            f'<rect x="1" y="1" width="{s - 2}" height="{s - 2}" rx="4" fill="none" stroke="{col}" stroke-width="1.8"/>{d}</svg>')

def undo_icon(x, y, col):
    return (f'<svg style="position:absolute;left:{x}px;top:{y}px" width="16" height="16" viewBox="0 0 16 16"><path d="M5 4 L2 7 L5 10" fill="none" '
            f'stroke="{col}" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/><path d="M2.5 7 H10 a3.5 3.5 0 0 1 0 7 H7" fill="none" '
            f'stroke="{col}" stroke-width="1.8" stroke-linecap="round"/></svg>')

def value_box(x, y, w, label, value, frac, col):
    out = box(x, y, w, 44, '', 'background:#43335f;border-radius:8px;')
    out += text(x + 10, y + 7, label, 9.5, C['caps'], 800, 'letter-spacing:0.5px;')
    out += text(x + w - 44, y + 6, value, 13, C['text'], 800, 'width:34px;text-align:right;')
    out += box(x + 10, y + 31, w - 20, 4, '', 'background:rgba(255,255,255,0.12);border-radius:2px;')
    out += box(x + 10, y + 31, (w - 20) * frac, 4, '', f'background:{col};border-radius:2px;')
    return out

def colours_panel(menu_open=False):
    out = plum(bands_now()) + dim_overlay()
    px, py, pw, ph = 140, 96, 820, 568
    out += box(px, py, pw, ph, '', 'background:#2d2147;border:1px solid rgba(196,178,255,0.4);border-radius:14px;box-shadow:0 20px 50px rgba(0,0,0,0.65);box-sizing:border-box;')
    out += text(px + 24, py + 20, 'COLOURS', 17, C['text'], 800, 'letter-spacing:0.5px;')
    out += text(px + 128, py + 24, 'Triad, Triad Time and Triad Split share them', 12, C['dim'], 600)
    out += text(px + pw - 300, py + 24, 'START FROM', 10, C['caps'], 800, 'letter-spacing:0.6px;')
    out += box(px + pw - 214, py + 16, 190, 32, '', 'background:#43335f;border-radius:8px;' + ('border:1.5px solid #c4b2ff;box-sizing:border-box;' if menu_open else ''))
    out += text(px + pw - 200, py + 24, 'Plum', 13.5, C['text'], 800)
    out += box(px + pw - 150, py + 27, 60, 10, swatches('Plum'), 'display:flex;gap:3px;')
    out += text(px + pw - 44, py + 23, '&#9662;', 13, C['text'])
    # left: the nine colours
    lx, ly, lw = px + 24, py + 70, 380
    out += text(lx, ly, 'THEME COLOURS', 10, C['pink'], 800, 'letter-spacing:0.6px;')
    for i, (name, hexv) in enumerate(ROLES):
        ry = ly + 20 + i * 33
        sel = i == 6
        if sel: out += box(lx - 6, ry, lw + 12, 32, '', f'background:{C["lav"]};border-radius:8px;')
        out += box(lx + 2, ry + 6, 20, 20, '', f'background:{hexv};border-radius:4px;border:1.5px solid rgba(255,255,255,0.45);box-sizing:border-box;')
        out += text(lx + 34, ry + 8, name, 12.5, '#1b1030' if sel else C['text'], 800 if sel else 600)
        out += box(lx + lw - 122, ry + 4, 88, 24, '', 'background:#170f24;border-radius:6px;' + ('border:1px solid #1b1030;' if sel else 'border:1px solid rgba(196,178,255,0.22);') + 'box-sizing:border-box;')
        out += text(lx + lw - 112, ry + 9, hexv.upper(), 11.5, C['text'], 700, 'font-family:Menlo,monospace;')
        out += undo_icon(lx + lw - 22, ry + 8, '#1b1030' if sel else C['dim'])
    by = ly + 20 + 9 * 33 + 16
    out += text(lx, by, 'BAND COLOURS (KEPT FOR THE BANDS)', 10, C['pink'], 800, 'letter-spacing:0.6px;')
    for b, col in enumerate(BANDS10):
        out += box(lx + b * 34, by + 20, 26, 26, str(b + 1), f'background:{col};border-radius:5px;color:#1b1030;font-size:10.5px;font-weight:800;'
                                                           'display:flex;align-items:center;justify-content:center;')
    # right: the picker for the chosen colour
    rx, ry0, rw = px + 440, py + 70, 356
    out += text(rx, ry0, 'ACCENT (TITLES, LIT BUTTONS)', 10, C['pink'], 800, 'letter-spacing:0.6px;')
    out += box(rx, ry0 + 20, rw, 30, '#FF6BD0', 'background:#ff6bd0;border-radius:8px;color:#1b1030;font-size:13px;font-weight:800;'
                                                'display:flex;align-items:center;justify-content:center;font-family:Menlo,monospace;')
    sq_y = ry0 + 60
    out += box(rx, sq_y, rw, 150, '', 'border-radius:8px;background:linear-gradient(to top,#000,rgba(0,0,0,0)),linear-gradient(to right,#fff,hsl(318,100%,50%));')
    out += box(rx + rw * 0.58 - 8, sq_y + 2, 16, 16, '', 'border:2.5px solid #fff;border-radius:9px;box-shadow:0 0 0 1px rgba(0,0,0,0.5);box-sizing:border-box;')
    hy = sq_y + 160
    out += box(rx, hy, rw, 14, '', 'border-radius:7px;background:linear-gradient(to right,#f00,#ff0,#0f0,#0ff,#00f,#f0f,#f00);')
    hx = rx + rw * 318 / 360
    out += box(hx - 7, hy - 3, 14, 20, '', 'background:#ff00c0;border:2.5px solid #fff;border-radius:6px;box-shadow:0 0 0 1px rgba(0,0,0,0.5);box-sizing:border-box;')
    vy = hy + 26
    vw = (rw - 16) / 3
    out += value_box(rx, vy, vw, 'RED', '255', 1.0, '#ff5c6a')
    out += value_box(rx + vw + 8, vy, vw, 'GREEN', '107', 107 / 255, '#7ce07c')
    out += value_box(rx + 2 * (vw + 8), vy, vw, 'BLUE', '208', 208 / 255, '#6aa8ff')
    out += box(rx, vy + 56, rw, 36, '', 'background:#43335f;border:1px solid rgba(255,107,208,0.7);border-radius:8px;box-sizing:border-box;')
    out += dice(rx + rw / 2 - 92, vy + 65, 18) + text(rx + rw / 2 - 64, vy + 66, 'RANDOM COLOURWAY', 13, C['text'], 800)
    # bottom: the note and the buttons
    out += box(px + 24, py + ph - 92, pw - 48, 1, '', 'background:rgba(255,255,255,0.1);')
    out += text(px + 24, py + ph - 80, 'The theme you leave here is used by every Triad, Triad Time and Triad Split. Band colours never change; theme colours keep clear of them.',
                11, C['dim'], 600)
    bb = py + ph - 50
    out += box(px + 24, bb, 140, 32, 'MAKE READABLE', 'border:1px solid rgba(196,178,255,0.25);border-radius:8px;color:rgba(244,238,252,0.35);'
                                                      'font-size:12px;font-weight:800;display:flex;align-items:center;justify-content:center;box-sizing:border-box;')
    out += box(px + pw - 512, bb, 180, 32, '', 'background:#170f24;border:1px solid rgba(196,178,255,0.3);border-radius:8px;box-sizing:border-box;')
    out += text(px + pw - 500, bb + 9, 'My plum', 12.5, C['text'], 600)
    out += button(px + pw - 324, bb, 92, 32, 'SAVE AS', '#43335f', C['text'], 12, 8)
    out += button(px + pw - 220, bb, 92, 32, 'CANCEL', '#3a2a54', C['text'], 12, 8)
    out += button(px + pw - 116, bb, 92, 32, 'DONE', C['lav'], '#1b1030', 12.5, 8)
    if menu_open:
        items = [('head', 'READY-MADE')] + [('item', t, swatches(t), {'sel', 'check'} if t == 'Plum' else {'hover'} if t == 'Midnight' else set())
                                            for t in THEMES]
        items += [('head', 'MY THEMES'), ('item', 'Plum dark', swatches('Plum'), set()), ('item', 'Night drive', swatches('Midnight'), set()),
                  ('sep',), ('item', 'Custom', swatches('Black'), set())]
        m, _ = menu(px + pw - 300, py + 52, 276, items, 'Midnight')
        out += m
    return out

# ---- 0. BANDS with Plum's own dark band area --------------------------------------------------------------------
PLUM_SWAPS = [('#1d1430', '#140d18'), ('#2b1f42', '#1e1624'), ('#1b1329', '#140d18'), ('#140d1f', '#0c080e'), ('#4a3768', '#382a40'),
              ('#3a2a54', '#382a40')]

def plum(html):
    for a, b in PLUM_SWAPS:
        html = html.replace(a, b)
    return html

def page(body, h, plum_bg=False):
    bg = '#2a1f30' if plum_bg else f'linear-gradient(180deg,{C["bg1"]} 0%,{C["bg2"]} 55%,#251a3a 100%)'
    return (f'<!doctype html><html><head><meta charset="utf-8"><style>html,body{{margin:0;background:#140c1f}}'
            f'#win{{position:relative;width:{W}px;height:{h}px;overflow:hidden;font-family:{FONT};background:{bg}}}</style></head>'
            f'<body><div id="win">{body}</div></body></html>')

if __name__ == '__main__':
    from playwright.sync_api import sync_playwright
    out_dir = sys.argv[1]
    shots = [('0 BANDS - Plum dark band area', plum(bands_now()), 700, True),
             ('1 Menus - preset and gear', *sheet_top(), False),
             ('2 Menus - plug-ins and slots', *sheet_plugins(), False),
             ('3 Menus - bands, dividers, slopes', *sheet_bands(), False),
             ('4 Menus - LFO routing, trigger, grid', *sheet_lfo(), False),
             ('5 Menus - shape browser', *sheet_shapes(), False),
             ('6 Menus - matrix, Triad Split, small windows', *sheet_matrix_split(), False),
             ('7 Menus - Triad Time', *sheet_time(), False),
             ('8 COLOURS - Custom', colours_panel(False), 760, True),
             ('9 COLOURS - Start from open', colours_panel(True), 760, True)]
    only = sys.argv[2:] or None
    with sync_playwright() as p:
        browser = p.chromium.launch()
        for name, body, h, pb in shots:
            if only and not any(name.startswith(o) for o in only): continue
            pg = browser.new_page(viewport={'width': W, 'height': h}, device_scale_factor=2)
            html = os.path.join(out_dir, name + '.html')
            open(html, 'w').write(page(body, h, pb))
            pg.goto('file://' + html)
            pg.wait_for_timeout(200)
            pg.locator('#win').screenshot(path=os.path.join(out_dir, name + '.png'))
            pg.close()
        browser.close()
    print('ok')
