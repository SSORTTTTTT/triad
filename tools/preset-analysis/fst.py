# Reads FL Studio .fst preset files (FLhd/FLdt event stream) and returns the events.
import struct, sys
def events(data):
    assert data[:4] == b'FLhd', 'not an FL file'
    hlen = struct.unpack_from('<I', data, 4)[0]
    pos = 8 + hlen
    assert data[pos:pos+4] == b'FLdt'
    end = pos + 8 + struct.unpack_from('<I', data, pos + 4)[0]
    pos += 8
    out = []
    while pos < min(end, len(data)):
        eid = data[pos]; pos += 1
        if eid < 64: val = data[pos]; pos += 1
        elif eid < 128: val = struct.unpack_from('<H', data, pos)[0]; pos += 2
        elif eid < 192: val = struct.unpack_from('<I', data, pos)[0]; pos += 4
        else:
            n = 0; shift = 0
            while True:
                b = data[pos]; pos += 1
                n |= (b & 0x7f) << shift; shift += 7
                if not b & 0x80: break
            val = data[pos:pos+n]; pos += n
        out.append((eid, val))
    return out
if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read()
    for eid, val in events(d):
        if isinstance(val, bytes): print(eid, 'bytes', len(val), val[:60])
        else: print(eid, val)
