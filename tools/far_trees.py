# Холын модны хөнгөн хувилбар: Tripo модны маш их хялбаршуулсан загварыг (vehicleprep prop, VPREP_MODE=1) текстурын
# өнгөөр будаж (гурвалжин бүрд нэг өнгө) жижиг файл болгоно. Тоглоом үүнийг ертөнцийн хөдөлгөөнгүй геометрт (оройн
# өнгөтэй, текстургүй) мод бүрээр нь хуулж зурна (Structures.cpp: FarTree). Ойроос (World: kSceneryNear) бүрэн загвар.
#
# Хэрэглээ (өндөр нь 1 м болгосон загвараас):
#   VPREP_MODE=1 VPREP_HEIGHT=1 vehicleprep prop tmp/larch tmp/larch_far.vmesh 60 1
#   python tools/far_trees.py tmp/larch_far.vmesh assets/trees/larch_color.png assets/trees/larch_far.bin
#
# Файл (little-endian): "TFAR" u32 version=1, u32 triangleCount, гурвалжин бүр: f32 xyz[3], u8 rgba
import struct
import sys

from PIL import Image


def read_vmesh(path):
    data = open(path, 'rb').read()
    assert data[:4] == b'VMSH'
    parts = struct.unpack_from('<I', data, 8)[0]
    at = 12
    triangles = []
    for _ in range(parts):
        pivot = struct.unpack_from('<3f', data, at)
        at += 12
        _kind, vc, ic = struct.unpack_from('<3I', data, at)
        at += 12
        pos = struct.unpack_from('<%df' % (vc * 3), data, at)
        at += vc * 12
        at += vc * 12  # normals
        uv = struct.unpack_from('<%df' % (vc * 2), data, at)
        at += vc * 8
        idx = struct.unpack_from('<%dH' % ic, data, at)
        at += ic * 2
        for t in range(0, ic, 3):
            corners = []
            for k in range(3):
                i = idx[t + k]
                corners.append(((pos[i * 3] + pivot[0], pos[i * 3 + 1] + pivot[1], pos[i * 3 + 2] + pivot[2]),
                                (uv[i * 2], uv[i * 2 + 1])))
            triangles.append(corners)
    return triangles


def main(mesh_path, texture_path, out_path):
    texture = Image.open(texture_path).convert('RGB')
    w, h = texture.size
    pixels = texture.load()

    def sample(u, v):
        return pixels[min(w - 1, max(0, int(u * w))), min(h - 1, max(0, int(v * h)))]

    out = bytearray(b'TFAR')
    triangles = read_vmesh(mesh_path)
    out += struct.pack('<II', 1, len(triangles))
    for corners in triangles:
        # Өнгө: гурван орой ба төвийн дундаж (хялбаршуулсан гурвалжин олон навч, мөчрийг хамардаг)
        uvs = [c[1] for c in corners]
        uvs.append((sum(u for u, _ in uvs) / 3.0, sum(v for _, v in uvs) / 3.0))
        colors = [sample(u, v) for u, v in uvs]
        rgb = [int(sum(c[k] for c in colors) / len(colors)) for k in range(3)]
        for position, _ in corners:
            out += struct.pack('<3f', *position)
        out += struct.pack('<4B', rgb[0], rgb[1], rgb[2], 255)
    open(out_path, 'wb').write(out)
    print(f'{out_path}: {len(triangles)} triangles')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3])
