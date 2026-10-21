# Learns how Gross Beat time envelopes are built (runs between jumps, what each run does, how far each jump goes),
# in Triad Time's units: delay in beats (0 = live, 4 = one bar back), 4 beats per bar.
import json, math, collections
envs = json.load(open('gb-time.json'))
LIN = {0, 1, 7, 8, 11, 12}
SPEEDS = [0.0, 0.25, 0.5, 0.75, 1.5, 2.0, 3.0, 4.0, -1.0, -0.5, -2.0, -0.25, -3.0]   # FREEZE, FWD -24 -12 -5 +7 +12 +19 +24, REV 0 -12 +12 -24 +19
def speed_class(s):
    best = min(SPEEDS, key=lambda c: abs(math.log2(abs(c) + 0.06) - math.log2(abs(s) + 0.06)) + (2 if (c < 0) != (s < 0) and abs(c) > 0.01 else 0))
    return best
def polyline(e):
    """(x0, x1, d0, d1, kind, speed) in beats; kind: flat, ramp, curve, special; jumps where x0 == x1."""
    segs = []
    pts = e['points']
    for a, b in zip(pts, pts[1:]):
        x0, x1 = a[0], b[0]
        d0, d1 = (1 - a[1]) * 8, (1 - b[1]) * 8
        typ, t = b[3], b[2]
        if x1 - x0 < 1e-9: segs.append((x0, x0, d0, d1, 'jump', None)); continue
        if typ == 2:
            segs.append((x0, x1, d0, d0, 'flat', 1.0))
            if abs(d1 - d0) > 1e-9: segs.append((x1, x1, d0, d1, 'jump', None))
            continue
        if abs(d1 - d0) < 1e-9: segs.append((x0, x1, d0, d0, 'flat', 1.0)); continue
        s = 1 - (d1 - d0) / (x1 - x0)
        kind = 'ramp' if typ in LIN and abs(t) < 0.05 else 'curve' if typ in LIN or typ in (9, 10) else 'special'
        segs.append((x0, x1, d0, d1, kind, s))
    return segs
def runs(e):
    out, cur = [], []
    for s in polyline(e):
        if s[4] == 'jump':
            if cur: out.append(cur); cur = []
        else: cur.append(s)
    if cur: out.append(cur)
    return out
def frac(v):
    for f in (0.25, 1/3, 0.5, 2/3, 0.75, 1.0):
        if abs(v - f) < 0.04: return round(f, 4)
    return round(v, 2)
if __name__ == '__main__':
  pass
model = {'run_durations': collections.Counter(), 'contents': collections.Counter(), 'jumps': collections.Counter(),
         'jump_ratio': collections.Counter(), 'first_effect': collections.Counter(), 'runs_per_bar': collections.Counter(),
         'start_delay': collections.Counter()}
for e in envs:
    rs = runs(e)
    if not rs: continue
    model['runs_per_bar'][len(rs)] += 1
    prev_end, prev_dur = None, None
    for i, r in enumerate(rs):
        dur = r[-1][1] - r[0][0]
        model['run_durations'][round(dur, 4)] += 1
        content = tuple((s[4] if s[4] != 'ramp' else 'ramp', speed_class(s[5]) if s[4] in ('ramp', 'curve') else 1.0, frac((s[1] - s[0]) / dur)) for s in r)
        if all(c[0] == 'flat' for c in content): content = (('flat', 1.0, 1.0),)
        model['contents'][json.dumps(content)] += 1
        start = r[0][2]
        model['start_delay'][round(start, 3)] += 1
        if prev_end is not None:
            jump = start - prev_end
            model['jumps']['live' if start < 1e-6 else round(jump, 3)] += 1
            if prev_dur and jump > 1e-6: model['jump_ratio'][round(jump / prev_dur, 3)] += 1
        prev_end, prev_dur = r[-1][3], dur
    # where the first thing that is not live happens
    t0 = next((s[0] for r in rs for s in r if not (s[4] == 'flat' and s[2] < 1e-6)), 4.0)
    model['first_effect'][round(t0, 3)] += 1
out = {k: [[json.loads(a) if k == 'contents' else a, b] for a, b in v.most_common()] for k, v in model.items()}
json.dump(out, open('gb-model.json', 'w'))
if __name__ == '__main__':
    for k, v in model.items():
        print(k, v.most_common(12))
