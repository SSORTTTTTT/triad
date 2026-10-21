# Time envelopes from the Gross Beat banks: the first 36 slots of each 72-slot bank, as cumulative points.
import glob, json, collections, math
from gbparse import state, slots
files = sorted(glob.glob(__import__('os').path.join(__import__('os').environ.get('GB_BANKS', '.'), '*.fst')))
envs = []
seen = set()
for f in files:
    st = state(f)
    if st is None: continue
    ss = slots(st)
    if len(ss) != 72: continue
    for i, s in enumerate(ss[:36]):
        x = 0.0; pts = []
        for p in s['points']:
            x += p[0]
            pts.append((round(x, 6), round(p[1], 6), round(p[2], 4), int(p[3][:2], 16)))
        key = tuple((a, b, c, d) for a, b, c, d in pts)
        if key in seen: continue
        seen.add(key)
        envs.append({'bank': f.split('/')[-1][:-4], 'slot': i, 'name': s['name'], 'points': pts})
print(len(envs), 'unique time envelopes from', len(files), 'banks')
json.dump(envs, open('gb-time.json', 'w'))
lengths = collections.Counter(round(e['points'][-1][0], 3) for e in envs)
print('lengths (beats):', lengths.most_common(8))
types = collections.Counter(p[3] for e in envs for p in e['points'][1:])
print('segment types:', types.most_common())
tens = collections.Counter(round(p[2], 1) for e in envs for p in e['points'][1:])
print('tensions:', tens.most_common(10))
miny = collections.Counter(math.floor(min(p[1] for p in e['points']) * 8) / 8 for e in envs)
print('lowest y (1 = live, 0 = 2 bars back):', sorted(miny.items()))
trivial = sum(1 for e in envs if all(abs(p[1] - 1) < 1e-6 for p in e['points']))
print('flat at live (empty slots):', trivial)
