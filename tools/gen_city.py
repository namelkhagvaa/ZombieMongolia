"""Улаанбаатарын төвийн OpenStreetMap өгөгдлөөс тоглоомын хотын файл (assets/city/ub.bin) үүсгэнэ.

    python tools/gen_city.py tools/data/ub_center_osm.json.gz tools/data/ub_poi_osm.json.gz         tools/data/ub_relations_osm.json.gz assets/city/ub.bin

Өгөгдөл: (c) OpenStreetMap contributors, ODbL 1.0 (https://www.openstreetmap.org/copyright).
Жинхэнэ хэмжээ, жинхэнэ чиглэлтэй. Төв нь Сүхбаатарын талбай (тоглоомын 0, 0); хойд = +Z, зүүн = -X
(tools/gen_airport.py-тэй ижил). Тоглох талбайн гадна (±EXTENT) хэсгийг хасна.

Өгөгдлийг дахин татах (Overpass API, bbox = төвөөс ±2.65 км):
  way[building], way[highway], way[waterway], way[natural=water], way[railway],
  way[leisure~park|stadium|pitch], way[landuse~grass|forest|cemetery]  ->  out geom tags
  nwr[amenity~fuel|police|hospital], nwr[place=square], nwr[leisure=stadium],
  nwr[tourism~museum|attraction], nwr[building=government]              ->  out center tags
  relation[building], relation[leisure~park|stadium|pitch], relation[natural=water],
  relation[landuse~grass|forest|cemetery]                                ->  out geom

Файлын бүтэц (little-endian), src/City.cpp уншина:
  "CITY" u32 version=1
  u32 roads;     road:     u8 class, u8 dirt, f32 halfWidth, u16 n, n * (f32 x, f32 z)
  u32 buildings; building: u8 type, u8 levels, f32 height, u16 n, n * (f32 x, f32 z)   (цагийн зүүний эсрэг)
  u32 areas;     area:     u8 kind, u16 n, n * (f32 x, f32 z)   (0 ус, 1 цэцэрлэг, 2 талбай, 3 ой, 4 оршуулга, 5 хучилттай талбай)
  u32 lines;     line:     u8 kind, f32 halfWidth, u16 n, n * (f32 x, f32 z)
  u32 pois;      poi:      u8 kind, f32 x, f32 z, u16 bytes, utf-8 name   (kind 7: ШТС барих зөрүү)
"""
import gzip, json, math, struct, sys

LAT0, LON0 = 47.918850, 106.917580   # Сүхбаатарын талбай
EXTENT = 2700.0                      # тоглох талбай (±2666 м) ба бага зэрэг илүү

# Замын анги (src/City.h-ийн CityRoad), өргөн (хагас, м)
ROAD_CLASS = {
    'motorway': (0, 11.0), 'trunk': (0, 11.0), 'motorway_link': (1, 4.5), 'trunk_link': (1, 4.5),
    'primary': (1, 8.5), 'primary_link': (1, 4.0), 'secondary': (2, 7.0), 'secondary_link': (2, 3.5),
    'tertiary': (3, 5.5), 'tertiary_link': (3, 3.0), 'unclassified': (4, 3.5), 'residential': (4, 3.5),
    'living_street': (4, 3.0), 'service': (5, 2.6), 'track': (6, 2.2), 'pedestrian': (7, 3.0),
}  # явган хүний зам (footway, path, cycleway, steps) тоглоомд хэрэггүй
DIRT_SURFACES = {'unpaved', 'dirt', 'gravel', 'ground', 'earth', 'sand', 'mud', 'grass', 'fine_gravel', 'compacted'}

# Барилгын төрөл (src/City.h-ийн CityBuilding)
GENERIC, APARTMENTS, HOUSE, GER, COMMERCIAL, SCHOOL, INDUSTRIAL, RELIGIOUS, GOVERNMENT, GARAGE = range(10)
BUILDING_TYPE = {
    'apartments': APARTMENTS, 'residential': APARTMENTS, 'dormitory': APARTMENTS,
    'house': HOUSE, 'detached': HOUSE, 'baishin': HOUSE, 'hut': HOUSE, 'cabin': HOUSE, 'bungalow': HOUSE,
    'ger': GER, 'yurt': GER,
    'commercial': COMMERCIAL, 'retail': COMMERCIAL, 'office': COMMERCIAL, 'hotel': COMMERCIAL, 'supermarket': COMMERCIAL,
    'mall': COMMERCIAL, 'kiosk': COMMERCIAL, 'bank': COMMERCIAL,
    'school': SCHOOL, 'university': SCHOOL, 'college': SCHOOL, 'kindergarten': SCHOOL, 'hospital': SCHOOL,
    'industrial': INDUSTRIAL, 'warehouse': INDUSTRIAL, 'factory': INDUSTRIAL, 'construction': INDUSTRIAL,
    'temple': RELIGIOUS, 'monastery': RELIGIOUS, 'church': RELIGIOUS, 'religious': RELIGIOUS,
    'government': GOVERNMENT, 'public': GOVERNMENT, 'civic': GOVERNMENT,
    'garage': GARAGE, 'garages': GARAGE, 'shed': GARAGE, 'roof': GARAGE, 'carport': GARAGE,
}

AREA_KIND = {'water': 0, 'park': 1, 'grass': 1, 'pitch': 2, 'stadium': 2, 'forest': 3, 'cemetery': 4, 'plaza': 5}

# OSM-д буруу эсвэл дутуу өндөртэй алдартай барилгууд (нэр -> өндөр, м)
HEIGHT_FIX = {'Засгийн газрын ордон': 24.0}
POI_KIND = {'fuel': 0, 'square': 1, 'stadium': 2, 'police': 3, 'hospital': 4, 'museum': 5, 'attraction': 5, 'government': 6}

cos0 = math.cos(math.radians(LAT0))


def project(p):
    return (-(p['lon'] - LON0) * 111320.0 * cos0, (p['lat'] - LAT0) * 111320.0)


def load(path):
    opener = gzip.open if path.endswith('.gz') else open
    with opener(path, 'rt', encoding='utf-8') as f:
        return json.load(f)['elements']


def simplify(points, tolerance):
    """Douglas-Peucker."""
    if len(points) < 3:
        return points
    keep = [False] * len(points)
    keep[0] = keep[-1] = True
    stack = [(0, len(points) - 1)]
    while stack:
        a, b = stack.pop()
        ax, az = points[a]; bx, bz = points[b]
        dx, dz = bx - ax, bz - az
        length = math.hypot(dx, dz)
        best, index = -1.0, -1
        for i in range(a + 1, b):
            px, pz = points[i]
            d = abs(dz * (px - ax) - dx * (pz - az)) / length if length > 1e-6 else math.hypot(px - ax, pz - az)
            if d > best:
                best, index = d, i
        if best > tolerance:
            keep[index] = True
            stack += [(a, index), (index, b)]
    return [p for p, k in zip(points, keep) if k]


def signed_area(poly):
    return 0.5 * sum(poly[i - 1][0] * poly[i][1] - poly[i][0] * poly[i - 1][1] for i in range(len(poly)))


def ring(geometry, tolerance):
    pts = [project(p) for p in geometry]
    if len(pts) > 2 and math.dist(pts[0], pts[-1]) < 0.01:
        pts = pts[:-1]
    if len(pts) < 3:
        return None
    pts = simplify(pts + [pts[0]], tolerance)[:-1]
    if len(pts) < 3:
        return None
    if signed_area(pts) < 0:
        pts.reverse()
    return pts


def min_area_rect(poly):
    """Хамгийн бага талбайтай эргэлдсэн тэгш өнцөгт (ирмэг бүрийн чиглэлээр туршина), цагийн зүүний эсрэг."""
    best = None
    for i in range(len(poly)):
        (ax, az), (bx, bz) = poly[i - 1], poly[i]
        length = math.hypot(bx - ax, bz - az)
        if length < 1e-6:
            continue
        ux, uz = (bx - ax) / length, (bz - az) / length
        us = [x * ux + z * uz for x, z in poly]
        vs = [-x * uz + z * ux for x, z in poly]
        area = (max(us) - min(us)) * (max(vs) - min(vs))
        if best is None or area < best[0]:
            best = (area, ux, uz, min(us), max(us), min(vs), max(vs))
    _, ux, uz, u0, u1, v0, v1 = best
    corner = lambda u, v: (u * ux - v * uz, u * uz + v * ux)
    rect = [corner(u0, v0), corner(u1, v0), corner(u1, v1), corner(u0, v1)]
    if signed_area(rect) < 0:
        rect.reverse()
    return rect, best[0]


def inside_extent(points):
    return any(abs(x) < EXTENT and abs(z) < EXTENT for x, z in points)


def number(text, default=None):
    try:
        return float(str(text).replace('m', '').split(';')[0].strip())
    except (TypeError, ValueError):
        return default


def building_height(tags, kind, area):
    if tags.get('name') in HEIGHT_FIX:
        height = HEIGHT_FIX[tags['name']]
        return height, max(1, round(height / 3.5))
    height = number(tags.get('height'))
    levels = number(tags.get('building:levels'))
    if height is None and levels is not None:
        height = levels * 3.0 + (1.0 if levels > 2 else 0.5)
    if height is None:
        if kind == APARTMENTS: height = 28.0       # 9 давхар
        elif kind == HOUSE: height = 4.0
        elif kind == GER: height = 3.0
        elif kind == GARAGE: height = 3.0
        elif kind == COMMERCIAL: height = 13.0 if area > 300 else 7.0
        elif kind == SCHOOL: height = 11.0
        elif kind == INDUSTRIAL: height = 8.0
        elif kind == RELIGIOUS: height = 10.0
        elif kind == GOVERNMENT: height = 16.0
        else:  # building=yes: талбайгаас таамаглана
            height = 3.5 if area < 60 else 6.5 if area < 250 else 13.0 if area < 900 else 19.0
    if levels is None:
        levels = max(1, round((height - 0.5) / 3.0))
    return min(height, 160.0), int(min(levels, 60))


def outer_rings(relation):
    """Multipolygon-ийн гадна хүрээнүүд: хэсэгчилсэн way-уудыг үзүүрээр нь залгана."""
    parts = [m['geometry'] for m in relation.get('members', []) if m.get('role') == 'outer' and m.get('geometry')]
    rings = []
    key = lambda p: (round(p['lat'], 7), round(p['lon'], 7))
    while parts:
        ring = list(parts.pop())
        changed = True
        while key(ring[0]) != key(ring[-1]) and changed:
            changed = False
            for i, part in enumerate(parts):
                if key(part[0]) == key(ring[-1]): ring += part[1:]
                elif key(part[-1]) == key(ring[-1]): ring += part[::-1][1:]
                elif key(part[-1]) == key(ring[0]): ring = part[:-1] + ring
                elif key(part[0]) == key(ring[0]): ring = part[::-1][:-1] + ring
                else: continue
                parts.pop(i)
                changed = True
                break
        if key(ring[0]) == key(ring[-1]):
            rings.append(ring)
    return rings


def main(city_path, poi_path, relation_path, out_path):
    elements = load(city_path)
    # Multipolygon (Төрийн ордон гэх мэт): гадна хүрээ бүрийг тусдаа way болгоно (дотор хашааг үл тооно)
    for r in load(relation_path):
        if r.get('type') != 'relation':
            continue
        tags = {k: v for k, v in r.get('tags', {}).items() if k != 'type'}
        for outline in outer_rings(r):
            elements.append({"type": "way", "tags": tags, "geometry": outline})
    roads, buildings, areas, lines = [], [], [], []

    # Гэр хорооллын тор: олон гэртэй газрын тодорхойгүй гудамж шороон
    ger_cells = {}
    for e in elements:
        t = e.get('tags', {})
        if t.get('building') in ('ger', 'yurt', 'house', 'hut') and e.get('geometry'):
            x, z = project(e['geometry'][0])
            key = (int(x // 80), int(z // 80))
            ger_cells[key] = ger_cells.get(key, 0) + 1

    for e in elements:
        t = e.get('tags', {})
        geometry = e.get('geometry')
        if not geometry:
            continue
        if 'building' in t:
            pts = ring(geometry, 0.35)
            if not pts or not inside_extent(pts):
                continue
            area = signed_area(pts)
            if area < 10.0:
                continue
            kind = BUILDING_TYPE.get(t['building'], GENERIC)
            if kind == GENERIC and area < 90 and t.get('amenity') is None:
                x, z = pts[0]
                if ger_cells.get((int(x // 80), int(z // 80)), 0) >= 4:
                    kind = HOUSE  # гэр хорооллын жижиг байшин
            height, levels = building_height(t, kind, area)
            # Жижиг байшин, гарааш, ойролцоогоор тэгш өнцөгт барилга: 4 булантай тэгш өнцөгт болгоно
            # (тоглоомд гурвалжин цөөрнө). Том, нарийн хэлбэртэй барилга хэлбэрээ хадгална.
            if len(pts) > 4:
                rect, rect_area = min_area_rect(pts)
                if (area < 160.0 and rect_area < area * 1.35) or rect_area < area * 1.08:
                    pts = rect
                else:
                    pts = simplify(pts + [pts[0]], 0.8)[:-1]
                    if len(pts) < 3 or signed_area(pts) <= 0:
                        continue
            buildings.append((kind, levels, height, pts))
        elif t.get('highway') == 'pedestrian' and t.get('area') == 'yes':
            pts = ring(geometry, 0.5)  # талбай (Сүхбаатарын талбай)
            if pts and inside_extent(pts) and signed_area(pts) > 40.0:
                areas.append((AREA_KIND['plaza'], pts))
        elif 'highway' in t and t['highway'] in ROAD_CLASS:
            pts = simplify([project(p) for p in geometry], 0.5)
            if len(pts) < 2 or not inside_extent(pts):
                continue
            klass, half = ROAD_CLASS[t['highway']]
            lanes = number(t.get('lanes'))
            if lanes and klass <= 3:
                half = max(half, lanes * 1.7)
            width = number(t.get('width'))
            if width:
                half = max(0.8, min(14.0, width * 0.5))
            surface = t.get('surface')
            dirt = surface in DIRT_SURFACES or t['highway'] in ('track', 'path')
            if surface is None and klass in (4, 5):
                x, z = pts[len(pts) // 2]
                dirt = ger_cells.get((int(x // 80), int(z // 80)), 0) >= 6
            roads.append((klass, 1 if dirt else 0, half, pts))
        elif 'railway' in t and t['railway'] in ('rail', 'light_rail'):
            pts = simplify([project(p) for p in geometry], 0.5)
            if len(pts) >= 2 and inside_extent(pts):
                lines.append((2, 2.0, pts))
        elif 'waterway' in t:
            pts = simplify([project(p) for p in geometry], 0.8)
            if len(pts) >= 2 and inside_extent(pts):
                half = 12.0 if t['waterway'] == 'river' else 2.0
                lines.append((0 if t['waterway'] == 'river' else 1, half, pts))
        else:
            key = t.get('natural') if t.get('natural') == 'water' else t.get('leisure') or t.get('landuse')
            if key in AREA_KIND:
                pts = ring(geometry, 0.8)
                if pts and inside_extent(pts) and signed_area(pts) > 40.0:
                    areas.append((AREA_KIND[key], pts))

    # Сүхбаатарын талбай (OSM-д хучилттай талбай болж тэмдэглэгдээгүй): зүүн талын Их сургуулийн гудамж,
    # баруун талын Сүхбаатарын гудамж, урд талын Энхтайваны өргөн чөлөө, хойд талын Төрийн ордны хооронд.
    areas.append((AREA_KIND['plaza'], [(84.0, -112.0), (84.0, 94.0), (-101.0, 94.0), (-101.0, -112.0)][::-1]
                  if signed_area([(84.0, -112.0), (84.0, 94.0), (-101.0, 94.0), (-101.0, -112.0)]) < 0
                  else [(84.0, -112.0), (84.0, 94.0), (-101.0, 94.0), (-101.0, -112.0)]))

    pois = []
    for e in load(poi_path):
        t = e.get('tags', {})
        c = e.get('center') or ({'lat': e['lat'], 'lon': e['lon']} if 'lat' in e else None)
        if not c:
            continue
        kind = t.get('amenity') or t.get('place') or t.get('leisure') or t.get('tourism') or t.get('building')
        if kind not in POI_KIND:
            continue
        x, z = project(c)
        if abs(x) < EXTENT and abs(z) < EXTENT:
            pois.append((POI_KIND[kind], x, z, t.get('name', '')))

    # Тоглоомын ШТС (саравч, түгээгүүр, дэлгүүр) барих жинхэнэ бензин колонкууд: POI kind 7 = station,
    # байрлал нь ШТС-ийн загварыг шилжүүлэх зөрүү (src/Structures.cpp GasStation). Загварын эзлэх тэгш өнцөгт
    # (18..52, -20..-3) том зам, том барилгатай давхцахгүй, бие биеэсээ 160 м-ээс хол байх ёстой.
    # Эхнийх нь "хотын аварга" даалгаврын ШТС (src/City.h kCityGasOffset).
    def segment_distance(px, pz, a, b):
        dx, dz = b[0] - a[0], b[1] - a[1]
        length = dx * dx + dz * dz
        t = max(0.0, min(1.0, ((px - a[0]) * dx + (pz - a[1]) * dz) / length)) if length > 0 else 0.0
        return math.hypot(px - a[0] - dx * t, pz - a[1] - dz * t)

    stations = [(320.0, -1103.5)]
    for kind, x, z, name in sorted(pois, key=lambda poi: math.hypot(poi[1], poi[2])):
        if kind != POI_KIND['fuel'] or abs(x) > 2450 or abs(z) > 2450:
            continue
        ox, oz = x - 30.0, z + 11.5
        rect = (18 + ox, -20 + oz, 52 + ox, -3 + oz)
        if any(math.hypot(ox - sx, oz - sz) < 160 for sx, sz in stations):
            continue
        samples = [(rect[0] + (rect[2] - rect[0]) * i / 8, rect[1] + (rect[3] - rect[1]) * j / 4) for i in range(9) for j in range(5)]
        blocked = False
        for klass, dirt, half, pts in roads:
            if klass > 4:
                continue  # хашааны зам ШТС-аар дайрч болно
            if max(q[0] for q in pts) < rect[0] - 15 or min(q[0] for q in pts) > rect[2] + 15 or \
               max(q[1] for q in pts) < rect[1] - 15 or min(q[1] for q in pts) > rect[3] + 15:
                continue
            if any(segment_distance(px, pz, pts[i], pts[i + 1]) < half + 0.5 for px, pz in samples for i in range(len(pts) - 1)):
                blocked = True
                break
        for _, _, _, pts in buildings:
            if blocked:
                break
            bx = [q[0] for q in pts]; bz = [q[1] for q in pts]
            if max(bx) > rect[0] and min(bx) < rect[2] and max(bz) > rect[1] and min(bz) < rect[3]:
                blocked = signed_area(pts) > 450.0  # колонкын өөрийн жижиг барилга, саравчийг хасаж болно
        if not blocked:
            stations.append((ox, oz))
    for ox, oz in stations:
        pois.append((7, ox, oz, ''))

    out = bytearray(b'CITY') + struct.pack('<I', 1)

    def points(pts):
        out.extend(struct.pack('<H', len(pts)))
        for x, z in pts:
            out.extend(struct.pack('<ff', x, z))

    out.extend(struct.pack('<I', len(roads)))
    for klass, dirt, half, pts in roads:
        out.extend(struct.pack('<BBf', klass, dirt, half)); points(pts)
    out.extend(struct.pack('<I', len(buildings)))
    for kind, levels, height, pts in buildings:
        out.extend(struct.pack('<BBf', kind, levels, height)); points(pts)
    out.extend(struct.pack('<I', len(areas)))
    for kind, pts in areas:
        out.extend(struct.pack('<B', kind)); points(pts)
    out.extend(struct.pack('<I', len(lines)))
    for kind, half, pts in lines:
        out.extend(struct.pack('<Bf', kind, half)); points(pts)
    out.extend(struct.pack('<I', len(pois)))
    for kind, x, z, name in pois:
        data = name.encode('utf-8')[:200]
        out.extend(struct.pack('<BffH', kind, x, z, len(data))); out.extend(data)

    with open(out_path, 'wb') as f:
        f.write(out)
    kinds = {}
    for b in buildings:
        kinds[b[0]] = kinds.get(b[0], 0) + 1
    print('roads %d (dirt %d), buildings %d %s, areas %d, lines %d, pois %d, gas stations %d, %d KB'
          % (len(roads), sum(r[1] for r in roads), len(buildings), kinds, len(areas), len(lines), len(pois), len(stations),
             len(out) // 1024))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4])
