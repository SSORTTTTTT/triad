import json, math, sys
from PIL import Image, ImageDraw, ImageFont
batch = json.load(open(sys.argv[1] if len(sys.argv) > 1 else 'batch.json'))
W, H = 300, 96   # 3.1 : 1 like the grid
cols = 4
font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 11)
bold = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 13)
folders = []
for b in batch:
    if b['folder'] not in folders: folders.append(b['folder'])
def ev(shape, x):
    for a, b in zip(shape, shape[1:]):
        if a[0] <= x <= b[0] and b[0] > a[0]:
            t = (x - a[0]) / (b[0] - a[0]); k = a[2]
            f = t if abs(k) < 1e-3 else (math.exp(k * t) - 1) / (math.exp(k) - 1)
            return a[1] + (b[1] - a[1]) * f
    return shape[-1][1]
pages = []
for folder in folders:
    items = [b for b in batch if b['folder'] == folder]
    rows = (len(items) + cols - 1) // cols
    img = Image.new('RGB', (cols * (W + 16) + 16, 40 + rows * (H + 34)), (30, 31, 33))
    d = ImageDraw.Draw(img)
    d.text((16, 12), folder + '  (%d)' % len(items), fill=(111, 183, 233), font=bold)
    for i, it in enumerate(items):
        x0 = 16 + (i % cols) * (W + 16); y0 = 40 + (i // cols) * (H + 34)
        d.rectangle([x0, y0, x0 + W, y0 + H], fill=(17, 19, 18))
        for q in range(1, 16):
            c = (70, 74, 72) if q % 4 == 0 else (38, 41, 40)
            d.line([x0 + W * q / 16, y0, x0 + W * q / 16, y0 + H], fill=c)
        for q in range(1, 16):
            if q % 4 == 0: d.line([x0, y0 + H * q / 16, x0 + W, y0 + H * q / 16], fill=(70, 74, 72))
        sh = it['shape']
        pts = []
        # draw: sample curves, exact verticals
        for a, b in zip(sh, sh[1:]):
            if b[0] - a[0] < 1e-9:
                pts.append((x0 + W * a[0], y0 + H * (1 - a[1]))); pts.append((x0 + W * b[0], y0 + H * (1 - b[1]))); continue
            n = 24 if abs(a[2]) > 1e-3 else 1
            for k in range(n + 1):
                x = a[0] + (b[0] - a[0]) * k / n
                pts.append((x0 + W * x, y0 + H * (1 - ev([a, b], x))))
        d.line(pts, fill=(111, 183, 233), width=2)
        d.text((x0, y0 + H + 4), str(i + 1) + '  ' + ' '.join(sorted(set(it['moves']), key=it['moves'].index))[:44], fill=(200, 205, 200), font=font)
    pages.append(img)
out = Image.new('RGB', (max(p.width for p in pages) * 2 + 16, max(sum(p.height for p in pages[::2]), sum(p.height for p in pages[1::2]))), (30, 31, 33))
y = [0, 0]
for i, p in enumerate(pages):
    col = i % 2
    out.paste(p, (col * (pages[0].width + 16), y[col])); y[col] += p.height
out.save(sys.argv[2] if len(sys.argv) > 2 else 'batch.png')
print(out.size)
