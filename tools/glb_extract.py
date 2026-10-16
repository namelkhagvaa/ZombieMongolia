# Tripo3D-ийн GLB загвараас (нэг mesh, нэг материал) түүхий өгөгдлийг гаргана:
#   <out>.pos (float32 xyz), <out>.uv (float32 uv), <out>.idx (uint32), <out>_color.jpg (үндсэн өнгөний зураг)
# Хэрэглээ: python tools/glb_extract.py assets/cars/G-class.glb build_vehicle/gclass
import json, struct, sys, array

source, out = sys.argv[1], sys.argv[2]
data = open(source, 'rb').read()
json_length = struct.unpack('<I', data[12:16])[0]
gltf = json.loads(data[20:20 + json_length])
bin_start = 20 + json_length + 8

def view_bytes(view_index):
    view = gltf['bufferViews'][view_index]
    start = bin_start + view.get('byteOffset', 0)
    return data[start:start + view['byteLength']], view.get('byteStride')

def accessor(index, components):
    acc = gltf['accessors'][index]
    raw, stride = view_bytes(acc['bufferView'])
    offset = acc.get('byteOffset', 0)
    count = acc['count']
    kind = {5126: 'f', 5125: 'I', 5123: 'H', 5121: 'B'}[acc['componentType']]
    size = struct.calcsize(kind)
    stride = stride or size * components
    values = array.array(kind)
    if stride == size * components:
        values.frombytes(raw[offset:offset + count * stride])
    else:
        for i in range(count):
            values.frombytes(raw[offset + i * stride:offset + i * stride + size * components])
    return values

primitive = gltf['meshes'][0]['primitives'][0]
positions = accessor(primitive['attributes']['POSITION'], 3)
uvs = accessor(primitive['attributes']['TEXCOORD_0'], 2)
indices = array.array('I', accessor(primitive['indices'], 1))
open(out + '.pos', 'wb').write(positions.tobytes())
open(out + '.uv', 'wb').write(uvs.tobytes())
open(out + '.idx', 'wb').write(indices.tobytes())

material = gltf['materials'][primitive.get('material', 0)]
texture = gltf['textures'][material['pbrMetallicRoughness']['baseColorTexture']['index']]
image = gltf['images'][texture['source']]
raw, _ = view_bytes(image['bufferView'])
ext = '.png' if image.get('mimeType') == 'image/png' else '.jpg'
open(out + '_color' + ext, 'wb').write(raw)
print('vertices', len(positions) // 3, 'triangles', len(indices) // 3, 'image', ext, len(raw))
