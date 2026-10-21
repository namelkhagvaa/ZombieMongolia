"""Монголын OpenStreetMap татан авалтаас (Geofabrik .osm.pbf) аймгийн төвүүдийн хэсгийг салгана.

    python tools/pbf_towns.py mongolia-latest.osm.pbf [дугаар ...]

https://download.geofabrik.de/asia/mongolia-latest.osm.pbf (~60 MB). Overpass API ачаалалтай үед
tools/fetch_towns.py-ийн оронд: гаралт нь ижил хэлбэртэй (Overpass JSON, out geom / out center) тул
tools/gen_city.py --town шууд уншина. Нэмэлт сан хэрэггүй (PBF-ийг энд задална).
Өгөгдөл: (c) OpenStreetMap contributors, ODbL 1.0.
"""
import gzip, json, math, os, struct, sys, zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fetch_towns import TOWNS, HALF

MARGIN = 400.0  # м: хайрцгийн захаар гарсан барилга, замыг бүтнээр нь авна


# ---------------------------------------------------------------- protobuf

def varint(buf, pos):
    result = shift = 0
    while True:
        b = buf[pos]
        pos += 1
        result |= (b & 0x7F) << shift
        if b < 0x80:
            return result, pos
        shift += 7


def fields(buf):
    """(дугаар, төрөл, утга) — 0: varint, 2: bytes."""
    pos, end = 0, len(buf)
    while pos < end:
        key, pos = varint(buf, pos)
        number, wire = key >> 3, key & 7
        if wire == 0:
            value, pos = varint(buf, pos)
        elif wire == 2:
            length, pos = varint(buf, pos)
            value = buf[pos:pos + length]
            pos += length
        elif wire == 5:
            value = buf[pos:pos + 4]; pos += 4
        elif wire == 1:
            value = buf[pos:pos + 8]; pos += 8
        else:
            raise ValueError('wire type %d' % wire)
        yield number, wire, value


def packed(buf):
    out, pos, end = [], 0, len(buf)
    append = out.append
    while pos < end:
        result = shift = 0
        while True:
            b = buf[pos]
            pos += 1
            result |= (b & 0x7F) << shift
            if b < 0x80:
                break
            shift += 7
        append(result)
    return out


def zigzag(v):
    return (v >> 1) ^ -(v & 1)


def delta(values):
    out, total = [], 0
    for v in values:
        total += (v >> 1) ^ -(v & 1)
        out.append(total)
    return out


def blocks(path):
    with open(path, 'rb') as f:
        while True:
            head = f.read(4)
            if len(head) < 4:
                return
            header = f.read(struct.unpack('>I', head)[0])
            kind, size = None, 0
            for n, _, v in fields(header):
                if n == 1: kind = bytes(v).decode()
                elif n == 3: size = v
            blob = f.read(size)
            raw = None
            for n, _, v in fields(blob):
                if n == 1: raw = bytes(v)
                elif n == 3: raw = zlib.decompress(v)
            if kind == 'OSMData':
                yield raw


# ---------------------------------------------------------------- шүүлт

def wanted_shape(tags):
    """tools/fetch_towns.py-ийн Overpass асуулгатай ижил дүрэм (out geom)."""
    if 'building' in tags or 'highway' in tags or 'waterway' in tags or 'railway' in tags:
        return True
    if tags.get('natural') == 'water' or tags.get('barrier') in ('fence', 'wall'):
        return True
    if tags.get('leisure') in ('park', 'stadium', 'pitch') or tags.get('landuse') in ('grass', 'forest', 'cemetery'):
        return True
    return False


def wanted_poi(tags):
    return (tags.get('amenity') in ('fuel', 'police', 'hospital', 'townhall', 'marketplace') or tags.get('place') == 'square'
            or tags.get('leisure') == 'stadium' or tags.get('tourism') in ('museum', 'attraction')
            or tags.get('office') == 'government')


def main():
    path = sys.argv[1]
    wanted = [int(a) for a in sys.argv[2:]] or sorted(TOWNS)
    boxes = {}
    for index in wanted:
        name, lat, lon = TOWNS[index]
        dlat = (HALF + MARGIN) / 111320.0
        dlon = dlat / math.cos(math.radians(lat))
        boxes[index] = (lat - dlat, lon - dlon, lat + dlat, lon + dlon)

    def town_of(lat, lon):
        for index, (a, b, c, d) in boxes.items():
            if a <= lat <= c and b <= lon <= d:
                return index
        return -1

    nodes = {}      # id -> (lat, lon)
    poi_nodes = []  # (town, id, lat, lon, tags)
    ways = {}       # id -> (refs, tags)
    relations = []  # (id, tags, members)
    count = 0
    for raw in blocks(path):
        strings, groups, granularity, lat_offset, lon_offset = [], [], 100, 0, 0
        for n, _, v in fields(raw):
            if n == 1:
                strings = [bytes(s).decode('utf-8', 'replace') for m, _, s in fields(v) if m == 1]
            elif n == 2: groups.append(v)
            elif n == 17: granularity = v
            elif n == 19: lat_offset = v
            elif n == 20: lon_offset = v
        scale = granularity * 1e-9
        for group in groups:
            for n, _, v in fields(group):
                if n == 2:  # DenseNodes
                    ids = lats = lons = kv = None
                    for m, _, w in fields(v):
                        if m == 1: ids = delta(packed(w))
                        elif m == 8: lats = delta(packed(w))
                        elif m == 9: lons = delta(packed(w))
                        elif m == 10: kv = packed(w)
                    k = 0
                    for i in range(len(ids)):
                        lat = lat_offset * 1e-9 + lats[i] * scale
                        lon = lon_offset * 1e-9 + lons[i] * scale
                        tags = None
                        if kv:
                            while kv[k] != 0:
                                if tags is None: tags = {}
                                tags[strings[kv[k]]] = strings[kv[k + 1]]
                                k += 2
                            k += 1
                        town = town_of(lat, lon)
                        if town < 0:
                            continue
                        nodes[ids[i]] = (lat, lon)
                        if tags and wanted_poi(tags):
                            poi_nodes.append((town, ids[i], lat, lon, tags))
                    count += len(ids)
                elif n == 3:  # Way
                    wid, keys, vals, refs = 0, [], [], []
                    for m, _, w in fields(v):
                        if m == 1: wid = w
                        elif m == 2: keys = packed(w)
                        elif m == 3: vals = packed(w)
                        elif m == 8: refs = delta(packed(w))
                    if any(r in nodes for r in refs):
                        ways[wid] = (refs, {strings[a]: strings[b] for a, b in zip(keys, vals)})
                elif n == 4:  # Relation
                    rid, keys, vals, roles, memids, types = 0, [], [], [], [], []
                    for m, _, w in fields(v):
                        if m == 1: rid = w
                        elif m == 2: keys = packed(w)
                        elif m == 3: vals = packed(w)
                        elif m == 8: roles = packed(w)
                        elif m == 9: memids = delta(packed(w))
                        elif m == 10: types = packed(w)
                    tags = {strings[a]: strings[b] for a, b in zip(keys, vals)}
                    if tags.get('type') != 'multipolygon' or not (wanted_shape(tags) or wanted_poi(tags)):
                        continue
                    members = [(strings[r], mid) for r, mid, t in zip(roles, memids, types) if t == 1]
                    if any(mid in ways for _, mid in members):
                        relations.append((rid, tags, members))
                elif n == 1:  # Node (энгийн): Geofabrik-д бараг байдаггүй
                    pass
        if count and count % 2000000 < 8000:
            print('  %d M nodes read' % (count // 1000000), flush=True)
    print('nodes kept %d, ways %d, relations %d' % (len(nodes), len(ways), len(relations)), flush=True)

    def geometry(refs):
        return [{'lat': nodes[r][0], 'lon': nodes[r][1]} for r in refs if r in nodes]

    out = {index: [] for index in wanted}
    for wid, (refs, tags) in ways.items():
        geom = geometry(refs)
        if len(geom) < 2:
            continue
        mid = geom[len(geom) // 2]
        town = town_of(mid['lat'], mid['lon'])
        if town < 0:
            town = town_of(geom[0]['lat'], geom[0]['lon'])
        if town < 0:
            continue
        closed = refs[0] == refs[-1]
        if 'building' in tags and len(geom) < len(refs):
            continue  # хайрцгийн гадна талаар тасарсан барилга
        if wanted_shape(tags) and (closed or not ('building' in tags)):
            out[town].append({'type': 'way', 'id': wid, 'tags': tags, 'geometry': geom})
        if wanted_poi(tags):
            lat = sum(g['lat'] for g in geom) / len(geom); lon = sum(g['lon'] for g in geom) / len(geom)
            out[town].append({'type': 'way', 'id': wid, 'tags': tags, 'center': {'lat': lat, 'lon': lon}})
    for rid, tags, members in relations:
        parts = [{'type': 'way', 'ref': mid, 'role': role, 'geometry': geometry(ways[mid][0])}
                 for role, mid in members if mid in ways]
        if not parts or not parts[0]['geometry']:
            continue
        g = parts[0]['geometry'][0]
        town = town_of(g['lat'], g['lon'])
        if town < 0:
            continue
        if wanted_shape(tags):
            out[town].append({'type': 'relation', 'id': rid, 'tags': tags, 'members': parts})
        if wanted_poi(tags):
            out[town].append({'type': 'relation', 'id': rid, 'tags': tags, 'center': g})
    for town, nid, lat, lon, tags in poi_nodes:
        out[town].append({'type': 'node', 'id': nid, 'lat': lat, 'lon': lon, 'tags': tags})

    folder = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data', 'towns')
    os.makedirs(folder, exist_ok=True)
    for index in wanted:
        body = json.dumps({'version': 0.6, 'generator': 'pbf_towns.py (Geofabrik extract)',
                           'osm3s': {'copyright': 'The data included in this document is from www.openstreetmap.org. '
                                                  'The data is made available under ODbL.'},
                           'elements': out[index]}, ensure_ascii=False).encode('utf-8')
        with gzip.open(os.path.join(folder, '%02d.json.gz' % index), 'wb', 9) as f:
            f.write(body)
        print('%2d %s: %d elements, %d KB' % (index, TOWNS[index][0], len(out[index]), len(body) // 1024), flush=True)


if __name__ == '__main__':
    main()
