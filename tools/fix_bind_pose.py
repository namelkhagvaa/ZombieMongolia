# Зарим GLB (жишээ нь Tripo-гийн giant_zombie) ясны зангилаандаа байрлал, эргэлт (translation/rotation)
# бичээгүй, зөвхөн inverseBindMatrices-д хадгалдаг. raylib ясны анхны байрлалыг зангилаанаас уншдаг тул бүх
# яс 0 цэгт нийлж, хөдөлгөөн ажиллахгүй (загвар гулсана). Энэ скрипт зангилаа бүрийн локал хувиргалтыг
# inverseBindMatrices-аас сэргээж бичнэ.
#
# Хэрэглээ:  python tools/fix_bind_pose.py assets/giant_zombie_mesh.glb
import json
import math
import struct
import sys


def read_glb(path):
    data = open(path, 'rb').read()
    json_length = struct.unpack_from('<I', data, 12)[0]
    gltf = json.loads(data[20:20 + json_length])
    start = 20 + json_length
    length = struct.unpack_from('<I', data, start)[0]
    return gltf, bytearray(data[start + 8:start + 8 + length])


def write_glb(path, gltf, binary):
    while len(binary) % 4:
        binary.append(0)
    text = json.dumps(gltf, separators=(',', ':')).encode('utf-8')
    while len(text) % 4:
        text += b' '
    with open(path, 'wb') as f:
        f.write(struct.pack('<III', 0x46546C67, 2, 28 + len(text) + len(binary)))
        f.write(struct.pack('<II', len(text), 0x4E4F534A) + text)
        f.write(struct.pack('<II', len(binary), 0x004E4942) + binary)


def mat_mul(a, b):  # баганаар (glTF)
    return [sum(a[k * 4 + r] * b[c * 4 + k] for k in range(4)) for c in range(4) for r in range(4)]


def mat_inverse(m):
    rows = [[m[c * 4 + r] for c in range(4)] + [1.0 if r == k else 0.0 for k in range(4)] for r in range(4)]
    for c in range(4):
        p = max(range(c, 4), key=lambda r: abs(rows[r][c]))
        rows[c], rows[p] = rows[p], rows[c]
        d = rows[c][c]
        rows[c] = [x / d for x in rows[c]]
        for r in range(4):
            if r != c:
                f = rows[r][c]
                rows[r] = [x - f * y for x, y in zip(rows[r], rows[c])]
    return [rows[r][4 + c] for c in range(4) for r in range(4)]


def to_quaternion(m):
    # Баганын урт (scale)-ыг хасаад эргэлтийн матрицаас кватернион
    cols = [m[0:3], m[4:7], m[8:11]]
    scale = [math.sqrt(sum(x * x for x in c)) or 1.0 for c in cols]
    r = [[cols[c][row] / scale[c] for c in range(3)] for row in range(3)]
    trace = r[0][0] + r[1][1] + r[2][2]
    if trace > 0:
        s = math.sqrt(trace + 1.0) * 2
        q = ((r[2][1] - r[1][2]) / s, (r[0][2] - r[2][0]) / s, (r[1][0] - r[0][1]) / s, 0.25 * s)
    elif r[0][0] > r[1][1] and r[0][0] > r[2][2]:
        s = math.sqrt(1.0 + r[0][0] - r[1][1] - r[2][2]) * 2
        q = (0.25 * s, (r[0][1] + r[1][0]) / s, (r[0][2] + r[2][0]) / s, (r[2][1] - r[1][2]) / s)
    elif r[1][1] > r[2][2]:
        s = math.sqrt(1.0 + r[1][1] - r[0][0] - r[2][2]) * 2
        q = ((r[0][1] + r[1][0]) / s, 0.25 * s, (r[1][2] + r[2][1]) / s, (r[0][2] - r[2][0]) / s)
    else:
        s = math.sqrt(1.0 + r[2][2] - r[0][0] - r[1][1]) * 2
        q = ((r[0][2] + r[2][0]) / s, (r[1][2] + r[2][1]) / s, 0.25 * s, (r[1][0] - r[0][1]) / s)
    length = math.sqrt(sum(x * x for x in q))
    return [x / length for x in q], scale


def main(path):
    gltf, binary = read_glb(path)
    skin = gltf['skins'][0]
    accessor = gltf['accessors'][skin['inverseBindMatrices']]
    view = gltf['bufferViews'][accessor['bufferView']]
    offset = view.get('byteOffset', 0) + accessor.get('byteOffset', 0)
    joints = skin['joints']
    world = {}
    for k, node in enumerate(joints):
        ibm = list(struct.unpack_from('<16f', binary, offset + k * 64))
        world[node] = mat_inverse(ibm)

    parent = {}
    for i, node in enumerate(gltf['nodes']):
        for child in node.get('children', []):
            parent[child] = i

    fixed = 0
    for node in joints:
        up = parent.get(node)
        local = world[node] if up not in world else mat_mul(mat_inverse(world[up]), world[node])
        rotation, scale = to_quaternion(local)
        entry = gltf['nodes'][node]
        for key in ('matrix', 'translation', 'rotation', 'scale'):
            entry.pop(key, None)
        entry['translation'] = [local[12], local[13], local[14]]
        entry['rotation'] = rotation
        if any(abs(s - 1.0) > 1e-4 for s in scale):
            entry['scale'] = scale
        fixed += 1
    write_glb(path, gltf, binary)
    print(f'{path}: bind pose restored for {fixed} joints')


if __name__ == '__main__':
    main(sys.argv[1])
