# Биеэс бага зэрэг зайтай хөвж үүссэн жижиг хэсгийг (Tripo: дуулганы залаа ~3 см зайтай) биед наана: хэсгийг
# хамгийн ойрын цэг хүртэл шилжүүлнэ. Тайван байрлалд ард нь нуугдаж харагдахгүй ч толгой эргэхэд зай нь цоорхой болж
# харагддаг. Биед шүргэлцсэн, эсвэл хэт хол (зориуд тусдаа) хэсгийг хөндөхгүй.
#
# Хэрэглээ:  python tools/attach_floating.py assets/mongolian_hero_mesh.glb [--min-gap 0.02] [--max-gap 0.06]
import argparse
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from auto_skin import read_glb, write_glb


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('path')
    parser.add_argument('--min-gap', type=float, default=0.02, help='үүнээс ойр бол биед шүргэлцсэн гэж үзнэ')
    parser.add_argument('--max-gap', type=float, default=0.06, help='үүнээс хол бол зориуд тусдаа гэж үзнэ')
    parser.add_argument('--sink', type=float, default=0.004, help='наалдсан цэгээс биеийн дотогш оруулах зай')
    args = parser.parse_args()

    gltf, binary = read_glb(args.path)
    moved_total = 0
    for mesh in gltf['meshes']:
        for primitive in mesh['primitives']:
            accessor = gltf['accessors'][primitive['attributes']['POSITION']]
            view = gltf['bufferViews'][accessor['bufferView']]
            offset = view.get('byteOffset', 0) + accessor.get('byteOffset', 0)
            stride = view.get('byteStride') or 12
            count = accessor['count']
            points = [struct.unpack_from('<3f', binary, offset + stride * v) for v in range(count)]

            index_accessor = gltf['accessors'][primitive['indices']]
            index_view = gltf['bufferViews'][index_accessor['bufferView']]
            fmt = {5121: 'B', 5123: 'H', 5125: 'I'}[index_accessor['componentType']]
            indices = struct.unpack_from('<%d%s' % (index_accessor['count'], fmt), binary,
                                         index_view.get('byteOffset', 0) + index_accessor.get('byteOffset', 0))

            # Арлууд: гурвалжнаар, ижил байрлалтай (UV-ийн заадас) оройг нэгтгэж
            root = list(range(count))

            def find(v):
                while root[v] != v:
                    root[v] = root[root[v]]
                    v = root[v]
                return v

            seen = {}
            for v, p in enumerate(points):
                key = (round(p[0], 5), round(p[1], 5), round(p[2], 5))
                if key in seen:
                    root[find(v)] = find(seen[key])
                else:
                    seen[key] = v
            for t in range(0, len(indices), 3):
                a, b, c = indices[t:t + 3]
                root[find(a)] = find(b)
                root[find(b)] = find(c)
            islands = {}
            for v in range(count):
                islands.setdefault(find(v), []).append(v)
            body = max(islands.values(), key=len)
            body_points = [points[v] for v in body]

            # Ойрын цэг хайх тор
            cell = args.max_gap
            grid = {}
            for p in body_points:
                grid.setdefault(tuple(int(math.floor(c / cell)) for c in p), []).append(p)

            def nearest_body(p):
                base = [int(math.floor(c / cell)) for c in p]
                best, point = 1e9, None
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        for dz in (-1, 0, 1):
                            for q in grid.get((base[0] + dx, base[1] + dy, base[2] + dz), ()):
                                d = math.dist(p, q)
                                if d < best:
                                    best, point = d, q
                return best, point

            for vertices in islands.values():
                if vertices is body or len(vertices) * 20 > count:
                    continue
                gap, source, target = 1e9, None, None
                for v in vertices:
                    d, q = nearest_body(points[v])
                    if q is not None and d < gap:
                        gap, source, target = d, points[v], q
                if not (args.min_gap <= gap <= args.max_gap):
                    continue
                direction = [(target[k] - source[k]) / gap for k in range(3)]
                shift = [direction[k] * (gap + args.sink) for k in range(3)]
                for v in vertices:
                    p = points[v]
                    struct.pack_into('<3f', binary, offset + stride * v, p[0] + shift[0], p[1] + shift[1], p[2] + shift[2])
                for bound in ('min', 'max'):
                    accessor.pop(bound, None)
                moved_total += 1
                print(f'{args.path}: island of {len(vertices)} vertices was {gap:.4f} away, moved by '
                      f'({shift[0]:.4f} {shift[1]:.4f} {shift[2]:.4f})')
            if moved_total:
                ps = [struct.unpack_from('<3f', binary, offset + stride * v) for v in range(count)]
                accessor['min'] = [min(p[k] for p in ps) for k in range(3)]
                accessor['max'] = [max(p[k] for p in ps) for k in range(3)]
    if moved_total:
        write_glb(args.path, gltf, binary)
    else:
        print(f'{args.path}: no floating pieces')


if __name__ == '__main__':
    main()
