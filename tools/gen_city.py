"""Улаанбаатар ба аймгийн төвүүдийн OpenStreetMap өгөгдлөөс тоглоомын хотын файл үүсгэнэ.

    python tools/gen_city.py tools/data/ub_center_osm.json.gz tools/data/ub_poi_osm.json.gz \
        tools/data/ub_relations_osm.json.gz assets/city/ub.bin
    python tools/gen_city.py --town 0 1 2 ...     (tools/fetch_towns.py-ийн татсан -> assets/city/pNN.bin)

Аймгийн төвд даалгаврын газруудыг автоматаар сонгоно: застав (тоглоомын 0, 0: хуаран, дэлгүүр), төвийн талбай,
агуулах, ШТС (жинхэнэ колонк), хот айл, жижиг суурин (POI kind 7..11).

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
  u32 pois;      poi:      u8 kind, f32 x, f32 z, u16 bytes, utf-8 name
                 (kind 7: ШТС барих зөрүү, 8: төвийн талбай, 9: агуулах, 10: хот айл, 11: жижиг суурин)
  lines kind: 0 гол, 1 горхи, 2 төмөр зам, 3 банзан хашаа, 4 чулуун хана
"""
import gzip, json, math, os, struct, sys

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
POI_KIND = {'fuel': 0, 'square': 1, 'stadium': 2, 'police': 3, 'hospital': 4, 'museum': 5, 'attraction': 5, 'government': 6,
            'marketplace': 1}

cos0 = math.cos(math.radians(LAT0))


def set_origin(lat, lon):
    global LAT0, LON0, cos0
    LAT0, LON0 = lat, lon
    cos0 = math.cos(math.radians(lat))


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


def features(elements):
    """OSM way-уудаас зам, барилга, талбай, шугам (тоглоомын координатаар)."""
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
            pts = ring(geometry, 0.5)  # хучилттай талбай
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
        elif t.get('barrier') in ('fence', 'wall'):
            pts = simplify([project(p) for p in geometry], 0.4)
            if len(pts) >= 2 and inside_extent(pts):
                lines.append((3 if t['barrier'] == 'fence' else 4, 0.1, pts))  # хашаа (банз / чулуу)
        else:
            key = t.get('natural') if t.get('natural') == 'water' else t.get('leisure') or t.get('landuse')
            if key in AREA_KIND:
                pts = ring(geometry, 0.8)
                if pts and inside_extent(pts) and signed_area(pts) > 40.0:
                    areas.append((AREA_KIND[key], pts))
    return roads, buildings, areas, lines


def places(elements):
    pois = []
    for e in elements:
        t = e.get('tags', {})
        c = e.get('center') or ({'lat': e['lat'], 'lon': e['lon']} if 'lat' in e else None)
        if not c:
            continue
        kind = t.get('amenity') or t.get('place') or t.get('leisure') or t.get('tourism') or t.get('building') or t.get('office')
        kind = 'government' if kind == 'townhall' else kind
        if kind not in POI_KIND:
            continue
        x, z = project(c)
        if abs(x) < EXTENT and abs(z) < EXTENT:
            pois.append((POI_KIND[kind], x, z, t.get('name', '')))
    return pois


def segment_distance(px, pz, a, b):
    dx, dz = b[0] - a[0], b[1] - a[1]
    length = dx * dx + dz * dz
    t = max(0.0, min(1.0, ((px - a[0]) * dx + (pz - a[1]) * dz) / length)) if length > 0 else 0.0
    return math.hypot(px - a[0] - dx * t, pz - a[1] - dz * t)


class Occupancy:
    """4 м торын барилга, замын эзэлсэн нүднүүд: газар сонгоход."""
    CELL = 4.0

    def __init__(self, roads, buildings):
        self.building, self.road, self.major = set(), set(), set()
        for _, _, _, pts in buildings:
            xs = [p[0] for p in pts]; zs = [p[1] for p in pts]
            for ix in range(int(math.floor(min(xs) / self.CELL)), int(math.floor(max(xs) / self.CELL)) + 1):
                for iz in range(int(math.floor(min(zs) / self.CELL)), int(math.floor(max(zs) / self.CELL)) + 1):
                    self.building.add((ix, iz))
        for klass, _, half, pts in roads:
            target = self.major if klass <= 4 else self.road
            for i in range(len(pts) - 1):
                (ax, az), (bx, bz) = pts[i], pts[i + 1]
                steps = max(1, int(math.dist(pts[i], pts[i + 1]) / 2.0))
                for s in range(steps + 1):
                    x, z = ax + (bx - ax) * s / steps, az + (bz - az) * s / steps
                    r = int(math.ceil(half / self.CELL))
                    for dx in range(-r, r + 1):
                        for dz in range(-r, r + 1):
                            target.add((int(math.floor(x / self.CELL)) + dx, int(math.floor(z / self.CELL)) + dz))

    def free(self, x0, z0, x1, z1, minor_roads=True):
        """Тэгш өнцөгт барилга, (minor_roads бол бүх) замаас чөлөөтэй эсэх."""
        for ix in range(int(math.floor(x0 / self.CELL)), int(math.floor(x1 / self.CELL)) + 1):
            for iz in range(int(math.floor(z0 / self.CELL)), int(math.floor(z1 / self.CELL)) + 1):
                key = (ix, iz)
                if key in self.building or key in self.major or (minor_roads and key in self.road):
                    return False
        return True

    def clearance(self, x, z, limit, cells=('building',)):
        """(x, z)-ээс хамгийн ойрын барилга хүртэлх зай (limit хүртэл)."""
        sets = [getattr(self, c) for c in cells]
        cx, cz = int(math.floor(x / self.CELL)), int(math.floor(z / self.CELL))
        for r in range(0, int(limit / self.CELL) + 1):
            for dx in range(-r, r + 1):
                for dz in (-r, r) if abs(dx) != r else range(-r, r + 1):
                    if any((cx + dx, cz + dz) in s for s in sets):
                        return max(0.0, (r - 1) * self.CELL)
        return limit


def choose_sites(roads, buildings, pois):
    """Аймгийн төвийн даалгаврын газрууд (төвийн координатаар). Буцаах: (застав хүртэлх шилжилт, газрууд)."""
    occ = Occupancy(roads, buildings)
    major = [(k, h, pts) for k, d, h, pts in roads if k <= 3]

    def road_distance(x, z, roads_list):
        best = 1e9
        for _, half, pts in roads_list:
            if min(p[0] for p in pts) > x + best or max(p[0] for p in pts) < x - best: continue
            if min(p[1] for p in pts) > z + best or max(p[1] for p in pts) < z - best: continue
            for i in range(len(pts) - 1):
                best = min(best, segment_distance(x, z, pts[i], pts[i + 1]) - half)
        return best

    all_roads = [(k, h, pts) for k, d, h, pts in roads]

    # 1. Застав (дэлгүүртэй хуаран, тоглогчийн эхлэх цэг): хуарангийн тэгш өнцөгт (-72..40, -40..40)-д том
    #    барилга, том зам байхгүй (жижиг байшин, гэрийг тоглоом хасна: src/Structures.cpp CityClearings),
    #    төвд ойр, том замын дэргэд. Энэ цэг нь тоглоомын (0, 0) болно.
    grid = {}
    for _, _, _, pts in buildings:
        xs = [p[0] for p in pts]; zs = [p[1] for p in pts]
        box = (min(xs), min(zs), max(xs), max(zs), signed_area(pts))
        for ix in range(int(box[0] // 50), int(box[2] // 50) + 1):
            for iz in range(int(box[1] // 50), int(box[3] // 50) + 1):
                grid.setdefault((ix, iz), []).append(box)

    def clearable(x0, z0, x1, z1):
        seen, count = set(), 0
        for ix in range(int(x0 // 50), int(x1 // 50) + 1):
            for iz in range(int(z0 // 50), int(z1 // 50) + 1):
                for box in grid.get((ix, iz), []):
                    if id(box) in seen or box[2] < x0 or box[0] > x1 or box[3] < z0 or box[1] > z1:
                        continue
                    seen.add(id(box))
                    count += 1
                    if box[4] > 160.0 or count > 14:
                        return False
        return True

    best = None
    for radius in (450.0, 1200.0, 2000.0):
        for gx in range(-int(radius), int(radius) + 1, 12):
            for gz in range(-int(radius), int(radius) + 1, 12):
                if math.hypot(gx, gz) > radius or not clearable(gx - 72, gz - 40, gx + 40, gz + 40):
                    continue
                if not occ.free(gx - 72, gz - 40, gx + 40, gz + 40, minor_roads=False) and \
                   any((ix, iz) in occ.major for ix in range(int((gx - 72) // 4), int((gx + 40) // 4) + 1)
                       for iz in range(int((gz - 40) // 4), int((gz + 40) // 4) + 1)):
                    continue
                score = math.hypot(gx, gz)
                if major:
                    score += 1.5 * max(0.0, road_distance(gx, gz, major) - 45.0)
                if best is None or score < best[0]:
                    best = (score, gx, gz)
        if best:
            break
    ox, oz = (best[1], best[2]) if best else (0.0, 0.0)

    sites = []  # (kind, x, z) төвийн координатаар
    camp_rect = (ox - 75, oz - 42, ox + 42, oz + 42)
    in_camp = lambda x, z, m: camp_rect[0] - m < x < camp_rect[2] + m and camp_rect[1] - m < z < camp_rect[3] + m

    # 2. Аймгийн төвийн талбай: талбай / засаг даргын тамгын газрын дэргэдэх задгай газар
    anchors = [(x, z) for k, x, z, n in pois if k in (POI_KIND['square'], POI_KIND['government'])] or [(0.0, 0.0)]
    anchor = min(anchors, key=lambda p: math.hypot(*p))
    town = None
    for gx in range(-260, 261, 8):
        for gz in range(-260, 261, 8):
            x, z = anchor[0] + gx, anchor[1] + gz
            if in_camp(x, z, 25):
                continue
            c = occ.clearance(x, z, 30.0)
            if c < 12.0:
                continue
            score = math.hypot(gx, gz) - 4.0 * c
            if town is None or score < town[0]:
                town = (score, x, z)
    town = (town[1], town[2]) if town else (ox + 60.0, oz + 60.0)
    sites.append((8, town[0], town[1]))

    # 3. Агуулах (Warehouse(cx, cz): 16 x 10 м, хаалга +X): төвөөс 250-1500 м, замын дэргэд, чөлөөтэй
    store = None
    for gx in range(-1500, 1501, 16):
        for gz in range(-1500, 1501, 16):
            d = math.hypot(gx, gz)
            if d < 250 or d > 1500 or in_camp(gx, gz, 30) or not occ.free(gx - 12, gz - 10, gx + 22, gz + 10):
                continue
            rd = road_distance(gx + 20, gz, all_roads)
            if rd > 25:
                continue
            score = d * 0.3 + rd
            if store is None or score < store[0]:
                store = (score, gx, gz)
    if store:
        sites.append((9, store[1], store[2]))

    # 4. ШТС: жинхэнэ бензин колонк (загварын тэгш өнцөгт 18..52, -20..-3 чөлөөтэй), байхгүй бол замын дэргэд
    stations = []
    def station_free(sx, sz, allow_small):
        rect = (18 + sx, -20 + sz, 52 + sx, -3 + sz)
        if in_camp(rect[0], rect[1], 0) or in_camp(rect[2], rect[3], 0):
            return False
        for _, _, _, pts in buildings:
            bx = [q[0] for q in pts]; bz = [q[1] for q in pts]
            if max(bx) > rect[0] and min(bx) < rect[2] and max(bz) > rect[1] and min(bz) < rect[3]:
                if not allow_small or signed_area(pts) > 450.0:
                    return False
        return occ.free(rect[0], rect[1], rect[2], rect[3], minor_roads=False) or not allow_small and False
    for kind, x, z, name in sorted(pois, key=lambda poi: math.hypot(poi[1], poi[2])):
        if kind != POI_KIND['fuel'] or abs(x - ox) > 2400 or abs(z - oz) > 2400:
            continue
        sx, sz = x - 30.0, z + 11.5
        if any(math.hypot(sx - a, sz - b) < 160 for a, b in stations):
            continue
        rect = (18 + sx, -20 + sz, 52 + sx, -3 + sz)
        blocked = any(segment_distance(px, pz, pts[i], pts[i + 1]) < half + 0.5
                      for _, half, pts in major
                      for px, pz in [(rect[0] + (rect[2] - rect[0]) * i / 8, rect[1] + (rect[3] - rect[1]) * j / 4)
                                     for i in range(9) for j in range(5)]
                      for i in range(len(pts) - 1)) if major else False
        if not blocked and station_free(sx, sz, True):
            stations.append((sx, sz))
    if not stations:
        for gx in range(-1200, 1201, 16):
            for gz in range(-1200, 1201, 16):
                if math.hypot(gx, gz) < 150 or not occ.free(18 + gx, -22 + gz, 52 + gx, -1 + gz):
                    continue
                if in_camp(18 + gx, -20 + gz, 10) or in_camp(52 + gx, -3 + gz, 10):
                    continue
                if road_distance(35 + gx, gz + 4, all_roads) < 12:
                    stations.append((gx, gz))
                    break
            if stations:
                break
    for sx, sz in stations:
        sites.append((7, sx, sz))

    # 5. Хот айл (малчдын отор): барилгаас 200 м-ээс хол тал хээр, замаас зайтай
    #    Талбай даяар жигд тархахаар: нүднүүдийг тогтмол санамсаргүй дарааллаар шалгана.
    import random
    cells = [(gx + ox, gz + oz) for gx in range(-2350, 2351, 50) for gz in range(-2350, 2351, 50)]
    random.Random(7).shuffle(cells)
    camps = []
    for x, z in cells:
        if len(camps) >= 30:
            break
        if any(math.hypot(x - a, z - b) < 330 for a, b in camps):
            continue
        if occ.clearance(x, z, 200.0, ('building', 'major', 'road')) < 60.0 or occ.clearance(x, z, 200.0) < 200.0:
            continue
        camps.append((x, z))
    camps.sort(key=lambda c: math.hypot(c[0] - ox, c[1] - oz))  # ойрынх нь эхэнд (даалгаврын Camp(0))
    for x, z in camps:
        sites.append((10, x, z))

    # 6. Жижиг суурин: төвөөс гарах замуудын дагуу 600-2200 м-т, барилгаас 120 м-ээс хол
    hamlets = []
    for klass, half, pts in sorted(all_roads, key=lambda r: r[0]):
        for i in range(len(pts) - 1):
            for t in (0.0, 0.5):
                x = pts[i][0] + (pts[i + 1][0] - pts[i][0]) * t
                z = pts[i][1] + (pts[i + 1][1] - pts[i][1]) * t
                hx, hz = x + 32.0, z  # суурингийн урд гудамж (x - 30) замын дэргэд
                d = math.hypot(hx - ox, hz - oz)
                if d < 600 or d > 2200 or abs(hx - ox) > 2300 or abs(hz - oz) > 2300:
                    continue
                if any(math.hypot(hx - a, hz - b) < 450 for a, b in hamlets):
                    continue
                if occ.clearance(hx, hz, 120.0) < 110.0 or not occ.free(hx - 24, hz - 28, hx + 24, hz + 28):
                    continue
                hamlets.append((hx, hz))
        if len(hamlets) >= 5:
            break
    for x, z in hamlets[:5]:
        sites.append((11, x, z))
    return (ox, oz), sites


def write(out_path, roads, buildings, areas, lines, pois):
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
    count = lambda k: sum(1 for p in pois if p[0] == k)
    print('%s: roads %d, buildings %d %s, areas %d, lines %d (fences %d), stations %d, camps %d, hamlets %d, %d KB'
          % (out_path, len(roads), len(buildings), kinds, len(areas), len(lines), sum(1 for l in lines if l[0] >= 3),
             count(7), count(10), count(11), len(out) // 1024))


def shifted(origin, roads, buildings, areas, lines, pois):
    ox, oz = origin
    move = lambda pts: [(x - ox, z - oz) for x, z in pts]
    return ([(k, d, h, move(p)) for k, d, h, p in roads], [(k, l, h, move(p)) for k, l, h, p in buildings],
            [(k, move(p)) for k, p in areas], [(k, h, move(p)) for k, h, p in lines],
            [(k, x - ox, z - oz, n) for k, x, z, n in pois])


def with_relations(elements):
    """Multipolygon (Төрийн ордон гэх мэт): гадна хүрээ бүрийг тусдаа way болгоно (дотор хашааг үл тооно)."""
    result = []
    for e in elements:
        if e.get('type') == 'relation':
            tags = {k: v for k, v in e.get('tags', {}).items() if k != 'type'}
            for outline in outer_rings(e):
                result.append({'type': 'way', 'tags': tags, 'geometry': outline})
        else:
            result.append(e)
    return result


def ulaanbaatar(city_path, poi_path, relation_path, out_path):
    set_origin(47.918850, 106.917580)  # Сүхбаатарын талбай
    roads, buildings, areas, lines = features(with_relations(load(city_path) + load(relation_path)))
    lines = [l for l in lines if l[0] < 3]
    # Сүхбаатарын талбай (OSM-д хучилттай талбай болж тэмдэглэгдээгүй): зүүн талын Их сургуулийн гудамж,
    # баруун талын Сүхбаатарын гудамж, хойд талын Төрийн ордон, урд талын Хонхтой цэцэрлэгийн хооронд.
    areas.append((AREA_KIND['plaza'], [(84.0, -112.0), (84.0, 94.0), (-101.0, 94.0), (-101.0, -112.0)]))
    pois = places(load(poi_path))

    # ШТС: эхнийх нь "хотын аварга" даалгаврын ШТС (src/City.h kCityGasOffset), бусад нь жинхэнэ колонкууд
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
    pois += [(7, ox, oz, '') for ox, oz in stations]
    pois.append((8, -18.0, -342.0, 'Чойжин ламын сүм'))  # хотын төвийн даалгавар
    write(out_path, roads, buildings, areas, lines, pois)


def town(index, out_path):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from fetch_towns import TOWNS
    name, lat, lon = TOWNS[index]
    set_origin(lat, lon)
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data', 'towns', '%02d.json.gz' % index)
    elements = load(path)
    shapes = [e for e in elements if 'geometry' in e or 'members' in e]
    roads, buildings, areas, lines = features(with_relations(shapes))
    pois = places([e for e in elements if 'center' in e or e.get('type') == 'node'])
    origin, sites = choose_sites(roads, buildings, pois)
    pois += [(k, x, z, '') for k, x, z in sites]
    print('%s: outpost (game origin) at %.0f, %.0f from the town centre' % (name, origin[0], origin[1]))
    write(out_path, *shifted(origin, roads, buildings, areas, lines, pois))


if __name__ == '__main__':
    if sys.argv[1] == '--town':
        for arg in sys.argv[2:]:
            index = int(arg)
            town(index, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'assets', 'city', 'p%02d.bin' % index))
    else:
        ulaanbaatar(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4])
