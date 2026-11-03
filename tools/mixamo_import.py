# Mixamo-гоор ясжуулсан дүр + Mixamo-гийн хөдөлгөөнүүд -> тоглоомын файлууд:
#   assets/<name>_mesh.glb   - mesh + яс (зөвхөн байрлалтай: CharacterModel ясны байрлалаас л тооцдог)
#   assets/<name>_color.png  - өнгөний текстур
#   assets/soldier_anims.bin - хөдөлгөөнүүд (src/AnimClips.cpp уншина; цэргүүд хуваалцана, CharacterModel.cpp SetupClips)
#
#   python tools/mixamo_import.py man <ясжуулсан.fbx> <өнгөтэй.obj> <basecolor.jpg> <хөдөлгөөний хавтас> [texture px]
#
# Mixamo-д өнгөгүй OBJ оруулсан тул ясжуулсан FBX-д UV алга: Tripo-гийн ижил загварын өнгөтэй OBJ-ийн оройнууд
# байрлалаараа яг таардаг (тоо, байрлал ижил, дараалал өөр) тул UV-г OBJ-оос, ясны жинг FBX-ээс авна.
#
# Хөдөлгөөн: яс бүрийн эргэлтийг "анхны (T) байрлалаас хэр эргэсэн" хэлбэрээр (загварын орон зайд), эцгийнхтэй
# харьцангуй хадгална: L = D(эцэг)^-1 * D(яс), D = G(хөдөлгөөн) * G(анхны)^-1. Тоглоом D(яс) = D(эцэг) * L-ээр
# дахин угсарч, үений байрлалыг эцгийнхээс анхны уртаар тооцно (CharacterModel Follow-тэй ижил).
# Ташааны хэвтээ шилжилтийг (байрнаасаа явах) хасаж, хурдыг нь клипийн толгойд хадгална (хөл гулсахгүй тааруулахад).
import os, struct, sys, json
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fbx_reader

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
KTIME = 46186158000  # FBX: секундэд ногдох тик
FPS = 30.0

# Тоглоомын клипийн нэр -> эх файл (хөдөлгөөний хавтсанд харьцангуй). Чиглэл: f урагш, b арагш, l зүүн, r баруун.
CLIPS = {
    'idle': 'Pro Rifle Pack/idle.fbx',
    'idle_aim': 'Pro Rifle Pack/idle aiming.fbx',
    'crouch_idle': 'Pro Rifle Pack/idle crouching.fbx',
    'crouch_aim': 'Pro Rifle Pack/idle crouching aiming.fbx',
    'jump_up': 'Pro Rifle Pack/jump up.fbx',
    'jump_loop': 'Pro Rifle Pack/jump loop.fbx',
    'jump_down': 'Pro Rifle Pack/jump down.fbx',
}
for prefix, src in (('walk', 'walk'), ('run', 'run'), ('sprint', 'sprint'), ('crouch', 'walk crouching')):
    for tag, words in (('f', 'forward'), ('fl', 'forward left'), ('fr', 'forward right'), ('l', 'left'),
                       ('r', 'right'), ('b', 'backward'), ('bl', 'backward left'), ('br', 'backward right')):
        CLIPS[f'{prefix}_{tag}'] = f'Pro Rifle Pack/{src} {words}.fbx'
LOOPED = lambda name: not name.startswith('jump_up') and not name.startswith('jump_down')


def euler(deg):
    x, y, z = np.radians(deg)
    cx, sx, cy, sy, cz, sz = np.cos(x), np.sin(x), np.cos(y), np.sin(y), np.cos(z), np.sin(z)
    rx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    rz = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    return rz @ ry @ rx  # FBX eXYZ: X эхэлж эргэнэ


def quat(m):
    # 3x3 эргэлтийн матриц -> (x, y, z, w)
    t = np.trace(m)
    if t > 0:
        s = np.sqrt(t + 1.0) * 2
        q = [(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, 0.25 * s]
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = np.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        q = [0.25 * s, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s, (m[2, 1] - m[1, 2]) / s]
    elif m[1, 1] > m[2, 2]:
        s = np.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        q = [(m[0, 1] + m[1, 0]) / s, 0.25 * s, (m[1, 2] + m[2, 1]) / s, (m[0, 2] - m[2, 0]) / s]
    else:
        s = np.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
        q = [(m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, 0.25 * s, (m[1, 0] - m[0, 1]) / s]
    q = np.array(q)
    return q / np.linalg.norm(q)


class Scene:
    """FBX-ийн ясны мод: нэр, эцэг, статик шинж, хөдөлгөөний муруй."""

    def __init__(self, path):
        root, _ = fbx_reader.load(path)
        objs = root.first('Objects')
        self.objs = {o.props[0]: o for o in objs.children if o.props}
        conns = root.first('Connections').children
        self.bones = {}  # нэр -> Model node
        for o in objs.find('Model'):
            if o.props[2] == 'LimbNode':
                self.bones[o.props[1].split('\x00')[0].split(':')[-1]] = o
        byid = {o.props[0]: n for n, o in self.bones.items()}
        self.parent = {n: None for n in self.bones}
        for c in conns:
            if c.props[0] == 'OO' and c.props[1] in byid and c.props[2] in byid:
                self.parent[byid[c.props[1]]] = byid[c.props[2]]
        # Муруй: (яс, 'T'|'R', тэнхлэг) -> (цаг, утга)
        self.curves = {}
        nodes = {o.props[0]: o for o in objs.find('AnimationCurveNode')}
        curves = {o.props[0]: o for o in objs.find('AnimationCurve')}
        node_target = {}
        for c in conns:
            if c.props[0] == 'OP' and c.props[1] in nodes and c.props[2] in byid:
                prop = c.props[3]
                kind = 'T' if 'Translation' in prop else 'R' if 'Rotation' in prop else None
                if kind: node_target[c.props[1]] = (byid[c.props[2]], kind)
        self.length = 0.0
        for c in conns:
            if c.props[0] == 'OP' and c.props[1] in curves and c.props[2] in node_target:
                bone, kind = node_target[c.props[2]]
                cv = curves[c.props[1]]
                t = np.array(cv.first('KeyTime').props[0], dtype=np.float64) / KTIME
                v = np.array(cv.first('KeyValueFloat').props[0], dtype=np.float64)
                self.curves[(bone, kind, 'XYZ'.index(c.props[3][-1]))] = (t, v)
                self.length = max(self.length, t[-1])

    def prop(self, bone, name, default):
        p70 = self.bones[bone].first('Properties70')
        for p in p70.children if p70 else []:
            if p.props[0] == name: return np.array(p.props[4:7], dtype=np.float64)
        return np.array(default, dtype=np.float64)

    def local(self, bone, time=None):
        """Ясны эцэгтэй харьцангуй 4x4 хувиргалт (time=None: анхны / статик утгаар)."""
        t = self.prop(bone, 'Lcl Translation', [0, 0, 0])
        r = self.prop(bone, 'Lcl Rotation', [0, 0, 0])
        if time is not None:
            for kind, vec in (('T', t), ('R', r)):
                for axis in range(3):
                    key = (bone, kind, axis)
                    if key in self.curves:
                        ct, cv = self.curves[key]
                        vec[axis] = np.interp(time, ct, cv)
        pre = euler(self.prop(bone, 'PreRotation', [0, 0, 0]))
        post = euler(self.prop(bone, 'PostRotation', [0, 0, 0]))
        m = np.eye(4)
        m[:3, :3] = pre @ euler(r) @ post.T
        m[:3, 3] = t
        return m

    def globals(self, order, time=None):
        out = {}
        for n in order:
            m = self.local(n, time)
            out[n] = out[self.parent[n]] @ m if self.parent[n] else m
        return out


def bone_order(scene):
    order = []
    def visit(n):
        if n in order: return
        if scene.parent[n]: visit(scene.parent[n])
        order.append(n)
    for n in scene.bones: visit(n)
    return order


def write_glb(path, P, N, UV, J, W, I, order, parent, pos):
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
    a_i = add(I.astype(np.uint16), 5123, 'SCALAR', 34963) if len(P) < 65536 else add(I, 5125, 'SCALAR', 34963)
    index = {n: i for i, n in enumerate(order)}
    ibm = np.zeros((len(order), 16), dtype=np.float32)
    for i, n in enumerate(order):
        m = np.eye(4); m[:3, 3] = -pos[n]
        ibm[i] = m.T.reshape(-1)
    a_ibm = add(ibm, 5126, 'MAT4')
    nodes = []
    for n in order:
        local = pos[n] - (pos[parent[n]] if parent[n] else 0)
        node = {'name': 'mixamorig:' + n, 'translation': [float(x) for x in local]}
        kids = [index[c] for c in order if parent[c] == n]
        if kids: node['children'] = kids
        nodes.append(node)
    nodes.append({'name': 'body', 'mesh': 0, 'skin': 0})
    roots = [index[n] for n in order if not parent[n]]
    gltf = {
        'asset': {'version': '2.0', 'generator': 'tools/mixamo_import.py'},
        'scene': 0, 'scenes': [{'nodes': roots + [len(nodes) - 1]}],
        'nodes': nodes,
        'meshes': [{'primitives': [{'attributes': {'POSITION': a_pos, 'NORMAL': a_nrm, 'TEXCOORD_0': a_uv,
                                                   'JOINTS_0': a_j, 'WEIGHTS_0': a_w}, 'indices': a_i, 'material': 0}]}],
        'materials': [{'name': 'character', 'doubleSided': True}],
        'skins': [{'joints': list(range(len(order))), 'inverseBindMatrices': a_ibm, 'skeleton': roots[0]}],
        'accessors': accessors, 'bufferViews': views, 'buffers': [{'byteLength': sum(len(b) for b in blobs)}],
    }
    js = json.dumps(gltf, separators=(',', ':')).encode()
    js += b' ' * ((4 - len(js) % 4) % 4)
    bin_ = b''.join(blobs)
    with open(path, 'wb') as f:
        f.write(struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(bin_)))
        f.write(struct.pack('<I', len(js)) + b'JSON' + js)
        f.write(struct.pack('<I', len(bin_)) + b'BIN\0' + bin_)


def build_mesh(scene_path, obj_path, order, bind):
    root, _ = fbx_reader.load(scene_path)
    objs = root.first('Objects')
    names = {o.props[0]: o for o in objs.children if o.props}
    conns = root.first('Connections').children
    geo = objs.find('Geometry')[0]
    ctrl = np.array(geo.first('Vertices').props[0], dtype=np.float64).reshape(-1, 3)
    index = {n: i for i, n in enumerate(order)}
    # Ясны жин (FBX-ийн орой бүрд)
    skin = [names[c.props[1]] for c in conns if c.props[2] == geo.props[0] and names[c.props[1]].name == 'Deformer'][0]
    weights = [[] for _ in range(len(ctrl))]
    for c in conns:
        if c.props[2] != skin.props[0]: continue
        cluster = names[c.props[1]]
        idx = cluster.first('Indexes')
        if idx is None: continue
        bone = [names[b.props[1]] for b in conns if b.props[2] == cluster.props[0] and names[b.props[1]].name == 'Model'][0]
        j = index[bone.props[1].split('\x00')[0].split(':')[-1]]
        for v, w in zip(idx.props[0], cluster.first('Weights').props[0]):
            if w > 0: weights[v].append((w, j))
    J4 = np.zeros((len(ctrl), 4), dtype=np.uint8)
    W4 = np.zeros((len(ctrl), 4), dtype=np.float32)
    for v, ws in enumerate(weights):
        ws = sorted(ws, reverse=True)[:4]
        total = sum(w for w, _ in ws) or 1.0
        for k, (w, j) in enumerate(ws):
            J4[v, k], W4[v, k] = j, w / total
        if not ws: J4[v, 0], W4[v, 0] = index['Hips'], 1.0

    # Өнгөтэй OBJ: орой, UV, гурвалжин
    ov, ot, faces = [], [], []
    for line in open(obj_path, errors='ignore'):
        if line.startswith('v '): ov.append([float(x) for x in line.split()[1:4]])
        elif line.startswith('vt '): ot.append([float(x) for x in line.split()[1:3]])
        elif line.startswith('f '):
            corners = [tuple(int(p) - 1 for p in c.split('/')[:2]) for c in line.split()[1:]]
            for k in range(1, len(corners) - 1): faces.append((corners[0], corners[k], corners[k + 1]))
    ov, ot = np.array(ov), np.array(ot)
    # OBJ орой -> FBX орой (байрлалаар)
    key = lambda p: tuple(np.round(p, 4))
    lookup = {key(p): i for i, p in enumerate(ctrl)}
    match = np.array([lookup.get(key(p), -1) for p in ov])
    for i in np.where(match < 0)[0]:
        match[i] = int(np.argmin(np.linalg.norm(ctrl - ov[i], axis=1)))
    err = np.linalg.norm(ctrl[match] - ov, axis=1).max()
    assert err < 1e-3, f'OBJ ба FBX-ийн оройнууд таарахгүй байна ({err})'

    verts, kidx, tris = [], {}, []
    for f in faces:
        tri = []
        for vi, ti in f:
            k = (vi, ti)
            if k not in kidx:
                kidx[k] = len(verts); verts.append(k)
            tri.append(kidx[k])
        tris.append(tri)
    vi = np.array([k[0] for k in verts])
    P = ov[vi].astype(np.float32)
    UV = ot[[k[1] for k in verts]].astype(np.float32)
    UV[:, 1] = 1.0 - UV[:, 1]
    J = J4[match[vi]]
    W = W4[match[vi]]
    t = np.array(tris)
    Nv = np.zeros_like(ov)
    fn = np.cross(ov[vi[t[:, 1]]] - ov[vi[t[:, 0]]], ov[vi[t[:, 2]]] - ov[vi[t[:, 0]]])
    for k in range(3): np.add.at(Nv, vi[t[:, k]], fn)
    N = Nv[vi]
    N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-12)
    return P, N.astype(np.float32), UV, J, W, t.astype(np.uint32).reshape(-1)


def convert_clip(path, order, parent, bind_rot, bind_pos):
    scene = Scene(path)
    missing = [n for n in order if n not in scene.bones]
    frames = max(2, int(round(scene.length * FPS)) + 1)
    hips, rots, lead = [], [], []
    for f in range(frames):
        g = scene.globals([n for n in order if n in scene.bones], f / FPS)
        D = {}
        for n in order:
            D[n] = g[n][:3, :3] @ bind_rot[n].T if n in g else (D[parent[n]] if parent[n] else np.eye(3))
        local = []
        for n in order:
            L = D[parent[n]].T @ D[n] if parent[n] else D[n]
            local.append(quat(L))
        rots.append(local)
        hips.append(g['Hips'][:3, 3].copy())
        lead.append(g['RightFoot'][:3, 3] - g['LeftFoot'][:3, 3])
    hips = np.array(hips)
    rots = np.array(rots)
    # Кватернионы тэмдгийг дараалсан кадруудад тогтвортой (интерполяцид богино замаар)
    for f in range(1, frames):
        flip = (rots[f] * rots[f - 1]).sum(1) < 0
        rots[f][flip] *= -1
    duration = (frames - 1) / FPS
    velocity = (hips[-1] - hips[0]) / duration
    velocity[1] = 0.0
    times = np.arange(frames)[:, None] / FPS
    hips = hips - velocity * times
    hips[:, 0] -= hips[:, 0].mean() - bind_pos['Hips'][0]
    hips[:, 2] -= hips[:, 2].mean() - bind_pos['Hips'][2]
    # Алхааны үе: баруун хөл явах зүг рүүгээ хамгийн урд гарсан кадр (тоглоомын walkPhase 0-тэй таарна). Зогсолтод 0.
    phase = 0.0
    if np.linalg.norm(velocity) > 0.1:
        ahead = np.array(lead)[:-1] @ (velocity / np.linalg.norm(velocity))
        phase = float(np.argmax(ahead)) / (frames - 1)
    return frames, velocity, hips, rots, missing, phase


def main():
    name, rig_path, obj_path, color_path, anim_dir = sys.argv[1:6]
    tex = int(sys.argv[6]) if len(sys.argv) > 6 else 2048
    rig = Scene(rig_path)
    order = bone_order(rig)
    G = rig.globals(order)
    bind_pos = {n: G[n][:3, 3] for n in order}
    bind_rot = {n: G[n][:3, :3] for n in order}

    P, N, UV, J, W, I = build_mesh(rig_path, obj_path, order, bind_pos)
    glb = os.path.join(ROOT, 'assets', f'{name}_mesh.glb')
    write_glb(glb, P, N, UV, J, W, I, order, rig.parent, bind_pos)
    print(glb, len(P), 'vertices', len(I) // 3, 'triangles', len(order), 'bones')

    image = Image.open(color_path).convert('RGB').resize((tex, tex), Image.LANCZOS)
    color = os.path.join(ROOT, 'assets', f'{name}_color.png')
    image.save(color, optimize=True)
    print(color, os.path.getsize(color), 'bytes')

    # Хөдөлгөөн: "ZMAN" 3, ясны тоо, нэрс + анхны байрлал (3 float), клипийн тоо; клип бүр: нэр, кадр, fps, давтагдах, хурд (vec3), үе,
    # кадр бүрд ташааны байрлал (3 float) + яс бүрийн эргэлт (int16 x4, /32767)
    out = bytearray(b'ZMAN') + struct.pack('<II', 3, len(order))
    for n in order:
        b = n.encode(); out += struct.pack('<B', len(b)) + b + struct.pack('<3f', *bind_pos[n])
    clips = []
    for clip, rel in CLIPS.items():
        path = os.path.join(anim_dir, rel)
        if not os.path.exists(path):
            print('  алга:', rel); continue
        frames, vel, hips, rots, missing, phase = convert_clip(path, order, rig.parent, bind_rot, bind_pos)
        clips.append((clip, frames, vel, hips, rots, phase))
        print(f'  {clip:12s} {frames:3d} кадр {frames / FPS:5.2f} с хурд {np.linalg.norm(vel):.3f} ({vel[0]:+.2f} {vel[2]:+.2f})'
              + (f' ясгүй: {missing}' if missing else ''))
    out += struct.pack('<I', len(clips))
    for clip, frames, vel, hips, rots, phase in clips:
        b = clip.encode()
        out += struct.pack('<B', len(b)) + b
        out += struct.pack('<IfB4f', frames, FPS, 1 if LOOPED(clip) else 0, *vel, phase)
        for f in range(frames):
            out += struct.pack('<3f', *hips[f])
            out += np.clip(np.round(rots[f] * 32767), -32767, 32767).astype('<i2').tobytes()
    anims = os.path.join(ROOT, 'assets', 'soldier_anims.bin')
    open(anims, 'wb').write(bytes(out))
    print(anims, len(out), 'bytes', len(clips), 'clips')


if __name__ == '__main__':
    main()
