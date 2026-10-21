"""Аймгийн төв бүрээс 10.5 км доторх гол мөрөн, нуур, ой, авто замыг Geofabrik .osm.pbf-ээс салгана
(tools/data/towns/NN_water.json.gz): тоглоомын алсын газрын (10 км) ус, ой.

    python tools/pbf_water.py mongolia-latest.osm.pbf [дугаар ...]

Гурван удаа уншина (санах ой хэмнэнэ): 1) multipolygon-ийн гишүүн way-ууд, 2) way-ууд, тэдгээрийн зангилаанууд,
3) зангилааны координат. Өгөгдөл: (c) OpenStreetMap contributors, ODbL 1.0.
"""
import gzip, json, math, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fetch_towns import TOWNS
from pbf_towns import blocks, fields, packed, delta

RADIUS = 10500.0
CENTERS = dict(TOWNS)
CENTERS[15] = ('Улаанбаатар', 47.918850, 106.917580)


def wanted(tags):
    if tags.get('waterway') in ('river', 'stream', 'canal'):
        return True
    if tags.get('highway') in ('motorway', 'trunk', 'primary', 'secondary', 'tertiary', 'motorway_link', 'trunk_link', 'primary_link'):
        return True  # хот руу орж ирэх авто замууд (алсын газарт)
    if tags.get('natural') in ('water', 'wood') or 'water' in tags or tags.get('landuse') in ('reservoir', 'forest'):
        return True
    return False


def tags_of(strings, keys, vals):
    return {strings[a]: strings[b] for a, b in zip(keys, vals)}


def string_table(raw):
    for n, _, v in fields(raw):
        if n == 1:
            return [bytes(s).decode('utf-8', 'replace') for m, _, s in fields(v) if m == 1]
    return []


def groups(raw):
    return [v for n, _, v in fields(raw) if n == 2]


def main():
    path = sys.argv[1]
    indices = [int(a) for a in sys.argv[2:]] or sorted(CENTERS)
    boxes = {}
    for index in indices:
        name, lat, lon = CENTERS[index]
        dlat = RADIUS / 111320.0
        dlon = dlat / math.cos(math.radians(lat))
        boxes[index] = (lat - dlat, lon - dlon, lat + dlat, lon + dlon)

    def town_of(lat, lon):
        for index, (a, b, c, d) in boxes.items():
            if a <= lat <= c and b <= lon <= d:
                return index
        return -1

    # 1) multipolygon-ууд ба гишүүн way-ууд
    relations, member_ways = [], set()
    for raw in blocks(path):
        strings = None
        for group in groups(raw):
            for n, _, v in fields(group):
                if n != 4:
                    continue
                if strings is None:
                    strings = string_table(raw)
                rid, keys, vals, roles, memids, types = 0, [], [], [], [], []
                for m, _, w in fields(v):
                    if m == 1: rid = w
                    elif m == 2: keys = packed(w)
                    elif m == 3: vals = packed(w)
                    elif m == 8: roles = packed(w)
                    elif m == 9: memids = delta(packed(w))
                    elif m == 10: types = packed(w)
                tags = tags_of(strings, keys, vals)
                if tags.get('type') == 'multipolygon' and wanted(tags):
                    members = [(strings[r], mid) for r, mid, t in zip(roles, memids, types) if t == 1]
                    relations.append((rid, tags, members))
                    member_ways.update(mid for _, mid in members)
    print('relations %d, member ways %d' % (len(relations), len(member_ways)), flush=True)

    # 2) way-ууд
    ways, needed = {}, set()
    for raw in blocks(path):
        strings = None
        for group in groups(raw):
            for n, _, v in fields(group):
                if n != 3:
                    continue
                if strings is None:
                    strings = string_table(raw)
                wid, keys, vals, refs = 0, [], [], None
                for m, _, w in fields(v):
                    if m == 1: wid = w
                    elif m == 2: keys = packed(w)
                    elif m == 3: vals = packed(w)
                    elif m == 8: refs = w
                tags = tags_of(strings, keys, vals)
                if wid in member_ways or wanted(tags):
                    nodes = delta(packed(refs))
                    ways[wid] = (nodes, tags)
                    needed.update(nodes)
    print('ways %d, nodes needed %d' % (len(ways), len(needed)), flush=True)

    # 3) зангилааны координат
    coords = {}
    for raw in blocks(path):
        granularity, lat_offset, lon_offset = 100, 0, 0
        for n, _, v in fields(raw):
            if n == 17: granularity = v
            elif n == 19: lat_offset = v
            elif n == 20: lon_offset = v
        scale = granularity * 1e-9
        for group in groups(raw):
            for n, _, v in fields(group):
                if n != 2:
                    continue
                ids = lats = lons = None
                for m, _, w in fields(v):
                    if m == 1: ids = delta(packed(w))
                    elif m == 8: lats = w
                    elif m == 9: lons = w
                hit = [i for i, nid in enumerate(ids) if nid in needed]
                if not hit:
                    continue
                la, lo = delta(packed(lats)), delta(packed(lons))
                for i in hit:
                    coords[ids[i]] = (lat_offset * 1e-9 + la[i] * scale, lon_offset * 1e-9 + lo[i] * scale)
    print('coordinates %d' % len(coords), flush=True)

    def geometry(refs):
        return [{'lat': coords[r][0], 'lon': coords[r][1]} for r in refs if r in coords]

    out = {index: [] for index in indices}
    for wid, (refs, tags) in ways.items():
        if not wanted(tags):
            continue
        geom = geometry(refs)
        if len(geom) < 2:
            continue
        towns = {town_of(g['lat'], g['lon']) for g in geom[::max(1, len(geom) // 8)]} - {-1}
        for town in towns:
            out[town].append({'type': 'way', 'id': wid, 'tags': tags, 'geometry': geom})
    for rid, tags, members in relations:
        parts = [{'type': 'way', 'ref': mid, 'role': role, 'geometry': geometry(ways[mid][0])}
                 for role, mid in members if mid in ways]
        parts = [p for p in parts if p['geometry']]
        if not parts:
            continue
        towns = {town_of(g['lat'], g['lon']) for p in parts for g in p['geometry'][::max(1, len(p['geometry']) // 4)]} - {-1}
        for town in towns:
            out[town].append({'type': 'relation', 'id': rid, 'tags': tags, 'members': parts})

    folder = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data', 'towns')
    for index in indices:
        body = json.dumps({'generator': 'pbf_water.py (Geofabrik extract)',
                           'osm3s': {'copyright': 'The data included in this document is from www.openstreetmap.org. '
                                                  'The data is made available under ODbL.'},
                           'elements': out[index]}, ensure_ascii=False).encode('utf-8')
        with gzip.open(os.path.join(folder, '%02d_water.json.gz' % index), 'wb', 9) as f:
            f.write(body)
        print('%2d %s: %d water / forest features, %d KB' % (index, CENTERS[index][0], len(out[index]), len(body) // 1024), flush=True)


if __name__ == '__main__':
    main()
