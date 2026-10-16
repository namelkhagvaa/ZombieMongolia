# Ястай боловч арьсны жингүй (бүх орой нэг ясанд, жишээ нь Hips-д холбогдсон) Mixamo загварт жинг автоматаар
# үүсгэнэ: орой бүрийг хамгийн ойрын хоёр ясны хэсэгт (үенээс хүүхэд үе хүртэлх шугам) зайны урвуугаар
# холбоно. Гар, хөлийн хувьд тухайн талын (зүүн/баруун) ясыг л сонгоно: хоёр хөл, гар хоорондоо наалдахгүй.
#
# Хэрэглээ:  python tools/auto_skin.py assets/giant_zombie_mesh.glb
# (tools/fix_bind_pose.py-ийн дараа ажиллуулна: ясны байрлал зөв байх ёстой.)
import json
import math
import struct
import sys

# Жин авах яс ба хүүхэд үе (хэсгийн төгсгөл). Хуруунуудыг гартай нэгтгэнэ.
SEGMENTS = [
    ('Hips', 'Spine'), ('Spine', 'Spine1'), ('Spine1', 'Spine2'), ('Spine2', 'Neck'), ('Neck', 'Head'), ('Head', None),
    ('LeftShoulder', 'LeftArm'), ('LeftArm', 'LeftForeArm'), ('LeftForeArm', 'LeftHand'), ('LeftHand', 'LeftHandMiddle1'),
    ('RightShoulder', 'RightArm'), ('RightArm', 'RightForeArm'), ('RightForeArm', 'RightHand'), ('RightHand', 'RightHandMiddle1'),
    ('LeftUpLeg', 'LeftLeg'), ('LeftLeg', 'LeftFoot'), ('LeftFoot', 'LeftToeBase'), ('LeftToeBase', 'LeftToe_End'),
    ('RightUpLeg', 'RightLeg'), ('RightLeg', 'RightFoot'), ('RightFoot', 'RightToeBase'), ('RightToeBase', 'RightToe_End'),
]


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


def segment_distance(p, a, b):
    ab = [b[i] - a[i] for i in range(3)]
    ap = [p[i] - a[i] for i in range(3)]
    length = sum(x * x for x in ab)
    t = 0.0 if length < 1e-12 else max(0.0, min(1.0, sum(x * y for x, y in zip(ap, ab)) / length))
    return math.sqrt(sum((p[i] - a[i] - ab[i] * t) ** 2 for i in range(3)))


def main(path):
    gltf, binary = read_glb(path)
    skin = gltf['skins'][0]
    joints = skin['joints']
    names = [gltf['nodes'][n]['name'].split(':')[-1] for n in joints]
    index = {name: k for k, name in enumerate(names)}

    accessor = gltf['accessors'][skin['inverseBindMatrices']]
    view = gltf['bufferViews'][accessor['bufferView']]
    offset = view.get('byteOffset', 0) + accessor.get('byteOffset', 0)
    position = []
    for k in range(len(joints)):
        world = mat_inverse(list(struct.unpack_from('<16f', binary, offset + k * 64)))
        position.append((world[12], world[13], world[14]))

    segments = []
    for bone, child in SEGMENTS:
        if bone not in index:
            continue
        a = position[index[bone]]
        if child and child in index:
            b = position[index[child]]
        else:
            b = (a[0], a[1] + 0.06, a[2])  # толгой: дээш богино хэсэг
        side = 1 if bone.startswith('Left') else -1 if bone.startswith('Right') else 0
        kind = 'leg' if ('Leg' in bone or 'Foot' in bone or 'Toe' in bone) else                'arm' if ('Arm' in bone or 'Hand' in bone or 'Shoulder' in bone) else 'body'
        segments.append((index[bone], a, b, side, kind))

    primitive = gltf['meshes'][0]['primitives'][0]
    pos_accessor = gltf['accessors'][primitive['attributes']['POSITION']]
    pos_view = gltf['bufferViews'][pos_accessor['bufferView']]
    pos_offset = pos_view.get('byteOffset', 0) + pos_accessor.get('byteOffset', 0)
    stride = pos_view.get('byteStride', 12)
    count = pos_accessor['count']

    # Биеийн тал: Mixamo-д зүүн тал нь Hips-ээс LeftUpLeg-ийн зүгт (+X эсвэл -X).
    left_sign = 1.0 if position[index['LeftUpLeg']][0] > position[index['Hips']][0] else -1.0
    center_x = position[index['Hips']][0]
    # Хөлийн яс зөвхөн гуяны үеэс доош, биеийн гол хэсэгт (унжсан гар хөлд татагдахгүй, бүс урагдахгүй).
    leg_top = position[index['LeftUpLeg']][1]
    leg_reach = abs(position[index['LeftLeg']][0] - center_x) * 1.9
    # Гарын яс зөвхөн мөрний үеэс гадна талд (их бие, хөл гарт татагдахгүй).
    arm_inner = abs(position[index['LeftArm']][0] - center_x) * 0.75

    out_joints, out_weights = [], []
    for v in range(count):
        p = struct.unpack_from('<3f', binary, pos_offset + v * stride)
        side = 1 if (p[0] - center_x) * left_sign > 0.0 else -1
        near = abs(p[0] - center_x) < 0.015  # төв шугам: хоёр талын яс хоёулаа болно
        candidates = []
        lateral = abs(p[0] - center_x)
        for bone, a, b, bone_side, kind in segments:
            if bone_side != 0 and bone_side != side and not near:
                continue
            if kind == 'leg' and (p[1] > leg_top or lateral > leg_reach):
                continue
            if kind == 'arm' and lateral < arm_inner:
                continue
            candidates.append((segment_distance(p, a, b), bone))
        if len(candidates) < 2:
            candidates.append((1.0, index['Hips']))
        candidates.sort()
        best = candidates[:2]
        inverse = [1.0 / (d ** 4 + 1e-8) for d, _ in best]
        total = sum(inverse)
        weights = [w / total for w in inverse]
        out_joints.append((best[0][1], best[1][1], 0, 0))
        out_weights.append((weights[0], weights[1], 0.0, 0.0))

    def append(values, component, kind, fmt):
        while len(binary) % 4:
            binary.append(0)
        start = len(binary)
        for value in values:
            binary.extend(struct.pack('<' + fmt, *value))
        gltf['bufferViews'].append({'buffer': 0, 'byteOffset': start, 'byteLength': len(binary) - start, 'target': 34962})
        gltf['accessors'].append({'bufferView': len(gltf['bufferViews']) - 1, 'componentType': component,
                                  'count': len(values), 'type': kind})
        return len(gltf['accessors']) - 1

    primitive['attributes']['JOINTS_0'] = append(out_joints, 5121, 'VEC4', '4B')
    primitive['attributes']['WEIGHTS_0'] = append(out_weights, 5126, 'VEC4', '4f')
    gltf['buffers'] = [{'byteLength': len(binary)}]
    write_glb(path, gltf, binary)
    print(f'{path}: {count} vertices re-weighted to {len(segments)} bones')


if __name__ == '__main__':
    main(sys.argv[1])
