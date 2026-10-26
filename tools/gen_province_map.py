# Аяны газрын зургийн аймгийн хил (src/ProvinceMap.cpp): geoBoundaries-ийн Монголын ADM1 (OpenStreetMap, ODbL).
# Хөрш аймгуудын хуваалцсан хилийг нэг удаа хялбаршуулж хоёуланд нь ашиглана: хил хооронд зай, давхцал гарахгүй.
#
# Өгөгдөл: https://www.geoboundaries.org/api/current/gbOpen/MNG/ADM1/ -> simplifiedGeometryGeoJSON
# Хэрэглээ:  python tools/gen_province_map.py adm1.geojson [зөвшөөрөх алдаа, градус = 0.012]
import json
import math
import os
import sys

# Тоглоомын аймгийн дараалал (src/Regions.cpp) -> geoBoundaries-ийн нэр
ORDER = ['Bayan-Ölgii', 'Uvs', 'Khovd', 'Zavkhan', 'Govi-Altai', 'Hovsgel', 'Bayankhongor', 'Arkhangai', 'Bulgan',
         'Övörkhangai', 'Orkhon', 'Ömnögovi', 'Selenge', 'Darkhan-Uul', 'Töv', 'Ulaanbaatar', 'Dundgovi', 'Govisumber',
         'Dornogovi', 'Khentii', 'Sükhbaatar', 'Dornod']


def key(p):
    return (round(p[0], 6), round(p[1], 6))


def simplify(points, tolerance):
    # Douglas-Peucker (төгсгөлийн хоёр цэг үлдэнэ)
    if len(points) < 3:
        return list(points)
    keep = [False] * len(points)
    keep[0] = keep[-1] = True
    stack = [(0, len(points) - 1)]
    while stack:
        a, b = stack.pop()
        ax, ay = points[a]
        bx, by = points[b]
        dx, dy = bx - ax, by - ay
        length = math.hypot(dx, dy)
        best, index = -1.0, -1
        for i in range(a + 1, b):
            px, py = points[i]
            d = abs(dy * (px - ax) - dx * (py - ay)) / length if length > 1e-12 else math.hypot(px - ax, py - ay)
            if d > best:
                best, index = d, i
        if index >= 0 and best > tolerance:
            keep[index] = True
            stack += [(a, index), (index, b)]
    return [p for p, k in zip(points, keep) if k]


def main(path, tolerance):
    data = json.load(open(path, encoding='utf-8'))
    features = {f['properties']['shapeName']: f['geometry'] for f in data['features']}
    # Аймаг бүрийн цагираг: (аймаг, хэсэг/нүх, цэгүүд)
    rings = []
    for province, name in enumerate(ORDER):
        geometry = features[name]
        polygons = geometry['coordinates'] if geometry['type'] == 'MultiPolygon' else [geometry['coordinates']]
        for polygon in polygons:
            for k, ring in enumerate(polygon):
                points = [tuple(p) for p in ring]
                if key(points[0]) == key(points[-1]):
                    points = points[:-1]
                rings.append((province, 'part' if k == 0 else 'hole', points))

    # Цэг бүрийг аль аймгууд хуваалцдаг. Хуваалцагчид өөрчлөгдөх цэг (уулзвар) дээр хилийг хэсэгчилнэ.
    owners = {}
    for province, _, points in rings:
        for p in points:
            owners.setdefault(key(p), set()).add(province)

    cache = {}
    out = [{'parts': [], 'holes': []} for _ in ORDER]
    total = 0
    for province, kind, points in rings:
        n = len(points)
        sets = [frozenset(owners[key(p)]) for p in points]
        junctions = [i for i in range(n) if sets[i] != sets[i - 1] or sets[i] != sets[(i + 1) % n] or len(sets[i]) > 2]
        if not junctions:  # бүтэн цагираг нэг хөрштэй (жишээ нь Дархан-Уул Сэлэнгэ дотор): хамгийн бага цэгээс
            junctions = [min(range(n), key=lambda i: key(points[i]))]
        result = []
        for j, start in enumerate(junctions):
            end = junctions[(j + 1) % len(junctions)]
            arc = [points[(start + i) % n] for i in range(((end - start) % n or n) + 1)]
            forward = tuple(key(p) for p in arc)
            backward = forward[::-1]
            if backward in cache:
                simplified = cache[backward][::-1]
            else:
                simplified = cache.setdefault(forward, simplify(arc, tolerance))
            result += simplified[:-1]
        if len(result) < 3:
            continue
        # Цагийн зүүний эсрэг (уртраг, өргөрөг): хэсэг нь эерэг талбайтай, нүх нь сөрөг (Polygon.h-тэй адил)
        area = sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(result, result[1:] + result[:1]))
        if (area < 0) == (kind == 'part'):
            result.reverse()
        out[province]['parts' if kind == 'part' else 'holes'].append(result)
        total += len(result)

    # Хамгийн том хэсэг эхэнд (товшилт, нэр бичих цэг)
    for shape in out:
        shape['parts'].sort(key=lambda r: -abs(sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(r, r[1:] + r[:1]))))

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    lines = ['// Энэ файлыг tools/gen_province_map.py үүсгэнэ (гараар бүү засаарай).',
             '// Аймгуудын хил: geoBoundaries (gbOpen MNG ADM1), эх сурвалж OpenStreetMap, ODbL 1.0.',
             '#include "ProvinceMap.h"', '', 'namespace', '{']
    def ring_text(ring):
        return '{ ' + ', '.join('{ %.3ff, %.3ff }' % p for p in ring) + ' }'
    lines.append('    const std::vector<ProvinceMapShape> kShapes = {')
    for province, shape in enumerate(out):
        lines.append('        // %d %s' % (province + 1, ORDER[province]))
        lines.append('        { {')
        for ring in shape['parts']:
            lines.append('            ' + ring_text(ring) + ',')
        lines.append('          }, {')
        for ring in shape['holes']:
            lines.append('            ' + ring_text(ring) + ',')
        lines.append('          } },')
    lines += ['    };', '}', '', 'const std::vector<ProvinceMapShape>& ProvinceMapShapes() { return kShapes; }', '']
    with open(os.path.join(root, 'src', 'ProvinceMap.cpp'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines))
    print('%d points (tolerance %.4f deg)' % (total, tolerance))
    for province, shape in enumerate(out):
        print('  %-14s parts %d holes %d points %d' % (ORDER[province], len(shape['parts']), len(shape['holes']),
                                                        sum(len(r) for r in shape['parts'] + shape['holes'])))


if __name__ == '__main__':
    main(sys.argv[1], float(sys.argv[2]) if len(sys.argv) > 2 else 0.012)
