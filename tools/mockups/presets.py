import re
import os
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Source', 'TimePresets.h')).read()
pts_block = src[src.index('inline const Point points[] {'):]
pts_block = pts_block[:pts_block.index('};')]
points = [tuple(float(v) for v in m) for m in re.findall(r'\{ ([-0-9.]+)f, ([-0-9.]+)f, ([-0-9.]+)f \}', pts_block)]
entries = [(n, int(f), int(a), int(c)) for n, f, a, c in re.findall(r'\{ "([^"]+)", (\d+), (\d+), (\d+) \}', src)]
def shape(i):
    n, f, a, c = entries[i]
    return n, f, points[a:a + c]
if __name__ == '__main__':
    print(len(points), len(entries))
    for i in range(4): print(shape(i))
    print([ (i, e[0]) for i, e in enumerate(entries) if e[1] == 2][:3])
