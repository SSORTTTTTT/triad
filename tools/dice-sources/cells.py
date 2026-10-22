# Cell libraries for the SHAPERBOX (LFO tab) and GROSS BEAT VOLUME (Triad Time) dice: every source shape cut into quarter
# and eighth cells (motifs), the motifs' make-up, and how shapes repeat their cells (AAAA, AABA, ABAB ...).
import sys, json, math, collections, glob
import os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'time-presets'))
from gbparse import state, slots
K = 2.2   # Triad's curve for a ShaperBox curved segment / a Gross Beat tensioned one (about curveForMidpoint (0.25))

def shaperbox_shapes():
    out = []
    for w in json.load(open('sbx-waves.json')):
        if w['section'] in ('time', 'userWaves'): continue   # time waves are for time; the LFO dice moves mixes
        pts = [(min(1.0, max(0.0, x)), (y + 1.0) / 2.0, f) for x, y, f in w['points']]
        if pts[0][0] > 1e-6: pts.insert(0, (0.0, pts[-1][1], 3))      # ShaperBox waves wrap around
        if pts[-1][0] < 1 - 1e-6: pts.append((1.0, pts[0][1], 3))
        shape = []
        for i, (x, y, f) in enumerate(pts):
            # The factor of a point picks the line into it: 3 straight, 1 and 2 curved (two flavours).
            if shape and f in (1, 2) and abs(y - shape[-1][1]) > 1e-6 and x - shape[-1][0] > 1e-6:
                shape[-1] = (shape[-1][0], shape[-1][1], K if f == 1 else -K)
            shape.append((x, y, 0.0))
        out.append(shape)
    return out

def grossbeat_volume_shapes():
    out, seen = [], set()
    for f in sorted(glob.glob('../gb-zip/Gross Beat/*.fst')):
        st = state(f)
        if st is None: continue
        ss = slots(st)
        if len(ss) != 72: continue
        for s in ss[36:]:
            x, shape = 0.0, []
            for p in s['points']:
                nx = x + p[0]; y = p[1]; typ = int(p[3][:2], 16); t = p[2]
                if not shape: shape.append((0.0, y, 0.0)); x = nx; continue
                if typ == 2:                                  # hold, then step at the point
                    shape.append((nx / 4.0, shape[-1][1], 0.0)); shape.append((nx / 4.0, y, 0.0))
                else:
                    if abs(t) > 0.05 or typ in (9, 10, 1, 8, 12):
                        k = -t * 3.0 if abs(t) > 0.05 else K
                        shape[-1] = (shape[-1][0], shape[-1][1], max(-6.0, min(6.0, k)))
                    shape.append((nx / 4.0, y, 0.0))
                x = nx
            if not shape or shape[-1][0] > 1.0 + 1e-6 or len(shape) < 3: continue
            if shape[-1][0] < 1 - 1e-6: shape.append((1.0, shape[-1][1], 0.0))
            key = tuple((round(a, 4), round(b, 4), round(c, 2)) for a, b, c in shape)
            if key in seen: continue
            seen.add(key); out.append(shape)
    return out

def evaluate(shape, x):
    for a, b in zip(shape, shape[1:]):
        if a[0] <= x <= b[0] and b[0] > a[0]:
            t = (x - a[0]) / (b[0] - a[0]); k = a[2]
            f = t if abs(k) < 1e-3 else (math.exp(k * t) - 1) / (math.exp(k) - 1)
            return a[1] + (b[1] - a[1]) * f
    return shape[-1][1]

def cut(shape, x0, x1):
    """The part of a shape between x0 and x1, rescaled to 0..1 (a vertical step at x0 keeps its upper end)."""
    pts = []
    # value just after x0 (after any step at x0)
    after = [p for p in shape if abs(p[0] - x0) < 1e-9]
    start_y = after[-1][1] if after else evaluate(shape, x0)
    start_curve = 0.0
    for a, b in zip(shape, shape[1:]):
        if a[0] <= x0 < b[0]: start_curve = a[2]
    pts.append((0.0, start_y, start_curve))
    for p in shape:
        if x0 + 1e-9 < p[0] < x1 - 1e-9: pts.append(((p[0] - x0) / (x1 - x0), p[1], p[2]))
    before = [p for p in shape if abs(p[0] - x1) < 1e-9]
    end_y = before[0][1] if before else evaluate(shape, x1)
    pts.append((1.0, end_y, 0.0))
    # a curve cut short keeps its flavour (approximately)
    return pts

def features(cell):
    curved = sum(b[0] - a[0] for a, b in zip(cell, cell[1:]) if abs(a[2]) > 1e-3 and abs(b[1] - a[1]) > 1e-6)
    steps = sum(1 for a, b in zip(cell, cell[1:]) if abs(b[0] - a[0]) < 1e-9 and abs(b[1] - a[1]) > 1e-6)
    slopes = sum(1 for a, b in zip(cell, cell[1:]) if b[0] - a[0] > 1e-9 and abs(b[1] - a[1]) > 1e-6 and abs(a[2]) < 1e-3)
    return {'soft': round(curved, 3), 'steps': steps, 'slopes': slopes, 'points': len(cell)}

def mad(a, b, n=32):
    return sum(abs(evaluate(a, (i + 0.5) / n) - evaluate(b, (i + 0.5) / n)) for i in range(n)) / n

def library(shapes, name):
    cells = {4: collections.Counter(), 8: collections.Counter()}
    schemes = {4: collections.Counter(), 8: collections.Counter()}
    for s in shapes:
        for n in (4, 8):
            cs = [cut(s, i / n, (i + 1) / n) for i in range(n)]
            letters, reps = [], []
            for c in cs:
                for j, r in enumerate(reps):
                    if mad(c, r) < 0.03: letters.append('ABCDEFGH'[j]); break
                else:
                    reps.append(c); letters.append('ABCDEFGH'[len(reps) - 1])
            schemes[n][''.join(letters)] += 1
            for c in cs:
                key = tuple((round(x, 4), round(y, 4), round(k, 2)) for x, y, k in c)
                flat = all(abs(p[1] - key[0][1]) < 1e-6 for p in key)
                cells[n][key] += 1 if not flat else 0.3   # flat holds count less (there are many)
    out = {}
    for n in (4, 8):
        keep = [list(map(list, k)) for k, _ in cells[n].most_common(1400 if n == 4 else 1000) if len(k) <= 14]
        out[n] = {'cells': keep, 'weights': [round(cells[n][tuple(map(tuple, k))], 2) for k in keep],
                  'schemes': schemes[n].most_common(24)}
        print(name, n, 'cells:', len(cells[n]), 'kept', len(keep), 'schemes', schemes[n].most_common(8))
    return out

if __name__ == '__main__':
    sb = shaperbox_shapes()
    print('ShaperBox LFO waves:', len(sb))
    gv = grossbeat_volume_shapes()
    print('Gross Beat volume envelopes:', len(gv))
    json.dump({'shaperbox': library(sb, 'SHAPERBOX'), 'grossbeatVolume': library(gv, 'GB VOLUME')}, open('cells.json', 'w'))
    json.dump({'shaperbox': sb, 'grossbeatVolume': gv}, open('sources.json', 'w'))
