# Зарим Tripo загварын араг яс (inverseBindMatrices) нь mesh-ээсээ босоо тэнхлэгээр эргэсэн байдаг: жишээ нь зүүн
# гарын яс биеийн урд (-Z), харин гарын mesh хажуу талд (+X). Хөдөлгөөнгүй үед харагдахгүй ч тоглоом ясыг
# хөдөлгөхөд бие гажина. Энэ скрипт баруун -> зүүн мөрний чиглэлээс (+X байх ёстой) эргэлтийг олж, араг ясыг
# бүхэлд нь (inverseBindMatrices ба зангилааны локал хувиргалт) mesh-тэй таарахаар эргүүлнэ. Mesh өөрчлөгдөхгүй.
#
# Хэрэглээ:  python tools/fix_rig_rotation.py assets/survivor_doctor_woman_mesh.glb
# (зангилаандаа хувиргалтгүй загварт эхлээд tools/fix_bind_pose.py-г ажиллуулна)
import math
import struct
import sys

from fix_bind_pose import mat_inverse, mat_mul, read_glb, to_quaternion, write_glb


def rotation_y(angle):
    c, s = math.cos(angle), math.sin(angle)
    # баганаар (glTF): x' = x c + z s, z' = -x s + z c
    return [c, 0.0, -s, 0.0, 0.0, 1.0, 0.0, 0.0, s, 0.0, c, 0.0, 0.0, 0.0, 0.0, 1.0]


def main(path):
    gltf, binary = read_glb(path)
    skin = gltf['skins'][0]
    accessor = gltf['accessors'][skin['inverseBindMatrices']]
    view = gltf['bufferViews'][accessor['bufferView']]
    offset = view.get('byteOffset', 0) + accessor.get('byteOffset', 0)
    joints = skin['joints']
    names = {node: gltf['nodes'][node].get('name', '').split(':')[-1] for node in joints}
    world = {}
    for k, node in enumerate(joints):
        world[node] = mat_inverse(list(struct.unpack_from('<16f', binary, offset + k * 64)))

    position = {names[node]: (world[node][12], world[node][14]) for node in joints}
    dx = position['LeftArm'][0] - position['RightArm'][0]
    dz = position['LeftArm'][1] - position['RightArm'][1]
    angle = math.atan2(dz, dx)
    if abs(angle) < math.radians(10.0):
        print(f'{path}: skeleton already matches the mesh ({math.degrees(angle):.1f} degrees)')
        return
    turn = rotation_y(angle)
    for node in joints:
        world[node] = mat_mul(turn, world[node])

    parent = {}
    for i, node in enumerate(gltf['nodes']):
        for child in node.get('children', []):
            parent[child] = i
    for k, node in enumerate(joints):
        struct.pack_into('<16f', binary, offset + k * 64, *mat_inverse(world[node]))
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
    write_glb(path, gltf, binary)
    print(f'{path}: skeleton turned {math.degrees(angle):.1f} degrees to match the mesh')


if __name__ == '__main__':
    main(sys.argv[1])
