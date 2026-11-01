# Хамгийн энгийн binary FBX (7.x) уншигч: node -> (нэр, утгууд, хүүхдүүд). Зөвхөн өгөгдөл уншина.
import struct, zlib
class Node:
    __slots__ = ('name', 'props', 'children')
    def __init__(s, n, p, c): s.name, s.props, s.children = n, p, c
    def find(s, name): return [c for c in s.children if c.name == name]
    def first(s, name):
        r = s.find(name); return r[0] if r else None
def _arr(data, off, code):
    n, enc, clen = struct.unpack_from('<III', data, off); off += 12
    raw = data[off:off + clen]; off += clen
    if enc == 1: raw = zlib.decompress(raw)
    fmt = {'f': 'f', 'd': 'd', 'l': 'q', 'i': 'i', 'b': 'B'}[code]
    return list(struct.unpack('<%d%s' % (n, fmt), raw)), off
def _prop(data, off):
    t = chr(data[off]); off += 1
    if t == 'Y': return struct.unpack_from('<h', data, off)[0], off + 2
    if t == 'C': return data[off] != 0, off + 1
    if t == 'I': return struct.unpack_from('<i', data, off)[0], off + 4
    if t == 'F': return struct.unpack_from('<f', data, off)[0], off + 4
    if t == 'D': return struct.unpack_from('<d', data, off)[0], off + 8
    if t == 'L': return struct.unpack_from('<q', data, off)[0], off + 8
    if t in 'SR':
        n = struct.unpack_from('<I', data, off)[0]; off += 4
        v = data[off:off + n]; off += n
        return (v.decode('utf-8', 'replace') if t == 'S' else v), off
    if t in 'fdlib': return _arr(data, off, t)
    raise ValueError('prop type %r' % t)
def _node(data, off, wide):
    if wide: end, nprops, plen = struct.unpack_from('<QQQ', data, off); off += 24
    else: end, nprops, plen = struct.unpack_from('<III', data, off); off += 12
    nlen = data[off]; off += 1
    name = data[off:off + nlen].decode('ascii', 'replace'); off += nlen
    if end == 0: return None, off
    props = []
    for _ in range(nprops):
        v, off = _prop(data, off); props.append(v)
    children = []
    sentinel = 25 if wide else 13
    while off < end - sentinel:
        c, off = _node(data, off, wide)
        if c is None: break
        children.append(c)
    return Node(name, props, children), end
def load(path):
    data = open(path, 'rb').read()
    version = struct.unpack_from('<I', data, 23)[0]
    wide = version >= 7500
    off = 27; top = []
    while off < len(data):
        n, off2 = _node(data, off, wide)
        if n is None: break
        top.append(n); off = off2
    return Node('root', [], top), version
