# Ясыг загварын бие (mesh) дээр тааруулна: зарим GLB-д (жишээ нь Tripo-гийн m3) араг яс өөр координатад
# байдаг (Z дээшээ, хонго 0 цэгт, урагшаа +X), бие нь Y дээшээ. Тайван байрлалд зөв харагдах ч тоглоом мөчийг
# эргүүлэхэд эргэлтийн цэг (үе) мөчний гадна байх тул гар хөл буруу савлана.
#
# 1. Ясны зүг: зүүн тал = зүүн мөр - баруун мөр, дээш = хөл -> толгой, урд = тэдгээрийн вектор үржвэр
#    -> тоглоомын зүг рүү (зүүн +X, дээш +Y, урд +Z) эргүүлэх R.
# 2. Хэмжээ, байрлал: ясны хэсэг бүрийн дунд цэгийг тэр ясанд холбогдсон оройнуудын төвд хамгийн бага
#    квадратын аргаар тааруулна (s, t).
# 3. inverseBindMatrices-ийг T = [sR | t]-ээр шинэчилнэ (тайван байрлалд бие өөрчлөгдөхгүй).
# Дараа нь tools/fix_bind_pose.py ясны зангилааг шинэчилнэ. Зөв ястай загварт (fighter1) R ~ I, s ~ 1, t ~ 0.
#
# Хэрэглээ:  python tools/align_skeleton.py assets/model-3_mesh.glb [--dry-run]
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from auto_skin import SEGMENTS, mat_inverse, read_glb, write_glb


def mat_mul(a, b):
    return [sum(a[k * 4 + r] * b[c * 4 + k] for k in range(4)) for c in range(4) for r in range(4)]


def read_accessor(gltf, binary, index, components, kind):
    accessor = gltf['accessors'][index]
    view = gltf['bufferViews'][accessor['bufferView']]
    offset = view.get('byteOffset', 0) + accessor.get('byteOffset', 0)
    size = struct.calcsize('<' + kind)
    stride = view.get('byteStride', size * components)
    return [struct.unpack_from('<%d%s' % (components, kind), binary, offset + k * stride) for k in range(accessor['count'])]


def sub(a, b): return [a[i] - b[i] for i in range(3)]
def dot(a, b): return sum(a[i] * b[i] for i in range(3))
def norm(a):
    length = math.sqrt(dot(a, a)) or 1.0
    return [v / length for v in a]
def cross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def main(path, dry_run):
    gltf, binary = read_glb(path)
    skin = gltf['skins'][0]
    names = [gltf['nodes'][j]['name'].split(':')[-1] for j in skin['joints']]
    ibm_accessor = gltf['accessors'][skin['inverseBindMatrices']]
    ibm_view = gltf['bufferViews'][ibm_accessor['bufferView']]
    ibm_offset = ibm_view.get('byteOffset', 0) + ibm_accessor.get('byteOffset', 0)
    ibms = [list(struct.unpack_from('<16f', binary, ibm_offset + k * 64)) for k in range(ibm_accessor['count'])]
    joint = {name: mat_inverse(m)[12:15] for name, m in zip(names, ibms)}

    # 1. Ясны зүг -> тоглоомын зүг
    left = norm(sub(joint['LeftArm'], joint['RightArm']))
    feet = [(joint['LeftFoot'][i] + joint['RightFoot'][i]) / 2 for i in range(3)]
    up = sub(joint['Head'], feet)
    up = norm(sub(up, [left[i] * dot(up, left) for i in range(3)]))  # зүүн талд перпендикуляр
    forward = cross(left, up)
    # R нь ясны зүг (left, up, forward)-ийг (+X, +Y, +Z) болгоно: мөрүүд нь тэдгээр вектор
    rot = [left, up, forward]

    def apply_r(p):
        return [dot(rot[0], p), dot(rot[1], p), dot(rot[2], p)]

    # 2. Ясны хэсгийн дунд цэг ба холбогдсон оройн төв
    primitive = gltf['meshes'][0]['primitives'][0]
    positions = read_accessor(gltf, binary, primitive['attributes']['POSITION'], 3, 'f')
    joints = read_accessor(gltf, binary, primitive['attributes']['JOINTS_0'], 4,
                           {5121: 'B', 5123: 'H'}[gltf['accessors'][primitive['attributes']['JOINTS_0']]['componentType']])
    weights = read_accessor(gltf, binary, primitive['attributes']['WEIGHTS_0'], 4, 'f')
    sums = {}
    for p, js, ws in zip(positions, joints, weights):
        for j, w in zip(js, ws):
            if w <= 0.0:
                continue
            entry = sums.setdefault(names[j], [0.0, 0.0, 0.0, 0.0])
            for i in range(3):
                entry[i] += p[i] * w
            entry[3] += w
    pairs = []  # (ясны дунд цэг R-ээр эргүүлсэн, оройн төв, жин)
    for bone, child in SEGMENTS:
        if child is None or bone not in joint or child not in joint or bone not in sums or sums[bone][3] < 5.0:
            continue
        middle = [(joint[bone][i] + joint[child][i]) / 2 for i in range(3)]
        center = [sums[bone][i] / sums[bone][3] for i in range(3)]
        pairs.append((apply_r(middle), center, sums[bone][3]))
    if len(pairs) < 6:
        sys.exit('%s: too few weighted bones to align (%d); run auto_skin.py first?' % (path, len(pairs)))
    total = sum(w for _, _, w in pairs)
    qm = [sum(q[i] * w for q, _, w in pairs) / total for i in range(3)]
    cm = [sum(c[i] * w for _, c, w in pairs) / total for i in range(3)]
    s = sum(w * dot(sub(q, qm), sub(c, cm)) for q, c, w in pairs) / sum(w * dot(sub(q, qm), sub(q, qm)) for q, _, w in pairs)
    t = [cm[i] - s * qm[i] for i in range(3)]
    residual = math.sqrt(sum(w * sum((s * q[i] + t[i] - c[i]) ** 2 for i in range(3)) for q, c, w in pairs) / total)

    # T (баганаар): T * p = s * R * p + t
    T = [s * rot[0][0], s * rot[1][0], s * rot[2][0], 0,
         s * rot[0][1], s * rot[1][1], s * rot[2][1], 0,
         s * rot[0][2], s * rot[1][2], s * rot[2][2], 0,
         t[0], t[1], t[2], 1]
    angle = math.degrees(math.acos(max(-1.0, min(1.0, (rot[0][0] + rot[1][1] + rot[2][2] - 1) / 2))))
    head = [s * v + t[i] for i, v in enumerate(apply_r(joint['Head']))]
    foot = [s * v + t[i] for i, v in enumerate(apply_r(feet))]
    print('%s: rotation %.0f deg, scale %.3f, offset (%.3f %.3f %.3f), fit error %.3f, head y %.3f, feet y %.3f'
          % (path, angle, s, t[0], t[1], t[2], residual, head[1], foot[1]))
    if dry_run:
        return

    # 3. Шинэ inverseBindMatrices = IBM * T^-1 (шинэ ясны байрлал = T * хуучин)
    T_inv = mat_inverse(T)
    matrices = [mat_mul(m, T_inv) for m in ibms]
    while len(binary) % 4:
        binary.append(0)
    start = len(binary)
    for m in matrices:
        binary.extend(struct.pack('<16f', *m))
    gltf['bufferViews'].append({'buffer': 0, 'byteOffset': start, 'byteLength': len(binary) - start})
    gltf['accessors'].append({'bufferView': len(gltf['bufferViews']) - 1, 'componentType': 5126,
                              'count': len(matrices), 'type': 'MAT4'})
    skin['inverseBindMatrices'] = len(gltf['accessors']) - 1
    gltf['buffers'] = [{'byteLength': len(binary)}]
    write_glb(path, gltf, binary)
    print('%s: skeleton aligned to the mesh' % path)


if __name__ == '__main__':
    main(sys.argv[1], '--dry-run' in sys.argv)
