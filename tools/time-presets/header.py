# Writes Source/TimePresets.h from batch.json.
import json, sys
batch = json.load(open('batch.json'))
folders = []
for b in batch:
    if b['folder'] not in folders: folders.append(b['folder'])
short = {'OCTAVE DOWN': 'Down', 'OCTAVE UP': 'Up', 'REVERSE': 'Reverse', 'CHOPS': 'Chop', 'PITCH + CHOPS': 'Pitch chop'}
lines = []
lines.append('#pragma once')
lines.append('#include "Modulation.h"')
lines.append('')
lines.append('// Triad Time\'s TIME folders (0.13.0): %d patterns from a generator trained on the time envelopes of Thomas\'s Gross Beat' % len(batch))
lines.append('// banks (2,183 different patterns: how long the runs between jumps are, what each run does, how the jumps go). Each')
lines.append('// folder leans on what Triad Time is best at (Thomas): repitching forwards or backwards by an octave or two, and')
lines.append('// 90-degree chops. Every pattern is new (none within 0.12 beats on average of a pattern in the banks), on Triad Time\'s')
lines.append('// grid (1/64 bar across, 1/16 bar down) and within one bar back. Made by tools/time-presets (generate.py, header.py).')
lines.append('namespace triad::timePresets')
lines.append('{')
lines.append('struct Point { float x, y, curve; };')
lines.append('struct Entry { const char* name; int folder; int first, count; };')
lines.append('inline const char* const folderNames[] { %s };' % ', '.join('"%s"' % f for f in folders))
lines.append('inline constexpr int numFolders = %d;' % len(folders))
pts, entries = [], []
counter = {}
for b in batch:
    f = folders.index(b['folder'])
    counter[f] = counter.get(f, 0) + 1
    import math
    moves = []
    for (t0, t1, d0, d1, kind, sp) in b['segs']:
        if kind == 'ramp':
            m = 'FREEZE' if abs(sp) < 1e-3 else ('REV ' if sp < 0 else 'FWD ') + (('%+d' % round(12 * math.log2(abs(sp)))) if round(12 * math.log2(abs(sp))) else '0')
        elif kind == 'curve': m = 'TAPE'
        else: continue
        if m not in moves: moves.append(m)
    name = '%s %02d' % (short[b['folder']], counter[f]) + (': ' + ', '.join(moves) if moves else '')
    first = len(pts)
    for (x, y, c) in b['shape']:
        pts.append((x, y, c))
    entries.append((name.replace('"', ''), f, first, len(b['shape'])))
lines.append('inline const Point points[] {')
row = []
for i, (x, y, c) in enumerate(pts):
    row.append('{ %sf, %sf, %sf }' % (repr(round(x, 6)), repr(round(y, 6)), repr(round(c, 4))))
    if len(row) == 6: lines.append('    ' + ', '.join(row) + ','); row = []
if row: lines.append('    ' + ', '.join(row) + ',')
lines.append('};')
lines.append('inline const Entry entries[] {')
for e in entries:
    lines.append('    { "%s", %d, %d, %d },' % e)
lines.append('};')
lines.append('inline constexpr int numPresets = %d;' % len(entries))
lines.append('inline triad::mod::Shape shape (int index)')
lines.append('{')
lines.append('    const auto& e = entries[juce::jlimit (0, numPresets - 1, index)];')
lines.append('    triad::mod::Shape s;')
lines.append('    for (int i = 0; i < e.count; ++i) s.push_back ({ points[e.first + i].x, points[e.first + i].y, points[e.first + i].curve });')
lines.append('    return s;')
lines.append('}')
lines.append('inline std::vector<int> presetsIn (int folder)')
lines.append('{')
lines.append('    std::vector<int> list;')
lines.append('    for (int i = 0; i < numPresets; ++i) if (entries[i].folder == folder) list.push_back (i);')
lines.append('    return list;')
lines.append('}')
lines.append('}')
open(sys.argv[1], 'w').write('\n'.join(lines) + '\n')
print(len(entries), 'presets,', len(pts), 'points')
