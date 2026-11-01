# "Realistic FPS Hands" (Unity Asset Store, 3ds Max CAT rig) -> 1-р этгээдийн гар: assets/fp_arms_mesh.glb + fp_arms_color.png
#
#   python tools/fbx_hands.py <FPS Hands.FBX> <AlbedoTransparency.png> [texture px, анхдагч 1024]
#
# unitypackage (энгийн tar) дотроос FBX ба өнгөний зургийг гаргаж авна (500 МБ-ын ихэнх нь 8K текстур: тоглоомд
# зөвхөн загвар (~1 МБ) ба жижигрүүлсэн өнгөний зураг орно).
# CAT ясыг Mixamo нэрээр (CharacterModel.cpp FindBone): Hub -> Spine2, Collarbone -> Shoulder, Arm1 -> Arm,
# Arm21 -> ForeArm, Arm22 -> ForeArmTwist, Palm -> Hand, Digit1..5 -> Thumb, Index, Middle, Ring, Pinky.
# Бие, хөлгүй тул Hips, Spine, Neck, хөлийн ясыг жингүйгээр нэмнэ (CharacterModel бүтэн хүний ясыг шаарддаг).
# Ясны анхны байрлал эргэлтгүй (зөвхөн байрлал): тоглоом ясны байрлалаас л тооцдог.
import json, os, struct, sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fbx_reader

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
FINGERS = {'1': 'Thumb', '2': 'Index', '3': 'Middle', '4': 'Ring', '5': 'Pinky'}


def mixamo_name(cat):
    if cat == 'CATRigHub001': return 'Spine2'
    for tag, side in (('CATRigRArm', 'Right'), ('CATRigLArm', 'Left')):
        if not cat.startswith(tag): continue
        rest = cat[len(tag):]
        if rest == 'Collarbone': return side + 'Shoulder'
        if rest == '1': return side + 'Arm'
        if rest == '21': return side + 'ForeArm'
        if rest == '22': return side + 'ForeArmTwist'
        if rest == 'Palm': return side + 'Hand'
        if rest.startswith('Digit'): return side + 'Hand' + FINGERS[rest[5]] + rest[6]
    return None


def main():
    fbx_path, albedo_path = sys.argv[1], sys.argv[2]
    tex_size = int(sys.argv[3]) if len(sys.argv) > 3 else 1024
    root, _ = fbx_reader.load(fbx_path)
    objs = root.first('Objects')
    names = {o.props[0]: (o.name, o.props[1].split('\x00')[0], o) for o in objs.children}
    conns = [(c.props[1], c.props[2]) for c in root.first('Connections').children]
    mat = lambda a: np.array(a, dtype=float).reshape(4, 4).T  # FBX: баганан вектор, байрлал 12..14

    pose = [o for o in objs.children if o.name == 'Pose'][0]
    G = {names[pn.first('Node').props[0]][1]: mat(pn.first('Matrix').props[0]) for pn in pose.find('PoseNode')}
    parent = {}
    for c, p in conns:
        if names.get(c, ('',))[0] == 'Model' and names.get(p, ('',))[0] == 'Model': parent[names[c][1]] = names[p][1]

    # --- Яс: CAT -> Mixamo, байрлал (тоглоомын орон зай: Y дээш, +Z урд, зүүн гар +X) ---
    pos = {}
    for cat, m in G.items():
        n = mixamo_name(cat)
        if n: pos[n] = m[:3, 3].copy()
    par = {}
    for cat in G:
        n = mixamo_name(cat)
        if n and n != 'Spine2': par[n] = mixamo_name(parent[cat])
    hub = pos['Spine2']
    unit = np.linalg.norm(pos['RightArm'] - pos['RightShoulder']) / 0.15 * 0.1  # ~10 см
    extra = {  # (нэр, эцэг, байрлал): жингүй, бүтэн хүний ясны бүтцэд зориулсан
        'Hips': (None, hub + [0, -5.0 * unit, 0]),
        'Spine': ('Hips', hub + [0, -3.6 * unit, 0]),
        'Spine1': ('Spine', hub + [0, -1.8 * unit, 0]),
        'Neck': ('Spine2', hub + [0, 1.6 * unit, 0]),
        'Head': ('Neck', hub + [0, 2.6 * unit, 0]),
        # Тэмдэг: энэ загварын алга анхны байрлалдаа гадагш харсан (CharacterModel.cpp SetupHands эсрэг талыг авна;
        # эс бөгөөс хуруу бариулыг ороохын оронд гадагш нугардаг)
        'PalmsOut': ('Hips', hub + [0, -5.0 * unit, 0]),
    }
    for side, sx in (('Right', -1), ('Left', 1)):
        hips = hub + [0, -5.0 * unit, 0]
        extra[side + 'UpLeg'] = ('Hips', hips + [sx * 1.0 * unit, -0.5 * unit, 0])
        extra[side + 'Leg'] = (side + 'UpLeg', hips + [sx * 1.0 * unit, -4.5 * unit, 0])
        extra[side + 'Foot'] = (side + 'Leg', hips + [sx * 1.0 * unit, -8.5 * unit, 0])
        extra[side + 'ToeBase'] = (side + 'Foot', hips + [sx * 1.0 * unit, -9.0 * unit, 1.2 * unit])
    for n, (p, x) in extra.items():
        pos[n] = np.array(x, dtype=float)
        par[n] = p
    par['Spine2'] = 'Spine1'
    # Эцэг нь өмнө орох дараалал
    order = []
    def visit(n):
        if n in order: return
        if par[n]: visit(par[n])
        order.append(n)
    for n in sorted(pos): visit(n)
    index = {n: i for i, n in enumerate(order)}

    # --- Гарын mesh (бугуйн цаг орхино) ---
    hands = [o for _, (kind, name, o) in names.items() if kind == 'Model' and name == 'FPS Hands'][0]
    geo = [names[c][2] for c, p in conns if p == hands.props[0] and names.get(c, ('',))[0] == 'Geometry'][0]
    M = G['FPS Hands']
    ctrl = np.array(geo.first('Vertices').props[0], dtype=float).reshape(-1, 3)
    world = (M[:3, :3] @ ctrl.T).T + M[:3, 3]
    pvi = geo.first('PolygonVertexIndex').props[0]
    uvl = geo.first('LayerElementUV')
    uvs = np.array(uvl.first('UV').props[0], dtype=float).reshape(-1, 2)
    uvi = uvl.first('UVIndex').props[0]

    # Арьсны жин: control point бүрд
    skin = [names[c][2] for c, p in conns if p == geo.props[0] and names.get(c, ('',))[0] == 'Deformer']
    skin = [s for s in skin if s.props[2] == 'Skin'][0]
    weights = [[] for _ in range(len(ctrl))]
    for c, p in conns:
        if p != skin.props[0]: continue
        cluster = names[c][2]
        idx = cluster.first('Indexes')
        if idx is None: continue
        bone = [names[b][1] for b, q in conns if q == c and names.get(b, ('',))[0] == 'Model'][0]
        n = mixamo_name(bone)
        for v, w in zip(idx.props[0], cluster.first('Weights').props[0]):
            if w > 0: weights[v].append((w, index[n]))
    joints4 = np.zeros((len(ctrl), 4), dtype=np.uint8)
    weights4 = np.zeros((len(ctrl), 4), dtype=np.float32)
    for v, ws in enumerate(weights):
        ws = sorted(ws, reverse=True)[:4]
        total = sum(w for w, _ in ws) or 1.0
        for k, (w, j) in enumerate(ws):
            joints4[v, k] = j
            weights4[v, k] = w / total
        if not ws: joints4[v, 0], weights4[v, 0] = index['Spine2'], 1.0

    # Олон өнцөгтийг гурвалжин болгоно; орой = (control point, uv)
    verts, key_index, tris, poly = [], {}, [], []
    for corner, raw in enumerate(pvi):
        cp = raw if raw >= 0 else ~raw
        key = (cp, uvi[corner])
        if key not in key_index:
            key_index[key] = len(verts)
            verts.append(key)
        poly.append(key_index[key])
        if raw < 0:
            for k in range(1, len(poly) - 1): tris.append((poly[0], poly[k], poly[k + 1]))
            poly = []
    cps = np.array([k[0] for k in verts])
    P = world[cps].astype(np.float32)
    UV = uvs[[k[1] for k in verts]].astype(np.float32)
    UV[:, 1] = 1.0 - UV[:, 1]  # FBX: v дээш; glTF: v доош
    J = joints4[cps]
    W = weights4[cps]
    I = np.array(tris, dtype=np.uint32).reshape(-1)
    # Normal: гурвалжны normal-уудыг нийлүүлнэ (байрлалаар нь: uv-ийн заагт ч гөлгөр)
    N = np.zeros_like(world)
    t = np.array(tris).reshape(-1, 3)
    fn = np.cross(world[cps[t[:, 1]]] - world[cps[t[:, 0]]], world[cps[t[:, 2]]] - world[cps[t[:, 0]]])
    for k in range(3): np.add.at(N, cps[t[:, k]], fn)
    N = N[cps]
    N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-12)
    N = N.astype(np.float32)

    # --- GLB ---
    blobs, views, accessors = [], [], []
    def add(arr, comp, kind, target=None, minmax=False):
        data = np.ascontiguousarray(arr).tobytes()
        offset = sum(len(b) for b in blobs)
        blobs.append(data + b'\0' * ((4 - len(data) % 4) % 4))
        view = {'buffer': 0, 'byteOffset': offset, 'byteLength': len(data)}
        if target: view['target'] = target
        views.append(view)
        a = {'bufferView': len(views) - 1, 'componentType': comp, 'count': int(arr.shape[0]), 'type': kind}
        if minmax: a['min'], a['max'] = arr.min(0).tolist(), arr.max(0).tolist()
        accessors.append(a)
        return len(accessors) - 1
    a_pos = add(P, 5126, 'VEC3', 34962, True)
    a_nrm = add(N, 5126, 'VEC3', 34962)
    a_uv = add(UV, 5126, 'VEC2', 34962)
    a_j = add(J, 5121, 'VEC4', 34962)
    a_w = add(W, 5126, 'VEC4', 34962)
    a_i = add(I, 5125, 'SCALAR', 34963)
    ibm = np.zeros((len(order), 16), dtype=np.float32)
    for i, n in enumerate(order):
        m = np.eye(4); m[:3, 3] = -pos[n]
        ibm[i] = m.T.reshape(-1)  # glTF: багана дараалал
    a_ibm = add(ibm, 5126, 'MAT4')
    nodes = []
    for n in order:
        local = pos[n] - (pos[par[n]] if par[n] else 0)
        nodes.append({'name': n, 'translation': [float(x) for x in local],
                      'children': [index[c] for c in order if par[c] == n]})
    for nd in nodes:
        if not nd['children']: del nd['children']
    nodes.append({'name': 'body', 'mesh': 0, 'skin': 0})
    gltf = {
        'asset': {'version': '2.0', 'generator': 'tools/fbx_hands.py'},
        'scene': 0, 'scenes': [{'nodes': [index['Hips'], len(nodes) - 1]}],
        'nodes': nodes,
        'meshes': [{'primitives': [{'attributes': {'POSITION': a_pos, 'NORMAL': a_nrm, 'TEXCOORD_0': a_uv,
                                                   'JOINTS_0': a_j, 'WEIGHTS_0': a_w}, 'indices': a_i, 'material': 0}]}],
        'materials': [{'name': 'character', 'doubleSided': True}],
        'skins': [{'joints': list(range(len(order))), 'inverseBindMatrices': a_ibm, 'skeleton': index['Hips']}],
        'accessors': accessors, 'bufferViews': views, 'buffers': [{'byteLength': sum(len(b) for b in blobs)}],
    }
    js = json.dumps(gltf, separators=(',', ':')).encode()
    js += b' ' * ((4 - len(js) % 4) % 4)
    bin_ = b''.join(blobs)
    out = os.path.join(ROOT, 'assets', 'fp_arms_mesh.glb')
    with open(out, 'wb') as f:
        f.write(struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(bin_)))
        f.write(struct.pack('<I', len(js)) + b'JSON' + js)
        f.write(struct.pack('<I', len(bin_)) + b'BIN\0' + bin_)
    print(out, len(P), 'vertices', len(I) // 3, 'triangles', len(order), 'bones')

    image = Image.open(albedo_path).convert('RGB').resize((tex_size, tex_size), Image.LANCZOS)
    color = os.path.join(ROOT, 'assets', 'fp_arms_color.png')
    image.save(color, optimize=True)
    print(color, os.path.getsize(color), 'bytes')


main()
