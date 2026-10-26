# Ясгүй (эсвэл ясны өгөгдөл нь эвдэрсэн) загварт загвар дүрийн (template, жишээ нь model-1) Mixamo ясыг суулгана.
# Үе бүрийн байрлалыг шинэ биеийн хэлбэрээс хэмжинэ: хөл, нурууны үе тухайн өндрийн хөндлөн огтлолын төвд,
# мөр биеийн гадна ирмэгээс, бугуй доош унжсан гарын хамгийн гадна цэгээс. Гараа доош буулгасан эсвэл
# дэлгэсэн аль ч байрлалд ажиллана. Загвар +Z зүг рүү харсан, Y дээшээ, хөл нь 0 өндөрт байх ёстой.
# Дараа нь tools/auto_skin.py-ээр арьсны жинг тооцоолно (энэ скрипт өөрөө дуудна).
#
# Хэрэглээ:  python tools/fit_skeleton.py assets/iron_mesh.glb [assets/model-1_mesh.glb]
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import auto_skin  # noqa: E402
from auto_skin import mat_inverse, read_glb, write_glb  # noqa: E402


def positions(gltf, binary):
    prim = gltf['meshes'][0]['primitives'][0]
    accessor = gltf['accessors'][prim['attributes']['POSITION']]
    view = gltf['bufferViews'][accessor['bufferView']]
    offset = view.get('byteOffset', 0) + accessor.get('byteOffset', 0)
    stride = view.get('byteStride', 12)
    return [struct.unpack_from('<3f', binary, offset + i * stride) for i in range(accessor['count'])]


def template_skeleton(path):
    gltf, binary = read_glb(path)
    skin = gltf['skins'][0]
    joints = skin['joints']
    names = [gltf['nodes'][n]['name'].split(':')[-1] for n in joints]
    accessor = gltf['accessors'][skin['inverseBindMatrices']]
    view = gltf['bufferViews'][accessor['bufferView']]
    offset = view.get('byteOffset', 0) + accessor.get('byteOffset', 0)
    world = []
    for k in range(len(joints)):
        m = mat_inverse(list(struct.unpack_from('<16f', binary, offset + k * 64)))
        world.append((m[12], m[13], m[14]))
    parent = [-1] * len(joints)
    slot = {node: k for k, node in enumerate(joints)}
    for k, node in enumerate(joints):
        for child in gltf['nodes'][node].get('children', []):
            if child in slot:
                parent[slot[child]] = k
    height = max(p[1] for p in positions(gltf, binary))
    return names, world, parent, height


def mean(points, axis):
    return sum(p[axis] for p in points) / len(points) if points else 0.0


def main(path, template_path):
    names, tworld, parents, theight = template_skeleton(template_path)
    tpos = dict(zip(names, tworld))
    gltf, binary = read_glb(path)
    # Эвдэрсэн хуучин яс (байвал): хасаад шинээр суулгана
    if gltf.get('skins'):
        gltf.pop('skins')
        for node in gltf['nodes']:
            node.pop('skin', None)
            node.pop('children', None)  # хуучин ясны мод: дүрсний зангилаа хоёр эцэгтэй болохгүй
        gltf['scenes'][gltf.get('scene', 0)]['nodes'] = [i for i, n in enumerate(gltf['nodes']) if 'mesh' in n]
        for attribute in ('JOINTS_0', 'WEIGHTS_0'):
            gltf['meshes'][0]['primitives'][0]['attributes'].pop(attribute, None)
    verts = positions(gltf, binary)
    height = max(p[1] for p in verts)
    scale = height / theight

    def frac(name):
        return tpos[name][1] / theight * height

    def band(y, side, width=0.012):
        w = width * height
        return [p for p in verts if abs(p[1] - y) < w and (side == 0 or p[0] * side > 0.004 * height)]

    pos = {}
    # Нуруу, толгой: огтлолын төв (x = 0)
    for name in ('Hips', 'Spine', 'Spine1', 'Spine2', 'Neck', 'Head'):
        y = frac(name)
        slab = band(y, 0)
        pos[name] = (0.0, y, mean(slab, 2))
    pos['HeadTop_End'] = (0.0, height * 0.995, pos['Head'][2])

    for side, prefix in ((1, 'Left'), (-1, 'Right')):
        # Хөл: гуя, өвдөг, шагай тухайн талын огтлолын төвд
        for name in ('UpLeg', 'Leg', 'Foot'):
            y = frac(prefix + name)
            slab = band(y, side)
            x = mean(slab, 0) * (0.9 if name == 'UpLeg' else 1.0)
            pos[prefix + name] = (x, y, mean(slab, 2))
        ankle = pos[prefix + 'Foot']
        # Өвдөг гуя-шагайн шулуун дээр (урт хувцас, нөмрөгтэй бол огтлолын төв гадагш шилжиж хөл тахийдаг байсан:
        # алхахад алцганана). Гуяны үе шагай руу бага зэрэг ойртож хөл хэт налуу биш болно.
        hip = pos[prefix + 'UpLeg']
        hip = (side * max(abs(hip[0]), abs(ankle[0]) * 0.6), hip[1], hip[2])
        pos[prefix + 'UpLeg'] = hip
        knee_y = pos[prefix + 'Leg'][1]
        t = (hip[1] - knee_y) / max(1e-6, hip[1] - ankle[1])
        pos[prefix + 'Leg'] = (hip[0] + (ankle[0] - hip[0]) * t, knee_y,
                               hip[2] + (ankle[2] - hip[2]) * t + 0.01 * height)  # өвдөг бага зэрэг урагш
        sole = [p for p in verts if p[1] < 0.03 * height and p[0] * side > 0.004 * height]
        front = max(p[2] for p in sole)
        pos[prefix + 'ToeBase'] = (mean(sole, 0), frac(prefix + 'ToeBase'), ankle[2] + 0.6 * (front - ankle[2]))
        pos[prefix + 'Toe_End'] = (mean(sole, 0), frac(prefix + 'Toe_End'), front)

        # Мөр: биеийн гадна ирмэгээс
        shoulder_y = frac(prefix + 'Arm')
        slab = band(shoulder_y, side)
        edge = max(abs(p[0]) for p in slab)
        pos[prefix + 'Arm'] = (side * edge * 0.72, shoulder_y, mean(slab, 2))
        pos[prefix + 'Shoulder'] = (side * edge * 0.3, frac(prefix + 'Shoulder'), mean(slab, 2))

        # Бугуй: гар доош унжсан бол мөрнөөс доош хамгийн гадна цэгүүд. Дэлгэсэн бол template-ийн чиглэлээр.
        t_arm, t_hand = tpos[prefix + 'Arm'], tpos[prefix + 'Hand']
        hand_y = t_hand[1] / theight * height
        lowest = shoulder_y - 0.45 * height
        reach = [p for p in verts if lowest < p[1] < shoulder_y - 0.15 * height and p[0] * side > 0]
        outer = max(abs(p[0]) for p in reach)
        tip = [p for p in reach if abs(p[0]) > outer - 0.035 * height]
        wrist_y = max(min(p[1] for p in tip) + 0.07 * height, hand_y if abs(t_hand[0]) > 0.2 * theight else 0.0)
        wrist_y = min(wrist_y, shoulder_y - 0.2 * height)
        hand_slab = [p for p in tip if abs(p[1] - wrist_y) < 0.03 * height] or tip
        wrist = (side * (outer - 0.02 * height), wrist_y, mean(hand_slab, 2))
        pos[prefix + 'Hand'] = wrist
        arm = pos[prefix + 'Arm']
        elbow_y = (arm[1] + wrist[1]) * 0.5
        elbow_slab = [p for p in band(elbow_y, side, 0.015) if abs(p[0]) > (abs(arm[0]) + abs(wrist[0])) * 0.5 - 0.03 * height]
        pos[prefix + 'ForeArm'] = (side * (abs(arm[0]) + abs(wrist[0])) * 0.5, elbow_y,
                                   mean(elbow_slab, 2) - 0.01 * height if elbow_slab else (arm[2] + wrist[2]) * 0.5)

        # Хуруу: бугуйгаас гарын үргэлжлэл (тохойноос бугуй руу) чиглэлд; долоовор урд (+Z), чигчий ард
        forearm = pos[prefix + 'ForeArm']
        d = [wrist[i] - forearm[i] for i in range(3)]
        length = max(1e-6, sum(x * x for x in d) ** 0.5)
        d = [x / length for x in d]
        for finger, dz in (('Index', 0.016), ('Middle', 0.005), ('Ring', -0.006), ('Pinky', -0.016)):
            for k in range(1, 5):
                reach_k = (0.04 + 0.018 * (k - 1)) * height
                pos[f'{prefix}Hand{finger}{k}'] = (wrist[0] + d[0] * reach_k, wrist[1] + d[1] * reach_k,
                                                   wrist[2] + d[2] * reach_k + dz * height)
        for k in range(1, 5):
            reach_k = (0.015 + 0.015 * (k - 1)) * height
            pos[f'{prefix}HandThumb{k}'] = (wrist[0] + d[0] * reach_k - side * 0.008 * height,
                                            wrist[1] + d[1] * reach_k, wrist[2] + 0.025 * height + 0.01 * height * (k - 1))

    # Үлдсэн үе: template-ийн байрлалыг өндрөөр масштаблана
    world = [pos.get(n, tuple(c * scale for c in tpos[n])) for n in names]

    # --- Яс бичих: зангилаа (зөвхөн байрлал, эргэлтгүй), inverseBindMatrices = орон зайг шилжүүлэх
    base = len(gltf['nodes'])
    for k, name in enumerate(names):
        p = parents[k]
        local = [world[k][i] - (world[p][i] if p >= 0 else 0.0) for i in range(3)]
        node = {'name': 'mixamorig:' + name, 'translation': local}
        children = [base + c for c in range(len(names)) if parents[c] == k]
        if children:
            node['children'] = children
        gltf['nodes'].append(node)
    roots = [base + k for k in range(len(names)) if parents[k] < 0]
    gltf['scenes'][gltf.get('scene', 0)]['nodes'] += roots
    mesh_node = next(i for i, n in enumerate(gltf['nodes']) if 'mesh' in n)
    gltf['nodes'][mesh_node]['skin'] = 0

    while len(binary) % 4:
        binary.append(0)
    start = len(binary)
    for p in world:
        binary.extend(struct.pack('<16f', 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -p[0], -p[1], -p[2], 1))
    gltf['bufferViews'].append({'buffer': 0, 'byteOffset': start, 'byteLength': len(binary) - start})
    gltf['accessors'].append({'bufferView': len(gltf['bufferViews']) - 1, 'componentType': 5126,
                              'count': len(names), 'type': 'MAT4'})
    gltf['skins'] = [{'name': 'skin', 'inverseBindMatrices': len(gltf['accessors']) - 1,
                      'joints': [base + k for k in range(len(names))]}]
    gltf['buffers'] = [{'byteLength': len(binary)}]
    write_glb(path, gltf, binary)
    print(f'{path}: {len(names)} joints fitted (height {height:.3f})')
    for name in ('Hips', 'Head', 'LeftArm', 'LeftForeArm', 'LeftHand', 'LeftUpLeg', 'LeftLeg', 'LeftFoot'):
        print('  %-12s %s' % (name, ' '.join('%6.3f' % c for c in pos[name])))
    auto_skin.main(path)


if __name__ == '__main__':
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, 'assets', 'model-1_mesh.glb'))
