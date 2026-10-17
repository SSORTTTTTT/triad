# Reads JUCE ValueTree binary streams (ValueTree::writeToStream).
import struct, zlib
class Reader:
    def __init__(self, b): self.b, self.p = b, 0
    def byte(self): v = self.b[self.p]; self.p += 1; return v
    def cint(self):
        n = self.byte(); neg = n & 0x80; n &= 0x7f
        v = int.from_bytes(self.b[self.p:self.p+n], 'little'); self.p += n
        return -v if neg else v
    def string(self):
        e = self.b.index(b'\0', self.p); s = self.b[self.p:e].decode('utf-8', 'replace'); self.p = e + 1; return s
    def var(self):
        n = self.cint()
        if n <= 0: return None
        start = self.p; t = self.byte()
        if t == 1: v = struct.unpack_from('<i', self.b, self.p)[0]
        elif t == 2: v = True
        elif t == 3: v = False
        elif t == 4: v = struct.unpack_from('<d', self.b, self.p)[0]
        elif t == 5: v = self.b[self.p:start + n].rstrip(b'\0').decode('utf-8', 'replace')
        elif t == 6: v = struct.unpack_from('<q', self.b, self.p)[0]
        elif t == 7:
            count = self.cint(); v = [self.var() for _ in range(count)]
            self.p = max(self.p, start + n); return v
        elif t == 8: v = bytes(self.b[self.p:start + n])
        else: v = None
        self.p = start + n
        return v
    def tree(self):
        name = self.string()
        props = {}
        for _ in range(self.cint()):
            k = self.string(); props[k] = self.var()
        children = [self.tree() for _ in range(self.cint())]
        return {'type': name, 'props': props, 'children': children}
def from_zlib_in(blob):
    for i in range(len(blob) - 2):
        if blob[i] == 0x78 and blob[i+1] in (0x01, 0x5e, 0x9c, 0xda):
            try:
                out = zlib.decompressobj().decompress(blob[i:])
                if out.startswith(b'PluginState') or len(out) > 100: return Reader(out).tree()
            except Exception: pass
    return None
def walk(t, depth=0):
    yield depth, t
    for c in t['children']: yield from walk(c, depth + 1)
