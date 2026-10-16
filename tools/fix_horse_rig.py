# Tripo3D-ийн морины загварын эвдэрхий rig-ийг засаж тоглоомд бэлтгэнэ:
#   assets/horse_with_texure.glb (текстуртай; байхгүй бол assets/horse.glb)
#     ->  assets/horse_mesh.glb   (зассан яс, жин; текстургүй тул хурдан ачаална)
#         assets/horse_color.png  (загварын текстур 1024 px PNG; текстургүй загварт 4 өнгийн палитр)
#
# Tripo-гийн rig-ийн алдаа:
#   - хойд баруун хөл бүхэлдээ ганц ястай (нугарахгүй),
#   - хойд зүүн хөл тахилзуурын дээр тасарсан (доод хэсэг, туурайн ясгүй),
#   - ихэнх яс нэргүй (bone_17 г.м.), нэр нь тал, үүргээ илэрхийлэхгүй.
# Засвар: хойд хөл бүрт геометрээс ташаа -> өвдөг -> тахилзуур -> шагай гэсэн дөрвөн шинэ яс үүсгэж,
# хуучин хойд хөлийн ясны жинг тэдгээрт хуваарилна (ууц, гэдэстэй хуваалцсан жин хэвээр). Бүх ясыг үүргээр нь
# нэрлэнэ (тоглоом нэрээр нь хөдөлгөнө). Текстургүй загварт оройг үүргээр нь палитрын өнгөнд холбоно.
# raylib анхдагч тохиргоогоороо JPEG уншдаггүй тул текстурыг PNG болгоно (prepare_character.ps1-тэй адил).
#
# Хэрэглээ:  python tools/fix_horse_rig.py
import json
import math
import os
import struct
import subprocess
import tempfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEXTURED = os.path.join(ROOT, 'assets', 'horse_with_texure.glb')
SOURCE = TEXTURED if os.path.exists(TEXTURED) else os.path.join(ROOT, 'assets', 'horse.glb')
TEXTURE_SIZE = 1024
TARGET = os.path.join(ROOT, 'assets', 'horse_mesh.glb')
PALETTE = os.path.join(ROOT, 'assets', 'horse_color.png')

# Палитр (4x1): зүс, дэл/сүүл, туурай, хамар. UV нь пикселийн төвд.
COLORS = [(132, 86, 52), (40, 28, 22), (34, 30, 28), (78, 56, 44)]
COAT, MANE, HOOF, MUZZLE = range(4)

# Хуучин нэр -> шинэ нэр (тоглоом эдгээр нэрээр хөдөлгөнө). Морь -X зүг рүү харсан.
RENAME = {
    'tripo::Root': 'Root',
    'tripo::Spine_0': 'Pelvis',
    'bone_2': 'Tail_0', 'bone_3': 'Tail_1', 'bone_4': 'Tail_2',
    'tripo::Spine_1': 'Back_0', 'tripo::Spine_2': 'Back_1', 'tripo::Spine_3': 'Back_2',
    'tripo::Spine_4': 'Withers',
    'tripo::Head_0': 'Neck_0', 'tripo::Head_1': 'Neck_1', 'tripo::Head_2': 'Head',
    'tripo::Spine_5': 'FL_Shoulder', 'tripo::Spine_6': 'FL_Elbow',
    'tripo::0_Left_Limb_0': 'FL_Knee', 'tripo::0_Left_Limb_1': 'FL_Fetlock', 'tripo::0_Left_Limb_2': 'FL_Hoof',
    'bone_17': 'FR_Shoulder', 'bone_18': 'FR_Elbow',
    'tripo::0_Right_Limb_0': 'FR_Knee', 'tripo::0_Right_Limb_1': 'FR_Fetlock', 'tripo::0_Right_Limb_2': 'FR_Hoof',
    'bone_22': 'Chest_L', 'bone_23': 'Chest_R', 'bone_24': 'Belly',
    'bone_25': 'Unused_HL_0', 'tripo::1_Left_Limb_0': 'Unused_HL_1', 'tripo::1_Left_Limb_1': 'Unused_HL_2',
    'tripo::1_Right_Limb_0': 'Unused_HR_0',
}
OLD_HIND = {'bone_25', 'tripo::1_Left_Limb_0', 'tripo::1_Left_Limb_1', 'tripo::1_Right_Limb_0'}
TAIL = {'bone_2', 'bone_3', 'bone_4'}
NECK = {'tripo::Spine_4', 'tripo::Head_0', 'tripo::Head_1'}
LEGS = {'tripo::0_Left_Limb_1', 'tripo::0_Left_Limb_2', 'tripo::0_Right_Limb_1', 'tripo::0_Right_Limb_2'}


# ---------------------------------------------------------------- GLB
def read_glb(path):
    data = open(path, 'rb').read()
    assert struct.unpack_from('<I', data, 0)[0] == 0x46546C67, 'not a GLB'
    json_length = struct.unpack_from('<I', data, 12)[0]
    gltf = json.loads(data[20:20 + json_length])
    bin_start = 20 + json_length
    bin_length = struct.unpack_from('<I', data, bin_start)[0]
    return gltf, bytearray(data[bin_start + 8:bin_start + 8 + bin_length])


def compact(gltf, binary):
    # Зөвхөн accessor-ын ашигладаг bufferView-үүдийг үлдээнэ (зураг, солигдсон хуучин өгөгдөл хасагдана).
    used = sorted({a['bufferView'] for a in gltf['accessors'] if 'bufferView' in a})
    output = bytearray()
    remap = {}
    views = []
    for index in used:
        view = gltf['bufferViews'][index]
        while len(output) % 4:
            output.append(0)
        start = view.get('byteOffset', 0)
        copy = {'buffer': 0, 'byteOffset': len(output), 'byteLength': view['byteLength']}
        for key in ('byteStride', 'target'):
            if key in view:
                copy[key] = view[key]
        output.extend(binary[start:start + view['byteLength']])
        remap[index] = len(views)
        views.append(copy)
    for accessor in gltf['accessors']:
        if 'bufferView' in accessor:
            accessor['bufferView'] = remap[accessor['bufferView']]
    gltf['bufferViews'] = views
    return output


def export_texture(gltf, binary, path):
    # Загварын текстурыг (JPEG/PNG) TEXTURE_SIZE хүртэл жижигрүүлж PNG болгоно (Windows-ийн System.Drawing).
    image = gltf['images'][gltf['textures'][0]['source']]
    view = gltf['bufferViews'][image['bufferView']]
    start = view.get('byteOffset', 0)
    data = bytes(binary[start:start + view['byteLength']])
    with tempfile.TemporaryDirectory() as folder:
        source = os.path.join(folder, 'texture.bin')
        open(source, 'wb').write(data)
        script = (
            "Add-Type -AssemblyName System.Drawing;"
            f"$i=[Drawing.Image]::FromFile('{source}');"
            f"$s=[Math]::Min({TEXTURE_SIZE},$i.Width);"
            "$b=New-Object Drawing.Bitmap($s,$s);$g=[Drawing.Graphics]::FromImage($b);"
            "$g.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic;"
            "$g.DrawImage($i,0,0,$s,$s);"
            f"$b.Save('{path}',[Drawing.Imaging.ImageFormat]::Png);$g.Dispose();$b.Dispose();$i.Dispose()"
        )
        subprocess.run(['powershell', '-NoProfile', '-Command', script], check=True)


def write_glb(path, gltf, binary):
    while len(binary) % 4:
        binary.append(0)
    gltf['buffers'] = [{'byteLength': len(binary)}]
    text = json.dumps(gltf, separators=(',', ':')).encode('utf-8')
    while len(text) % 4:
        text += b' '
    total = 12 + 8 + len(text) + 8 + len(binary)
    with open(path, 'wb') as f:
        f.write(struct.pack('<III', 0x46546C67, 2, total))
        f.write(struct.pack('<II', len(text), 0x4E4F534A))
        f.write(text)
        f.write(struct.pack('<II', len(binary), 0x004E4942))
        f.write(binary)


COMPONENT = {5120: ('b', 1), 5121: ('B', 1), 5122: ('h', 2), 5123: ('H', 2), 5125: ('I', 4), 5126: ('f', 4)}
WIDTH = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}


def read_accessor(gltf, binary, index):
    accessor = gltf['accessors'][index]
    view = gltf['bufferViews'][accessor['bufferView']]
    fmt, size = COMPONENT[accessor['componentType']]
    width = WIDTH[accessor['type']]
    stride = view.get('byteStride', size * width)
    offset = view.get('byteOffset', 0) + accessor.get('byteOffset', 0)
    values = [struct.unpack_from('<' + fmt * width, binary, offset + k * stride) for k in range(accessor['count'])]
    if accessor.get('normalized') and fmt in 'BH':
        scale = 255.0 if fmt == 'B' else 65535.0
        values = [tuple(x / scale for x in v) for v in values]
    return values


def append_accessor(gltf, binary, values, component, kind, target=None):
    fmt, size = COMPONENT[component]
    while len(binary) % 4:
        binary.append(0)
    offset = len(binary)
    for v in values:
        binary.extend(struct.pack('<' + fmt * len(v), *v))
    view = {'buffer': 0, 'byteOffset': offset, 'byteLength': len(binary) - offset}
    if target:
        view['target'] = target
    gltf['bufferViews'].append(view)
    accessor = {'bufferView': len(gltf['bufferViews']) - 1, 'componentType': component, 'count': len(values), 'type': kind}
    gltf['accessors'].append(accessor)
    return len(gltf['accessors']) - 1


# ---------------------------------------------------------------- 4x4 (баганаар, glTF)
def mat_mul(a, b):
    return [sum(a[k * 4 + r] * b[c * 4 + k] for k in range(4)) for c in range(4) for r in range(4)]


def mat_inverse(m):
    rows = [[m[c * 4 + r] for c in range(4)] + [1.0 if r == k else 0.0 for k in range(4)] for r in range(4)]
    for c in range(4):
        pivot = max(range(c, 4), key=lambda r: abs(rows[r][c]))
        rows[c], rows[pivot] = rows[pivot], rows[c]
        p = rows[c][c]
        rows[c] = [x / p for x in rows[c]]
        for r in range(4):
            if r != c:
                f = rows[r][c]
                rows[r] = [x - f * y for x, y in zip(rows[r], rows[c])]
    return [rows[r][4 + c] for c in range(4) for r in range(4)]


def translation(p):
    return [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, p[0], p[1], p[2], 1]


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def segment_distance(p, a, b):
    ab = sub(b, a)
    t = max(0.0, min(1.0, sum(x * y for x, y in zip(sub(p, a), ab)) / max(1e-9, sum(x * x for x in ab))))
    q = (a[0] + ab[0] * t, a[1] + ab[1] * t, a[2] + ab[2] * t)
    return math.sqrt(sum(x * x for x in sub(p, q)))


# ---------------------------------------------------------------- PNG (палитр)
def write_png(path, pixels, width, height):
    raw = b''.join(b'\x00' + bytes([c for px in pixels[y * width:(y + 1) * width] for c in px]) for y in range(height))

    def chunk(tag, payload):
        return struct.pack('>I', len(payload)) + tag + payload + struct.pack('>I', zlib.crc32(tag + payload) & 0xFFFFFFFF)

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(raw, 9)))
        f.write(chunk(b'IEND', b''))


def main():
    gltf, binary = read_glb(SOURCE)
    skin = gltf['skins'][0]
    joints = list(skin['joints'])
    nodes = gltf['nodes']
    names = [nodes[j]['name'] for j in joints]
    ibm = read_accessor(gltf, binary, skin['inverseBindMatrices'])
    global_bind = [mat_inverse(list(m)) for m in ibm]
    bind_position = [(g[12], g[13], g[14]) for g in global_bind]
    by_name = {name: k for k, name in enumerate(names)}

    primitive = gltf['meshes'][0]['primitives'][0]
    attributes = primitive['attributes']
    positions = read_accessor(gltf, binary, attributes['POSITION'])
    normals = read_accessor(gltf, binary, attributes['NORMAL'])
    vertex_joints = read_accessor(gltf, binary, attributes['JOINTS_0'])
    vertex_weights = read_accessor(gltf, binary, attributes['WEIGHTS_0'])
    old_hind = {by_name[n] for n in OLD_HIND}

    # --- Хойд хөлийн шинэ үенүүд: хөлийн оройнуудын төв шугамаас (хоёр талыг тэгш хэмтэй болгож) ---
    def side_of(v):
        z = positions[v][2]
        if abs(z) > 1e-4:
            return 1 if z > 0 else -1
        right = sum(w for j, w in zip(vertex_joints[v], vertex_weights[v]) if names[j] == 'tripo::1_Right_Limb_0')
        return -1 if right > 0.5 else 1

    leg_vertices = [v for v in range(len(positions))
                    if positions[v][0] > 0.1 and sum(w for j, w in zip(vertex_joints[v], vertex_weights[v]) if j in old_hind) > 0.3]
    levels = {'stifle': 0.36, 'hock': 0.23, 'fetlock': 0.09, 'tip': 0.015}
    centers = {}
    for key, level in levels.items():
        xs, zs = [], []
        for v in leg_vertices:
            if abs(positions[v][1] - level) < 0.03:
                xs.append(positions[v][0])
                zs.append(abs(positions[v][2]))
        assert xs, f'no hind-leg vertices near y={level}'
        centers[key] = (sum(xs) / len(xs), level, sum(zs) / len(zs))
    hip = bind_position[by_name['bone_25']]
    hip_center = (hip[0], hip[1], (abs(hip[2]) + abs(bind_position[by_name['tripo::1_Right_Limb_0']][2])) * 0.5)

    def leg_chain(side):
        mirror = lambda p: (p[0], p[1], p[2] * side)
        return [mirror(hip_center), mirror(centers['stifle']), mirror(centers['hock']), mirror(centers['fetlock'])], \
               mirror(centers['tip'])

    # --- Шинэ зангилаа, яс ---
    pelvis = by_name['tripo::Spine_0']
    new_bones = {}
    for side, prefix in ((1, 'HL'), (-1, 'HR')):
        chain, tip = leg_chain(side)
        parent_joint = pelvis
        created = []
        for name, p in zip(('Hip', 'Stifle', 'Hock', 'Fetlock'), chain):
            parent_node = joints[parent_joint]
            local = mat_mul(mat_inverse(global_bind[parent_joint]), translation(p))
            nodes.append({'name': f'{prefix}_{name}', 'matrix': local})
            node = len(nodes) - 1
            nodes[parent_node].setdefault('children', []).append(node)
            joints.append(node)
            names.append(f'{prefix}_{name}')
            global_bind.append(translation(p))
            bind_position.append(p)
            parent_joint = len(joints) - 1
            created.append(parent_joint)
        new_bones[side] = (created, chain + [tip])

    # --- Сүүл: Tripo доод хагасыг (47 см) ганц ясаар хөдөлгөдөг тул мод шиг хатуу савладаг. Сүүлний доод
    #     хэсэгт гурван яс нэмж (Tail_3..5), сүүл намирч (долгио шиг) хөдөлдөг болгоно. Үенүүд сүүлний
    #     оройн төв шугам дээр (өндөр бүрийн оройн дундаж). ---
    tail_names = ['bone_2', 'bone_3', 'bone_4']
    tail_joints = [by_name[n] for n in tail_names]
    tail_vertices = [v for v in range(len(positions))
                     if sum(w for j, w in zip(vertex_joints[v], vertex_weights[v]) if j in tail_joints) > 0.5]
    tail_bottom = min(positions[v][1] for v in tail_vertices)
    top_joint = bind_position[by_name['bone_4']]

    def tail_center(height):
        near = [positions[v] for v in tail_vertices if abs(positions[v][1] - height) < 0.025]
        if not near:
            return (top_joint[0], height, top_joint[2])
        return (sum(p[0] for p in near) / len(near), height, sum(p[2] for p in near) / len(near))

    span = top_joint[1] - tail_bottom
    tail_chain = [tail_center(top_joint[1] - span * f) for f in (0.25, 0.5, 0.75)]
    parent_joint = by_name['bone_4']
    for k, p in enumerate(tail_chain):
        local = mat_mul(mat_inverse(global_bind[parent_joint]), translation(p))
        nodes.append({'name': f'Tail_{k + 3}', 'matrix': local})
        node = len(nodes) - 1
        nodes[joints[parent_joint]].setdefault('children', []).append(node)
        joints.append(node)
        names.append(f'Tail_{k + 3}')
        global_bind.append(translation(p))
        bind_position.append(p)
        parent_joint = len(joints) - 1
    # Сүүлний гинж (дээрээс доош): Tail_1 (bone_3) -> Tail_2 (bone_4) -> Tail_3..5; доод үзүүр
    tail_bones = [by_name['bone_3'], by_name['bone_4']] + list(range(len(joints) - 3, len(joints)))
    tail_points = [bind_position[b] for b in tail_bones] + [(tail_chain[-1][0], tail_bottom, tail_chain[-1][2])]
    skin['joints'] = joints
    ibm = [tuple(mat_inverse(g)) if k >= len(ibm) else ibm[k] for k, g in enumerate(global_bind)]
    skin['inverseBindMatrices'] = append_accessor(gltf, binary, [tuple(m) for m in ibm], 5126, 'MAT4')

    # --- Жин: хуучин хойд хөлийн ясны жинг шинэ хоёр хамгийн ойрын хэсэгт (зайны урвуугаар) ---
    out_joints, out_weights = [], []
    moved = 0
    for v in range(len(positions)):
        entries = {}
        old = 0.0
        for j, w in zip(vertex_joints[v], vertex_weights[v]):
            if w <= 0.0:
                continue
            if j in old_hind:
                old += w
            else:
                entries[j] = entries.get(j, 0.0) + w
        # Сүүлний доод хэсэг (Tail_1, Tail_2-ын жин): өндрөөр нь гинжний хэсгүүдэд дахин хуваарилна.
        tail_share = sum(entries.pop(j, 0.0) for j in (by_name['bone_3'], by_name['bone_4']))
        if tail_share > 0.0:
            y = positions[v][1]
            segment = len(tail_bones) - 1
            for k in range(len(tail_bones)):
                if y >= tail_points[k + 1][1]:
                    segment = k
                    break
            top, bottom = tail_points[segment][1], tail_points[segment + 1][1]
            t = max(0.0, min(1.0, (top - y) / max(1e-6, top - bottom)))
            # Үений орчимд (хэсгийн дээд 35%) дээд хэсэгтэй зөөлөн холилдоно.
            blend = min(1.0, 0.5 + t / 0.7) if segment > 0 else 1.0
            entries[tail_bones[segment]] = entries.get(tail_bones[segment], 0.0) + tail_share * blend
            if blend < 1.0:
                entries[tail_bones[segment - 1]] = entries.get(tail_bones[segment - 1], 0.0) + tail_share * (1.0 - blend)
        if old > 0.0:
            moved += 1
            bones, points = new_bones[side_of(v)]
            distances = [(segment_distance(positions[v], points[k], points[k + 1]), bones[k]) for k in range(4)]
            distances.sort()
            near = distances[:2]
            inverse = [1.0 / (d ** 4 + 1e-6) for d, _ in near]
            total = sum(inverse)
            for (d, bone), share in zip(near, inverse):
                entries[bone] = entries.get(bone, 0.0) + old * share / total
        top = sorted(entries.items(), key=lambda e: -e[1])[:4]
        total = sum(w for _, w in top) or 1.0
        top = [(j, w / total) for j, w in top] + [(0, 0.0)] * (4 - len(top))
        out_joints.append(tuple(j for j, _ in top))
        out_weights.append(tuple(w for _, w in top))
    attributes['JOINTS_0'] = append_accessor(gltf, binary, out_joints, 5121, 'VEC4', 34962)
    attributes['WEIGHTS_0'] = append_accessor(gltf, binary, out_weights, 5126, 'VEC4', 34962)

    for node in nodes:
        if node.get('name') in RENAME:
            node['name'] = RENAME[node['name']]

    if gltf.get('images'):
        # Текстуртай загвар: UV хэвээр, текстурыг PNG болгож mesh-ээс салгана.
        export_texture(gltf, binary, PALETTE)
        for key in ('images', 'textures', 'samplers'):
            gltf.pop(key, None)
        gltf['materials'] = [{'name': 'horse', 'doubleSided': True}]
        binary = compact(gltf, binary)
        write_glb(TARGET, gltf, binary)
        print(f'horse (textured): {len(positions)} vertices, {moved} re-weighted to the new hind legs')
        for side, prefix in ((1, 'HL'), (-1, 'HR')):
            bones, points = new_bones[side]
            print(prefix, ' -> '.join(f'({p[0]:+.3f},{p[1]:+.3f},{p[2]:+.3f})' for p in points))
        print('wrote', os.path.relpath(TARGET, ROOT), 'and', os.path.relpath(PALETTE, ROOT), f'from {os.path.basename(SOURCE)}')
        return

    # --- Өнгө (палитрын UV): туурай, сүүл, дэл, хамар, бусад нь зүс ---
    def dominant(v):
        return names[max(zip(out_weights[v], out_joints[v]))[1]]

    uvs = []
    counts = [0, 0, 0, 0]
    for v in range(len(positions)):
        x, y, z = positions[v]
        bone = dominant(v)
        color = COAT
        if y < 0.04 and (bone in LEGS or bone.startswith(('HL_', 'HR_'))):
            color = HOOF
        elif bone in TAIL or (bone == 'Pelvis' and x > 0.33):
            color = MANE
        elif bone in NECK and abs(z) < 0.028 and y > 0.55:
            color = MANE   # дэл: хүзүүний орой
        elif bone == 'tripo::Head_2' and x < -0.43:
            color = MUZZLE
        counts[color] += 1
        uvs.append(((color + 0.5) / 4.0, 0.5))
    attributes['TEXCOORD_0'] = append_accessor(gltf, binary, uvs, 5126, 'VEC2', 34962)

    gltf['materials'][0]['pbrMetallicRoughness']['baseColorFactor'] = [1, 1, 1, 1]
    binary = compact(gltf, binary)
    write_glb(TARGET, gltf, binary)
    write_png(PALETTE, COLORS, 4, 1)

    print(f'horse: {len(positions)} vertices, {moved} re-weighted to the new hind legs, colors {counts}')
    for side, prefix in ((1, 'HL'), (-1, 'HR')):
        bones, points = new_bones[side]
        print(prefix, ' -> '.join(f'({p[0]:+.3f},{p[1]:+.3f},{p[2]:+.3f})' for p in points))
    print('wrote', os.path.relpath(TARGET, ROOT), 'and', os.path.relpath(PALETTE, ROOT))


if __name__ == '__main__':
    main()
