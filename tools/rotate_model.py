# Загварын ясыг (--mesh: бүх загварыг) босоо тэнхлэгээр 90 градусаар эргүүлж тоглоомын бусад загвартай (зүүн тал +X, урагш +Z) тааруулна.
# Зарим загвар (жишээ нь Tripo-гийн giant_zombie) урагшаа +X, зүүн тал нь -Z байдаг: тоглоом хөлийг урагш хойш
# савлуулахад хөл нь хажуу тийш савлана. Оройн байрлал, normal, ясны inverseBindMatrices-ийг эргүүлнэ.
# Дараа нь tools/fix_bind_pose.py ясны зангилааг шинэчилнэ.
#
# Хэрэглээ:  python tools/rotate_model.py assets/giant_zombie_mesh.glb
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from auto_skin import mat_inverse, read_glb, write_glb


def rotate(v):
    # (x, y, z) -> (-z, y, x): урагш +X -> +Z, зүүн тал -Z -> +X
    return (-v[2], v[1], v[0])


# Дээрх эргэлтийн 4x4 матриц (баганаар, glTF)
R = [0, 0, 1, 0,
     0, 1, 0, 0,
     -1, 0, 0, 0,
     0, 0, 0, 1]


def mat_mul(a, b):
    return [sum(a[k * 4 + r] * b[c * 4 + k] for k in range(4)) for c in range(4) for r in range(4)]


def rewrite_vec3(gltf, binary, index):
    accessor = gltf['accessors'][index]
    view = gltf['bufferViews'][accessor['bufferView']]
    offset = view.get('byteOffset', 0) + accessor.get('byteOffset', 0)
    stride = view.get('byteStride', 12)
    values = []
    for k in range(accessor['count']):
        values.append(rotate(struct.unpack_from('<3f', binary, offset + k * stride)))
    while len(binary) % 4:
        binary.append(0)
    start = len(binary)
    for v in values:
        binary.extend(struct.pack('<3f', *v))
    gltf['bufferViews'].append({'buffer': 0, 'byteOffset': start, 'byteLength': len(binary) - start, 'target': 34962})
    new = {'bufferView': len(gltf['bufferViews']) - 1, 'componentType': 5126, 'count': len(values), 'type': 'VEC3'}
    if 'min' in accessor:
        new['min'] = [min(v[i] for v in values) for i in range(3)]
        new['max'] = [max(v[i] for v in values) for i in range(3)]
    gltf['accessors'].append(new)
    return len(gltf['accessors']) - 1


def main(path):
    gltf, binary = read_glb(path)
    # Загварын бие (mesh) өөрөө зөв зүгтэй, зөвхөн яс нь 90 градус эргэсэн байдаг: ясыг л эргүүлнэ.
    for mesh in (gltf['meshes'] if '--mesh' in sys.argv else []):
        for primitive in mesh['primitives']:
            attributes = primitive['attributes']
            for key in ('POSITION', 'NORMAL'):
                if key in attributes:
                    attributes[key] = rewrite_vec3(gltf, binary, attributes[key])

    skin = gltf['skins'][0]
    accessor = gltf['accessors'][skin['inverseBindMatrices']]
    view = gltf['bufferViews'][accessor['bufferView']]
    offset = view.get('byteOffset', 0) + accessor.get('byteOffset', 0)
    matrices = []
    for k in range(accessor['count']):
        world = mat_inverse(list(struct.unpack_from('<16f', binary, offset + k * 64)))
        matrices.append(mat_inverse(mat_mul(R, world)))
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
    print(f'{path}: skeleton rotated 90 degrees (forward +X -> +Z)')


if __name__ == '__main__':
    main(sys.argv[1])
