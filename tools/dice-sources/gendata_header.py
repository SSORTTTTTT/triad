# Writes Source/GeneratorData.h: what each dice source learned (ShaperBox and Gross Beat volume cells, Gross Beat time runs).
import json, sys
def fl(v):
    t = repr(round(float(v), 4))
    return t + 'f'
cells = json.load(open('cells.json'))
model = json.load(open('../gb/gb-model.json'))
L = []
L.append('#pragma once')
L.append('// What the dice learned from each set of presets Thomas sent (made by tools/dice-sources from the presets, which are')
L.append('// not stored here). SHAPERBOX: his ShaperBox presets (4,001 different LFO waves, not the Time ones). GROSS BEAT VOLUME:')
L.append('// the volume envelopes of his Gross Beat banks (336). Both as cells (a quarter or an eighth of a shape) and how')
L.append('// shapes repeat their cells (AAAB, ABAB ...). GROSS BEAT TIME: the time envelopes of the banks (2,183): how long the')
L.append('// runs between jumps are, what each run does and how the jumps go, in beats of a 4-beat bar.')
L.append('namespace triad::mod::sources')
L.append('{')
L.append('struct CellPoint { float x, y, curve; };')
L.append('struct Cell { int first, count; float weight; };')
L.append('struct Scheme { const char* letters; float weight; };')
L.append('struct Library { const CellPoint* points; const Cell* cells[2]; int numCells[2]; const Scheme* schemes[2]; int numSchemes[2]; };   // [0]: quarters, [1]: eighths')
def emit_library(name, lib):
    pts, entries = [], {}
    for n in ('4', '8'):
        es = []
        for cell, w in zip(lib[n]['cells'], lib[n]['weights']):
            es.append((len(pts), len(cell), w))
            pts.extend(cell)
        entries[n] = es
    L.append('inline const CellPoint %sPoints[] {' % name)
    row = []
    for x, y, k in pts:
        row.append('{%s,%s,%s}' % (fl(x), fl(y), fl(round(k, 2))))
        if len(row) == 10: L.append('    ' + ','.join(row) + ','); row = []
    if row: L.append('    ' + ','.join(row) + ',')
    L.append('};')
    for n, tag in (('4', 'Quarters'), ('8', 'Eighths')):
        L.append('inline const Cell %s%s[] {' % (name, tag))
        row = []
        for f, c, w in entries[n]:
            row.append('{%d,%d,%s}' % (f, c, fl(w)))
            if len(row) == 10: L.append('    ' + ','.join(row) + ','); row = []
        if row: L.append('    ' + ','.join(row) + ',')
        L.append('};')
        L.append('inline const Scheme %s%sSchemes[] { %s };' % (name, tag, ', '.join('{"%s",%s}' % (s, fl(w)) for s, w in lib[n]['schemes'])))
    L.append('inline const Library %s { %sPoints, { %sQuarters, %sEighths }, { %d, %d }, { %sQuartersSchemes, %sEighthsSchemes }, { %d, %d } };'
             % (name, name, name, name, len(entries['4']), len(entries['8']), name, name, len(lib['4']['schemes']), len(lib['8']['schemes'])))
emit_library('shaperBox', cells['shaperbox'])
emit_library('grossBeatVolume', cells['grossbeatVolume'])
# Gross Beat time runs (Triad Time's grid only: no triplets).
grid = (0.0625, 0.125, 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0)
L.append('struct Weighted { float value, weight; };')
L.append('inline const Weighted timeRunBeats[] { %s };' % ', '.join('{%s,%s}' % (fl(d), fl(w)) for d, w in model['run_durations'] if d in grid))
L.append('inline const Weighted timeRunsPerBar[] { %s };' % ', '.join('{%s,%s}' % (fl(n), fl(w)) for n, w in model['runs_per_bar'] if 1 <= int(n) <= 10))
kinds = {'flat': 0, 'ramp': 1, 'curve': 2}
cont = []
for c, w in model['contents']:
    if len(c) == 1 and c[0][0] in kinds: cont.append((kinds[c[0][0]], c[0][1], w))
L.append('struct TimeContent { int kind; float speed, weight; };   // kind: 0 flat, 1 straight line, 2 curve (a tape stop or start)')
L.append('inline const TimeContent timeContents[] { %s };' % ', '.join('{%d,%s,%s}' % (c[0], fl(c[1]), fl(c[2])) for c in cont))
L.append('enum TimeJump { jumpLive, jumpFurther, jumpRepeat, jumpTowards, jumpSame, jumpCarry };')
L.append('inline const Weighted timeJumps[] { {(float) jumpLive,3965.0f}, {(float) jumpFurther,1144.0f}, {(float) jumpRepeat,1054.0f}, {(float) jumpTowards,383.0f}, {(float) jumpSame,233.0f}, {(float) jumpCarry,104.0f} };')
L.append('inline const Weighted timeFurther[] { {1.0f,271.0f}, {0.5f,169.0f}, {0.25f,143.0f}, {2.0f,52.0f}, {0.75f,51.0f}, {1.5f,26.0f} };')
L.append('}')
open(sys.argv[1], 'w').write('\n'.join(L) + '\n')
print('written', sum(1 for _ in L))
