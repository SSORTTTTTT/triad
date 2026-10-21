# Reads the envelopes in a Gross Beat bank (.fst): 72 named slots (time, then volume?), each a list of points.
import struct, sys
sys.path.insert(0, __import__('os').path.join(__import__('os').path.dirname(__import__('os').path.abspath(__file__)), '..', 'preset-analysis'))
from fst import events

def state(path):
    d = open(path, 'rb').read()
    for eid, val in events(d):
        if eid == 213: return val
    return None

def slots(st):
    out = []
    # The first slot name follows a fixed header; find each slot by its structure.
    pos = 0
    n = len(st)
    while pos < n - 32:
        L = st[pos]
        if 0 <= L < 64 and pos + 1 + L + 27 <= n:
            name = st[pos + 1: pos + 1 + L]
            if all(b >= 32 for b in name):
                h = pos + 1 + L
                try:
                    a, b, z, c, d2, cnt = struct.unpack_from('<ii7siii', st, h)
                except struct.error:
                    pos += 1; continue
                if a == 5 and b == 5 and z == bytes(7) and 0 < cnt < 2000 and h + 27 + cnt * 24 <= n:
                    pts = []
                    q = h + 27
                    ok = True
                    for i in range(cnt):
                        x, y, t = struct.unpack_from('<ddf', st, q)
                        flags = st[q + 20: q + 24]
                        if not (-1e-6 <= x <= 64 and -1e-6 <= y <= 1.0 + 1e-6): ok = False; break
                        pts.append((x, y, t, flags.hex()))
                        q += 24
                    if ok:
                        out.append({'name': name.decode('latin1'), 'hdr': (c, d2), 'points': pts, 'end': q})
                        pos = q
                        continue
        pos += 1
    return out

if __name__ == '__main__':
    st = state(sys.argv[1])
    ss = slots(st)
    print(len(ss), 'slots')
    for i, s in enumerate(ss):
        print(i, repr(s['name']), s['hdr'], len(s['points']), [(round(p[0], 4), round(p[1], 4), round(p[2], 2), p[3]) for p in s['points'][:8]])
