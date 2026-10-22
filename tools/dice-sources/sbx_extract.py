# The waves in Thomas's ShaperBox presets (.fst): every LFO wave with 3 or more points, by section.
import sys, glob, json, collections
import os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'preset-analysis'))
from fst import events
from juce_vt import from_zlib_in
files = sorted(glob.glob('../sbx/SHAPERBOX/**/*.fst', recursive=True))
def waves(t, path):
    if t['type'] == 'WaveStateModel':
        for c in t['children']:
            if c['type'] == 'enumeration' and c['props'].get('id') == 'points':
                pts = []
                for p in c['children']:
                    v = {q['props'].get('id'): q['props'].get('value') for q in p['children']}
                    if 'x' in v and 'y' in v: pts.append((v['x'] / 8192.0, float(v['y']), int(v.get('factor', 3) or 0)))
                yield path, pts
        return
    for c in t['children']:
        yield from waves(c, path + [c['props'].get('id') or c['type']])
out, seen, factors, bad = [], set(), collections.Counter(), 0
for f in files:
    try:
        st = [v for e, v in events(open(f, 'rb').read()) if e == 213]
        t = from_zlib_in(st[0]) if st else None
    except Exception:
        t = None
    if t is None: bad += 1; continue
    for path, pts in waves(t, []):
        if len(pts) < 3: continue
        pts.sort(key=lambda p: p[0])
        key = tuple((round(x, 4), round(y, 3), fct) for x, y, fct in pts)
        if key in seen: continue
        seen.add(key)
        for p in pts: factors[p[2]] += 1
        section = path[0] if path else '?'
        out.append({'file': f.split('/')[-1], 'section': section, 'points': pts})
print(len(files), 'files,', bad, 'unread,', len(out), 'unique waves')
print('sections:', collections.Counter(w['section'] for w in out).most_common())
print('factors:', factors.most_common(12))
json.dump(out, open('sbx-waves.json', 'w'))
