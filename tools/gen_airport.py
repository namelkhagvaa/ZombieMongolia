"""Буянт-Ухаа нисэх онгоцны буудлын OpenStreetMap өгөгдлөөс src/AirportData.h үүсгэнэ.

    python tools/gen_airport.py tools/data/buyant_ukhaa_osm.json src/AirportData.h

Өгөгдөл: (c) OpenStreetMap contributors, ODbL 1.0 (https://www.openstreetmap.org/copyright).
Жинхэнэ хэмжээ, жинхэнэ чиглэлтэй; тоглоомын ертөнцөд OFFSET-оор шилжүүлнэ (хойд = +Z, зүүн = -X).
"""
import json, math, sys

OFFSET = (-800.0, 350.0)          # тоглоомын ертөнц дэх байрлал (метр)
LAT0, LON0 = 47.843, 106.766      # проекцийн төв
UB_BOUNDARY = [(x * 1.24, z * 1.24) for x, z in
               [(2067, -563), (1216, 1100), (-768, 1515), (-1901, 476), (-1618, -978), (83, -1394), (1500, -1186)]]

src, dst = sys.argv[1], sys.argv[2]
elements = json.load(open(src, encoding='utf-8'))['elements']
cos0 = math.cos(math.radians(LAT0))


def world(geometry):
    pts = [(-(p['lon'] - LON0) * 111320.0 * cos0 + OFFSET[0], (p['lat'] - LAT0) * 111320.0 + OFFSET[1]) for p in geometry]
    if len(pts) > 2 and math.dist(pts[0], pts[-1]) < 0.01:
        pts = pts[:-1]
    return pts


def inside(poly, p):
    x, y = p
    r = False
    for i in range(len(poly)):
        a, b = poly[i], poly[i - 1]
        if (a[1] > y) != (b[1] > y) and x < (b[0] - a[0]) * (y - a[1]) / (b[1] - a[1]) + a[0]:
            r = not r
    return r


def seg_dist(p, a, b):
    abx, aby = b[0] - a[0], b[1] - a[1]
    l = abx * abx + aby * aby
    t = max(0.0, min(1.0, ((p[0] - a[0]) * abx + (p[1] - a[1]) * aby) / l)) if l else 0.0
    return math.hypot(p[0] - a[0] - abx * t, p[1] - a[1] - aby * t)


def ring_dist(poly, p):
    return min(seg_dist(p, poly[i - 1], poly[i]) for i in range(len(poly)))


def hull(points):
    pts = sorted(set(points))
    cross = lambda o, a, b: (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def signed_area(poly):
    return sum(poly[i - 1][0] * poly[i][1] - poly[i][0] * poly[i - 1][1] for i in range(len(poly))) * 0.5


def ccw(poly):
    return poly if signed_area(poly) > 0 else poly[::-1]


strips, aprons, helipads, buildings, feature_points = [], [], [], [], []
for e in elements:
    tags = e.get('tags', {})
    kind = tags.get('aeroway')
    if 'geometry' not in e:
        continue
    pts = world(e['geometry'])
    if kind == 'runway':
        gravel = tags.get('surface') == 'gravel'
        strips.append((pts, float(tags.get('width', 45)) * 0.5, 1 if gravel else 0))
    elif kind == 'taxiway':
        strips.append((pts, float(tags.get('width', 18)) * 0.5, 2))
    elif kind == 'apron':
        aprons.append(ccw(pts))
    elif kind == 'helipad':
        helipads.append(ccw(pts))
    elif kind in ('terminal', 'hangar'):
        buildings.append((ccw(pts), tags, kind))
    else:
        continue
    feature_points += pts

core = hull(feature_points)
for e in elements:
    tags = e.get('tags', {})
    if 'building' not in tags or tags.get('aeroway') or 'geometry' not in e:
        continue
    pts = world(e['geometry'])
    cx, cz = sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts)
    near = inside(core, (cx, cz)) or ring_dist(core, (cx, cz)) < 250.0
    if near and all(inside(UB_BOUNDARY, p) and ring_dist(UB_BOUNDARY, p) > 25.0 for p in pts):
        buildings.append((ccw(pts), tags, 'building'))

flat = ccw(hull(feature_points + [p for b in buildings for p in b[0]]))


# Байшингийн төрөл: 0 терминал, 1 ангар, 2 бусад, 3 олон давхар, 4 сүм
def building_info(pts, tags, kind):
    levels = tags.get('building:levels')
    a = abs(signed_area(pts))
    b = tags.get('building')
    if kind == 'terminal':
        return 0, (16.0 if pts is main_terminal else 10.0)
    if kind == 'hangar' or b == 'hangar':
        return 1, max(8.0, min(22.0, math.sqrt(a) * 0.22))
    if b == 'church':
        return 4, 9.0
    if levels:
        return (3 if int(levels) >= 3 else 2), 3.1 * int(levels) + 0.6
    if b in ('house', 'garage', 'roof'):
        return 2, 3.6
    return 2, (7.0 if a > 600 else 4.5)


main_terminal = max((b[0] for b in buildings if b[2] == 'terminal'), key=lambda p: abs(signed_area(p)))
points = []


def push(pts):
    first = len(points)
    points.extend(pts)
    return first, len(pts)


strip_rows = [(*push(p), hw, k) for p, hw, k in strips]
apron_rows = [push(p) for p in aprons]
heli_rows = [push(p) for p in helipads]
bld_rows = []
for pts, tags, kind in buildings:
    k, h = building_info(pts, tags, kind)
    bld_rows.append((*push(pts), h, k))
flat_row = push(flat)

main = max((s for s in strips if s[2] == 0), key=lambda s: math.dist(s[0][0], s[0][-1]))[0]
term = max((b[0] for b in buildings if b[2] == 'terminal'), key=lambda p: abs(signed_area(p)))
tx, tz = sum(p[0] for p in term) / len(term), sum(p[1] for p in term) / len(term)

out = [
    '// Буянт-Ухаа олон улсын нисэх онгоцны буудал: жинхэнэ хэмжээ, жинхэнэ чиглэл.',
    '// tools/gen_airport.py-ээр үүсгэсэн, гараар бүү засаарай.',
    '// Газрын зургийн өгөгдөл: (c) OpenStreetMap contributors, ODbL 1.0.',
    '#pragma once',
    '',
    '#include "raylib.h"',
    '',
    '#include <cstdint>',
    '',
    'namespace airport',
    '{',
    '    struct Strip { int first, count; float halfWidth; uint8_t kind; }; // kind: 0 засмал ХБЗ, 1 хайрган ХБЗ, 2 явгалах зам',
    '    struct Ring { int first, count; };',
    '    struct Building { int first, count; float height; uint8_t kind; }; // kind: 0 терминал, 1 ангар, 2 бусад, 3 олон давхар, 4 сүм',
    '',
    '    inline constexpr Vector2 kPoints[] = {',
]
for i in range(0, len(points), 5):
    out.append('        ' + ' '.join('{ %.1ff, %.1ff },' % p for p in points[i:i + 5]))
out.append('    };')
out.append('    inline constexpr Strip kStrips[] = {')
for row in strip_rows:
    out.append('        { %d, %d, %.1ff, %d },' % row)
out.append('    };')
for name, rows in (('kAprons', apron_rows), ('kHelipads', heli_rows)):
    out.append('    inline constexpr Ring %s[] = {' % name)
    out.append('        ' + ' '.join('{ %d, %d },' % r for r in rows))
    out.append('    };')
out.append('    inline constexpr Building kBuildings[] = {')
for row in bld_rows:
    out.append('        { %d, %d, %.1ff, %d },' % row)
out.append('    };')
out.append('    // Тэгшлэх талбай: бүх байгууламжийг хамарсан гүдгэр олон өнцөгт (цагийн зүүний эсрэг) + kFlatMargin')
out.append('    inline constexpr Ring kFlatHull = { %d, %d };' % flat_row)
out.append('    inline constexpr float kFlatMargin = 40.0f;')
out.append('    // Гол терминалын төв ба гол ХБЗ-ын хоёр үзүүр')
out.append('    inline constexpr Vector2 kTerminal = { %.1ff, %.1ff };' % (tx, tz))
out.append('    inline constexpr Vector2 kRunwayEnds[2] = { { %.1ff, %.1ff }, { %.1ff, %.1ff } };' % (*main[0], *main[-1]))
out.append('}')
out.append('')
open(dst, 'w', encoding='utf-8', newline='\n').write('\n'.join(out))
print('points', len(points), 'strips', len(strips), 'aprons', len(aprons), 'helipads', len(helipads),
      'buildings', len(buildings), 'flat hull', len(flat))
