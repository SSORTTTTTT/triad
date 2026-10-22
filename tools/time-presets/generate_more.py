# A new batch of Triad Time TIME patterns, drawn from what was learned from the Gross Beat banks (model.py, jumps.py):
# runs between jumps, what each run does (stay, an octave down or up, reverse, tape stop...), how the jumps go (back to
# live, replay, further back). Each folder leans on one thing Thomas likes; every pattern is checked to be new (not a
# copy of any pattern in the banks), on Triad Time's grid (1/64 bar across, 1/16 bar of delay down), within one bar back.
import json, math, random, collections, sys
from model import envs, polyline, model, speed_class
# python3 generate_more.py [kept.json] [per folder] [seed]
#   0.13.0: batch-120.json 200 20261381   (880 more on top of generate.py's 120: 1,000)
#   0.14.0: batch-1000.json 600 20261022  (2,000 more on top of those: 3,000)
KEPT = sys.argv[1] if len(sys.argv) > 1 else 'batch-120.json'
PER_FOLDER = int(sys.argv[2]) if len(sys.argv) > 2 else 200
R = random.Random(int(sys.argv[3]) if len(sys.argv) > 3 else 20261021 + 360)
BEATS = 4.0
GRID_D = 0.25           # one row of Triad Time's grid: 1/16 of a bar back
GRID_T = 0.0625         # one column: 1/64 of a bar
def weighted(items):
    total = sum(w for _, w in items)
    r = R.random() * total
    for v, w in items:
        r -= w
        if r <= 0: return v
    return items[-1][0]
# Learned distributions (Triad Time's grid only: no triplets).
DUR = [(d, w) for d, w in model['run_durations'].items() if d in (0.0625, 0.125, 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0)]
CONTENT = collections.Counter()
for c, w in model['contents'].items():
    c = json.loads(c)
    if len(c) == 1 and c[0][0] in ('flat', 'ramp', 'curve'): CONTENT[(c[0][0], c[0][1])] += w
JUMPS = [('live', 3965), ('further', 1144), ('repeat', 1054), ('towards', 383), ('same', 233), ('carry', 104)]
FURTHER = [(1.0, 271), (0.5, 169), (0.25, 143), (2.0, 52), (0.75, 51), (1.5, 26)]
RUNS = [(n, w) for n, w in model['runs_per_bar'].items() if 2 <= n <= 9]
OCT_DOWN = {0.5, 0.25}
OCT_UP = {2.0, 4.0}
REV = {-1.0, -0.5, -2.0}
def semitones(s):
    return 0 if abs(s) < 1e-3 else round(12 * math.log2(abs(s)))
def label(s):
    if abs(s) < 1e-3: return 'FREEZE'
    return ('REV ' if s < 0 else 'FWD ') + ('%+d' % semitones(s) if semitones(s) else '0')

def durations(n):
    """n run lengths adding up to a bar, each starting on a multiple of the smaller of its length and a beat."""
    for _ in range(200):
        t, out = 0.0, []
        for i in range(n):
            left = BEATS - t
            if i == n - 1:
                d = left
            else:
                opts = [(d, w) for d, w in DUR if d <= left - 0.0625 * (n - 1 - i) and abs((t / min(d, 1.0)) - round(t / min(d, 1.0))) < 1e-9]
                if not opts: break
                d = weighted(opts)
            out.append(d); t += d
        if len(out) == n and abs(t - BEATS) < 1e-9 and all(d >= 0.0625 for d in out): return out
    return None

def make(weights, need):
    """One pattern: runs with learned lengths, contents (re-weighted for the folder) and jumps."""
    n = weighted(RUNS)
    durs = durations(n)
    if durs is None: return None
    content = [(k, w * weights.get(k, 1.0)) for k, w in CONTENT.items()]
    segs = []        # (t0, t1, d0, d1, kind, speed)
    t, d = 0.0, 0.0
    prev = None
    for i, dur in enumerate(durs):
        if i > 0:
            j = weighted(JUMPS)
            p0, pd, pend = prev
            if j == 'live': d = 0.0
            elif j == 'repeat': d = p0 + pd
            elif j == 'further': d = pend + weighted(FURTHER)
            elif j == 'towards': d = max(0.0, pend - weighted(FURTHER))
            elif j == 'same': d = p0
            else: d = pend
        elif R.random() < 0.15:
            d = weighted([(0.5, 2), (1.0, 3), (2.0, 1)])
        (kind, s) = weighted(content)
        if kind == 'flat': s = 1.0
        d1 = d + (1.0 - s) * dur
        # Triad Time's grid and range.
        if abs(d / GRID_D - round(d / GRID_D)) > 1e-6 or abs(d1 / GRID_D - round(d1 / GRID_D)) > 1e-6: return None
        if d < -1e-9 or d1 < -1e-9 or d > BEATS + 1e-9 or d1 > BEATS + 1e-9: return None
        segs.append((t, t + dur, d, d1, kind, s))
        prev = (d, dur, d1)
        t += dur
    if not need(segs): return None
    return segs

def to_shape(segs):
    pts = []
    tape = 2.0 * math.log(1.0 / 0.25 - 1.0)       # Triad's curveForMidpoint (0.25): a tape stop
    for (t0, t1, d0, d1, kind, s) in segs:
        a = (round(t0 / BEATS, 6), round(1 - d0 / BEATS, 6))
        b = (round(t1 / BEATS, 6), round(1 - d1 / BEATS, 6))
        curve = 0.0
        if kind == 'curve': curve = tape if R.random() < 0.7 else -tape
        if pts and abs(pts[-1][0] - a[0]) < 1e-9 and abs(pts[-1][1] - a[1]) < 1e-9: pts[-1] = (a[0], a[1], curve)
        else: pts.append((a[0], a[1], curve))
        pts.append((b[0], b[1], 0.0))
    # Flat runs next to each other at the same delay: one line.
    out = []
    for p in pts:
        if len(out) >= 2 and abs(out[-1][1] - p[1]) < 1e-9 and abs(out[-2][1] - p[1]) < 1e-9 and abs(out[-2][2]) < 1e-9 and abs(out[-1][0] - p[0]) > 1e-9:
            out[-1] = p
        else: out.append(p)
    return out

def delay_curve(segs, n=256):
    v = []
    for i in range(n):
        t = (i + 0.5) / n * BEATS
        for (t0, t1, d0, d1, kind, s) in segs:
            if t0 <= t < t1:
                f = (t - t0) / (t1 - t0)
                v.append(d0 + (d1 - d0) * (f * f if kind == 'curve' else f)); break
    return v
SOURCES = []
for e in envs:
    segs = [(x0, x1, min(d0, 8), min(d1, 8), k, s) for (x0, x1, d0, d1, k, s) in polyline(e) if k != 'jump']
    SOURCES.append(delay_curve(segs))
def distance(a, b): return sum(abs(x - y) for x, y in zip(a, b)) / len(a)

def counts(segs):
    c = collections.Counter()
    for i, (t0, t1, d0, d1, kind, s) in enumerate(segs):
        if kind == 'ramp' and t1 - t0 >= 0.25:
            if s in OCT_DOWN: c['down'] += 1
            if s in OCT_UP: c['up'] += 1
            if s in REV: c['rev'] += 1
        if i > 0 and d0 > segs[i - 1][3] + 1e-9: c['chop'] += 1
    return c
FOLDERS = [
    ('OCTAVE DOWN', {('ramp', 0.5): 5, ('ramp', 0.25): 4, ('curve', 0.5): 2}, lambda c: c['down'] >= 1),
    ('OCTAVE UP', {('ramp', 2.0): 12, ('ramp', 4.0): 10, ('curve', 2.0): 3}, lambda c: c['up'] >= 1),
    ('REVERSE', {('ramp', -1.0): 5, ('ramp', -0.5): 5, ('ramp', -2.0): 5, ('curve', -1.0): 2}, lambda c: c['rev'] >= 1),
    ('CHOPS', {('flat', 1.0): 2}, lambda c: c['chop'] >= 3),
    ('PITCH + CHOPS', {('ramp', 0.5): 3, ('ramp', -1.0): 3, ('ramp', 2.0): 4, ('ramp', 0.25): 2, ('ramp', -0.5): 2}, lambda c: c['chop'] >= 2 and (c['down'] + c['up'] + c['rev']) >= 1),
]
first = json.load(open(KEPT))   # the patterns there already, kept as they are (and the new ones kept clear of them)
batch = [dict(m, curve=delay_curve([tuple(x) for x in m['segs']])) for m in first]
stats = collections.Counter()
for name, weights, crit in FOLDERS:
    made = []
    tries = 0
    have = sum(1 for m in batch if m['folder'] == name)
    while have + len(made) < PER_FOLDER and tries < 6000000:
        tries += 1
        segs = make(weights, lambda s: crit(counts(s)))
        if segs is None: continue
        if all(k == 'flat' for (_, _, _, _, k, _) in segs) and name != 'CHOPS': continue
        curve = delay_curve(segs)
        nearest = min(distance(curve, s) for s in SOURCES)
        if nearest < 0.12: stats['too close to a bank pattern'] += 1; continue
        if any(distance(curve, m['curve']) < 0.2 for m in batch + made): stats['too close to one made already'] += 1; continue
        made.append({'folder': name, 'segs': segs, 'curve': curve, 'nearest': nearest})
    print(name, len(made), 'more made in', tries, 'tries')
    batch += made
print(dict(stats))
for i, m in enumerate(batch):
    if 'shape' in m: continue
    m['shape'] = to_shape(m['segs'])
    moves = [label(s) for (t0, t1, d0, d1, k, s) in m['segs'] if k in ('ramp', 'curve')]
    m['moves'] = moves
json.dump([{'folder': m['folder'], 'shape': m['shape'], 'moves': m['moves'], 'nearest': m['nearest'], 'segs': m['segs']} for m in batch], open('batch.json', 'w'))
print('nearest bank pattern (beats, mean difference): min', round(min(m['nearest'] for m in batch), 3), 'median', round(sorted(m['nearest'] for m in batch)[len(batch)//2], 3))
