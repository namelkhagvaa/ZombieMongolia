"""Microsoft Global ML Building Footprints: аймгийн төвүүдийн барилгын хүрээ (хиймэл дагуулын зургаас гаргасан).

    python tools/ms_buildings.py [дугаар ...]      -> tools/data/towns/<дугаар>_ms.json.gz

OSM-д дутуу барилгыг нөхөхөд (tools/gen_city.py нь OSM-ийн барилгатай давхцаагүйг л нэмнэ). Өгөгдөл нь 9-р
томруулалтын quadkey хавтангаар (tools/data/ms_links.csv: dataset-links.csv). Зарим барилгад өндрийн таамаг (м) бий.
Өгөгдөл: Microsoft, Open Data Commons Open Database License (ODbL).
"""
import csv, gzip, io, json, math, os, sys, urllib.request

ROOT = os.path.dirname(os.path.abspath(__file__))
LINKS = os.path.join(ROOT, 'data', 'ms_links.csv')
LINKS_URL = 'https://minedbuildings.z5.web.core.windows.net/global-buildings/dataset-links.csv'
CACHE = os.path.join(ROOT, 'data', 'ms_cache')
HALF = 2700.0  # м (tools/gen_city.py-ийн EXTENT)


def quadkey(lat, lon, zoom=9):
    n = 2 ** zoom
    x = int((lon + 180.0) / 360.0 * n)
    y = int((1.0 - math.asinh(math.tan(math.radians(lat))) / math.pi) / 2.0 * n)
    key = ''
    for z in range(zoom, 0, -1):
        mask = 1 << (z - 1)
        key += str((1 if x & mask else 0) + (2 if y & mask else 0))
    return key


def links():
    if not os.path.exists(LINKS):
        urllib.request.urlretrieve(LINKS_URL, LINKS)
    out = {}
    with open(LINKS, encoding='utf-8') as f:
        for row in csv.DictReader(f):
            if row['Location'] == 'Mongolia':
                out.setdefault(row['QuadKey'], []).append(row['Url'])
    return out


def read(url):
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, url.split('quadkey=')[1].replace('/', '_'))
    if not os.path.exists(path):
        request = urllib.request.Request(url, headers={'User-Agent': 'ZombieMongolia-mapprep/1.0'})
        with urllib.request.urlopen(request, timeout=300) as response:
            body = response.read()
        with open(path, 'wb') as f:
            f.write(body)
    with gzip.open(path, 'rt', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if line:
                yield json.loads(line)


def town(index, table):
    from fetch_towns import TOWNS
    name, lat, lon = TOWNS[index]
    dlat = HALF / 111320.0
    dlon = dlat / math.cos(math.radians(lat))
    keys = {quadkey(lat + a * dlat, lon + b * dlon) for a in (-1, 1) for b in (-1, 1)}
    elements = []
    for key in sorted(keys):
        for url in table.get(key, []):
            for feature in read(url):
                geometry = feature.get('geometry') or {}
                if geometry.get('type') != 'Polygon':
                    continue
                ring = geometry['coordinates'][0]
                cx = sum(p[0] for p in ring) / len(ring)
                cy = sum(p[1] for p in ring) / len(ring)
                if abs(cy - lat) > dlat or abs(cx - lon) > dlon:
                    continue
                tags = {'building': 'yes', 'source': 'microsoft'}
                height = (feature.get('properties') or {}).get('height', -1)
                if isinstance(height, (int, float)) and height > 0:
                    tags['height'] = '%.1f' % height
                elements.append({'type': 'way', 'tags': tags, 'geometry': [{'lat': p[1], 'lon': p[0]} for p in ring]})
    out = os.path.join(ROOT, 'data', 'towns', '%02d_ms.json.gz' % index)
    with gzip.open(out, 'wt', encoding='utf-8') as f:
        json.dump({'elements': elements}, f)
    print('%2d %s: %d buildings (quadkeys %s)' % (index, name, len(elements), ','.join(sorted(keys))), flush=True)


def main():
    sys.path.insert(0, ROOT)
    from fetch_towns import TOWNS
    table = links()
    for index in [int(a) for a in sys.argv[1:]] or sorted(TOWNS):
        town(index, table)


if __name__ == '__main__':
    main()
