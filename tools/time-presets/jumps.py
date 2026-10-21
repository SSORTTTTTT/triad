# What jumps between runs do: back to live, replay the run just played (a repeat), carry on, or jump by an amount.
import json, collections
from model import envs, runs
kinds = collections.Counter()
other = collections.Counter()
for e in envs:
    rs = runs(e)
    for a, b in zip(rs, rs[1:]):
        d0, dur, dend = a[0][2], a[-1][1] - a[0][0], a[-1][3]
        start = b[0][2]
        if start < 1e-6: kinds['live'] += 1
        elif abs(start - (d0 + dur)) < 1e-3: kinds['repeat'] += 1
        elif abs(start - dend) < 1e-3: kinds['carry on'] += 1
        elif abs(start - d0) < 1e-3: kinds['same start delay'] += 1
        elif start > dend: kinds['further back'] += 1; other[round(start - dend, 3)] += 1
        else: kinds['towards live'] += 1
print(kinds.most_common())
print('further back by:', other.most_common(10))
