"""Шалгах зураг: аймгийн төвийн газрын өндрийг (зүүн: хуучин SRTM / AWS Terrain Tiles, баруун: Copernicus GLO-30)
сүүдэрлэсэн рельефээр, дээр нь OSM-ийн гол (цэнхэр), зам (цагаан), барилга (саарал) зурна.

    python tools/compare_terrain.py <аймгийн дугаар> [<гаралт.png>]

Гол хөндийгөөр урсаж, зам толгодын бэлээр явж байвал өндрийн тор ба OSM зөв давхцаж байна (кодын алдаагүй).
Тоглох талбай: төвөөс ±3 км, хойд нь дээшээ, зүүн нь баруун тийш (газрын зургийн ердийн чиглэл).
"""
import math, os, struct, sys, zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_city
from gen_city import features, load, set_origin, with_relations

HALF = 3000.0
SIZE = 600  # пиксел (10 м)


def png(path, width, height, rows):
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)
    raw = b''.join(b'\x00' + bytes(row) for row in rows)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def heights(source, lat0, lon0):
    k = 111320.0 * math.cos(math.radians(lat0))
    step = HALF * 2.0 / SIZE
    grid = []
    for py in range(SIZE):
        z = HALF - (py + 0.5) * step  # хойд = дээш
        lat = lat0 + z / 111320.0
        grid.append([source.at(lat, lon0 + (-HALF + (px + 0.5) * step) / k) for px in range(SIZE)])
    return grid, step


def shade(grid, step):
    lo = min(min(r) for r in grid)
    hi = max(max(r) for r in grid)
    image = []
    for y in range(SIZE):
        row = []
        for x in range(SIZE):
            dzdx = (grid[y][min(x + 1, SIZE - 1)] - grid[y][max(x - 1, 0)]) / (2 * step)
            dzdy = (grid[max(y - 1, 0)][x] - grid[min(y + 1, SIZE - 1)][x]) / (2 * step)
            # Баруун хойдоос гэрэл
            nx, ny, nz = -dzdx * 3.0, -dzdy * 3.0, 1.0
            length = math.sqrt(nx * nx + ny * ny + nz * nz)
            light = max(0.0, (nx * -0.6 + ny * 0.6 + nz * 0.55) / length)
            t = (grid[y][x] - lo) / max(1.0, hi - lo)
            base = (90 + 120 * t, 110 + 90 * t, 70 + 60 * t)  # нам: ногоон, өндөр: шаргал
            row.append([int(min(255, c * (0.35 + 0.9 * light))) for c in base])
        image.append(row)
    return image, lo, hi


def to_pixel(x, z):
    # gen_city.project: зүүн = -X, хойд = +Z
    return (-x + HALF) / (HALF * 2) * SIZE, (HALF - z) / (HALF * 2) * SIZE


def draw_line(image, a, b, color, width):
    (x0, y0), (x1, y1) = to_pixel(*a), to_pixel(*b)
    steps = int(max(abs(x1 - x0), abs(y1 - y0)) * 2) + 1
    r = width // 2
    for i in range(steps + 1):
        x = int(x0 + (x1 - x0) * i / steps)
        y = int(y0 + (y1 - y0) * i / steps)
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                if 0 <= x + dx < SIZE and 0 <= y + dy < SIZE:
                    image[y + dy][x + dx] = list(color)


def overlay(image, roads, buildings, lines):
    for _, _, _, pts in buildings:
        for i in range(len(pts)):
            draw_line(image, pts[i], pts[(i + 1) % len(pts)], (150, 150, 150), 1)
    for klass, dirt, half, pts in roads:
        for i in range(len(pts) - 1):
            draw_line(image, pts[i], pts[i + 1], (235, 235, 235) if not dirt else (205, 185, 150), 2 if klass <= 2 else 1)
    for kind, half, pts in lines:
        if kind > 1:
            continue
        for i in range(len(pts) - 1):
            draw_line(image, pts[i], pts[i + 1], (40, 120, 255), 3 if kind == 0 else 2)


def main():
    index = int(sys.argv[1])
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data', 'compare_%02d.png' % index)
    from fetch_towns import TOWNS
    import copdem, dem
    name, lat, lon = TOWNS[index]
    set_origin(lat, lon)
    folder = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data', 'towns')
    elements = load(os.path.join(folder, '%02d.json.gz' % index))
    shapes = [e for e in elements if 'geometry' in e or 'members' in e]
    roads, buildings, areas, lines = features(with_relations(shapes), aimag=True)
    try:
        water = [e for e in load(os.path.join(folder, '%02d_water.json.gz' % index)) if 'geometry' in e]
        lines += [l for l in features(water)[3] if l[0] <= 1]
    except FileNotFoundError:
        pass
    panels = []
    for label, source in (('srtm', dem.Elevation(13)), ('copernicus', copdem.Elevation())):
        grid, step = heights(source, lat, lon)
        image, lo, hi = shade(grid, step)
        overlay(image, roads, buildings, lines)
        print('%s %s: %.0f..%.0f m' % (name, label, lo, hi), flush=True)
        panels.append(image)
    gap = [[20, 20, 20]] * 8
    rows = [[c for pixel in panels[0][y] + gap + panels[1][y] for c in pixel] for y in range(SIZE)]
    png(out, SIZE * 2 + 8, SIZE, rows)
    print('wrote', out)


if __name__ == '__main__':
    main()
