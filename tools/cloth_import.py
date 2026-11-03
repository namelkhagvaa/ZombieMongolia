# Хувцасны бараа: Tripo-гийн "үндсэн бие + зөвхөн тэр хувцас" загвараас хувцсыг тасдаж, үндсэн биеийн ясанд өмсгөнө.
#   assets/items/<name>_mesh.glb   - хувцас (үндсэн биеийн ясны бүтэц, жинтэй)
#   assets/items/<name>_color.png  - өнгө
#   assets/items/<name>_hide.bin   - хувцас дор нуугдах биеийн гурвалжнууд (u32 тоо + u32 дугаарууд, <base>_mesh.glb-ийн
#                                    гурвалжны дарааллаар)
#
#   python tools/cloth_import.py [--set] <name> <хувцастай.fbx> <basecolor> <vehicleprep.exe> [гурвалжин=10000] [base=man]
# --geo: хувцас арьстай ойролцоо өнгөтэй (шаргал, бор) бол өнгөөр биш байрлалаар ангилна (--set-тэй хамт)
# --set: бүтэн иж бүрдэл (дотуур өмдгүй): <name>_helmet, _jacket, _pants, _boots, _gloves бараа болгон хуваана; бүгд
#        <name>_color.png-г хуваалцана.
# Жишээ (цэргийн куртка):
#   python tools/cloth_import.py jacket1 assets/cloth/tripo_convert_eb20a667-....fbx <...>.fbm/military_man_3d_model_basecolor.JPEG vehicleprep.exe
#
# Хувцастай загвар нь үндсэн биетэй (tools/mixamo_import.py-ийн assets/<base>_mesh.glb) ижил хүн, ижил T-pose байх ёстой.
# 1. Гурвалжин бүрийн өнгөөр ангилна: арьс (улбар), дотуур өмд (саарал) = бие; бусад (хувцасны өнгө) = хувцас;
#    тодорхойгүйг хөршүүдээр нь шийднэ.
# 2. Гар: Tripo гарыг бага зэрэг өөр өнцгөөр алдалдаг: бугуйн байрлалаар мөрийг тойруулан эргүүлж тааруулна.
# 3. meshoptimizer-ээр (vehicleprep prop) хялбаршуулна.
# 4. Орой бүрд үндсэн биеийн хамгийн ойр оройн ясны жинг өгнө.
# 5. Хувцсан дор бүрэн далдлагдсан биеийн гурвалжнуудыг тэмдэглэнэ (хөдлөхөд арьс цоорч гарахгүй, зурах ажил бага).
import os, struct, subprocess, sys, json, tempfile
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fbx_reader
from mixamo_import import write_glb

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')


def read_glb(path):
    data = open(path, 'rb').read()
    n = struct.unpack_from('<I', data, 12)[0]
    js = json.loads(data[20:20 + n])
    binary = data[20 + n + 8:]
    def acc(i):
        a = js['accessors'][i]
        v = js['bufferViews'][a['bufferView']]
        dtype = {5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}[a['componentType']]
        width = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}[a['type']]
        arr = np.frombuffer(binary, dtype=dtype, count=a['count'] * width, offset=v.get('byteOffset', 0))
        return arr.reshape(a['count'], width) if width > 1 else arr
    prim = js['meshes'][0]['primitives'][0]
    at = prim['attributes']
    joints = js['skins'][0]['joints']
    names = [js['nodes'][j]['name'].split(':')[-1] for j in joints]
    parent = {}
    for i, node in enumerate(js['nodes']):
        for c in node.get('children', []): parent[c] = i
    pos = {}
    for j, name in zip(joints, names):
        p, k = np.zeros(3), j
        while k is not None:
            p += np.array(js['nodes'][k].get('translation', [0, 0, 0]))
            k = parent.get(k)
        pos[name] = p
    par = {name: (js['nodes'][parent[j]]['name'].split(':')[-1] if j in parent and parent[j] in joints else None)
           for j, name in zip(joints, names)}
    return dict(P=acc(at['POSITION']).astype(np.float64), N=acc(at['NORMAL']).astype(np.float64),
                J=acc(at['JOINTS_0']), W=acc(at['WEIGHTS_0']), I=acc(prim['indices']).reshape(-1, 3),
                order=names, parent=par, pos=pos)


def face_zone(base):
    """Нүүрний гурвалжнууд (хөмсөгнөөс эрүү хүртэлх урд тал): бараа нуухгүй."""
    c = base['P'][base['I']].mean(1)
    top = base['P'][:, 1].max()
    return (c[:, 1] > top - 0.13) & (c[:, 1] < top - 0.04) & (c[:, 2] > base['pos']['Head'][2] + 0.02)


def nearest(points, cloud, chunk=1500, margin=0.03):
    """Цэг бүрд cloud-ийн хамгийн ойрын цэгийн дугаар. Цэгүүдийг орон зайгаар эрэмбэлж, хэсэг бүрт зөвхөн түүний хүрээ +
    margin доторх cloud-ийн цэгүүдээс хайна (margin-аас хол гарсныг бүх cloud-оос дахин хайна: хариу яг зөв хэвээр)."""
    points = np.asarray(points, dtype=np.float32)
    cloud = np.asarray(cloud, dtype=np.float32)
    cell = np.round(points / 0.03).astype(np.int64)
    order = np.lexsort((cell[:, 2], cell[:, 0], cell[:, 1]))
    out = np.empty(len(points), dtype=np.int64)
    def search(idx, ids):
        c = cloud[ids]
        d = (c * c).sum(1)[None, :] - 2.0 * points[idx] @ c.T
        return ids[np.argmin(d, 1)]
    everything = np.arange(len(cloud))
    for s in range(0, len(points), chunk):
        idx = order[s:s + chunk]
        lo, hi = points[idx].min(0) - margin, points[idx].max(0) + margin
        ids = np.where(((cloud >= lo) & (cloud <= hi)).all(1))[0]
        if len(ids) == 0:
            out[idx] = search(idx, everything)
            continue
        found = search(idx, ids)
        out[idx] = found
        far = idx[((cloud[found] - points[idx]) ** 2).sum(1) > margin * margin]
        for t in range(0, len(far), chunk): out[far[t:t + chunk]] = search(far[t:t + chunk], everything)
    return out


def load_dressed(path):
    root, _ = fbx_reader.load(path)
    g = root.first('Objects').find('Geometry')[0]
    v = np.array(g.first('Vertices').props[0], dtype=np.float64).reshape(-1, 3)
    pvi = np.array(g.first('PolygonVertexIndex').props[0], dtype=np.int64)
    assert len(pvi) % 3 == 0 and (pvi[2::3] < 0).all(), 'гурвалжин биш олон өнцөгт'
    uvl = g.first('LayerElementUV')
    uv = np.array(uvl.first('UV').props[0], dtype=np.float64).reshape(-1, 2)
    uvi = np.array(uvl.first('UVIndex').props[0], dtype=np.int64).reshape(-1, 3)
    tri = np.where(pvi < 0, ~pvi, pvi).reshape(-1, 3)
    # Tripo-гийн FBX: зарим нь Z дээш (зангилаа X-ээр -90 эргэсэн) -> Y дээш, +Z урагш; заримд нь эргэлтгүй (Y дээш)
    rx = 0.0
    for m in root.first('Objects').find('Model'):
        p70 = m.first('Properties70')
        for p in p70.children if p70 else []:
            if p.props[0] == 'Lcl Rotation': rx = p.props[4]
    pos = np.stack([v[:, 0], v[:, 2], -v[:, 1]], 1) if abs(rx + 90.0) < 1.0 else v.copy()
    return pos, tri, uv, uvi


def classify(pos, tri, uv, uvi, tex, underwear=True):
    H, W = tex.shape[:2]
    cu = uv[uvi].mean(1)
    col = tex[np.clip(((1 - cu[:, 1]) * H).astype(int), 0, H - 1), np.clip((cu[:, 0] * W).astype(int), 0, W - 1)].astype(int)
    r, g, b = col[:, 0], col[:, 1], col[:, 2]
    y = pos[tri].mean(1)[:, 1]
    hi, lo = col.max(1), col.min(1)
    label = np.full(len(tri), -1)
    skin = (r > g + 12) & (r > 90) & (r > b + 20)
    if not underwear:
        # Иж бүрдэл: арьснаас бусад бүх өнгө (хар гутал, бээлий ч) хувцас. Нүд, хөмсөг гэх мэт бараан хэсгийг
        # split_set гадаргуугаас өндөрт нь шүүнэ.
        return ~skin, skin, col
    grey = (hi - lo < 14) & (hi > 30) & (hi < 140) & (y < 0.56) & underwear
    label[skin | grey] = 0
    label[~(skin | grey) & (hi - lo >= 14)] = 1
    # Тодорхойгүй (хар, цагаан толбо): хөрш гурвалжнуудын олонхоор
    for _ in range(12):
        unknown = label < 0
        if not unknown.any(): break
        score = np.zeros(len(pos)); count = np.zeros(len(pos))
        known = ~unknown
        for k in range(3):
            np.add.at(score, tri[known, k], label[known])
            np.add.at(count, tri[known, k], 1)
        s = score[tri[unknown]].sum(1); c = count[tri[unknown]].sum(1)
        decided = c > 0
        idx = np.where(unknown)[0][decided]
        label[idx] = (s[decided] / c[decided] > 0.5).astype(int)
    label[label < 0] = 0
    return label == 1, skin, col


def classify_geo(pos, tri, tex_col, base):
    """Хувцас нь арьстай ойролцоо өнгөтэй үед (шаргал, бор камуфляж): байрлалаар ялгана. Бугуйнаас цааш гар (арьс),
    толгой, хүзүүнд биеийн гадаргууд наалдсан хэсэг нүүр, хүзүү (арьс); бусад бүгд хувцас (бүтэн иж бүрдэл: дотуур
    өмд харагдахгүй). Бугуйн орчимд өнгөөр шийднэ (ханцуйн ирмэг)."""
    cen = pos[tri].mean(1)
    r, g, b = tex_col[:, 0], tex_col[:, 1], tex_col[:, 2]
    skin_color = (r > g + 12) & (r > 90) & (r > b + 20)
    skin = np.zeros(len(tri), bool)
    hand_x = abs(base['pos']['LeftHand'][0])
    ax = np.abs(cen[:, 0])
    skin |= ax > hand_x + 0.02
    edge = (ax > hand_x - 0.02) & (ax <= hand_x + 0.02)
    skin |= edge & skin_color
    top = base['P'][:, 1].max()
    neck = base['pos']['Neck'][1]
    head = np.where((cen[:, 1] > neck - 0.01) & (ax < 0.2))[0]
    k = nearest(cen[head], base['P'])
    offset = ((cen[head] - base['P'][k]) * base['N'][k]).sum(1)
    bone = np.array(base['order'])[base['J'][k, np.argmax(base['W'][k], 1)]]
    y = cen[head, 1]
    on_head = np.isin(bone, ['Head', 'HeadTop_End'])
    # Малгай: хөмсөгнөөс дээш, эсвэл саравч (гадаргуугаас 1.2 см-ээс илүү); нүүр, чих, эрүү бол арьс
    # (саравч зөвхөн урд талд: чих ч гадаргуугаас холдсон байдаг)
    cap = (y > top - 0.055) | ((offset > 0.012) & (y > top - 0.09) & (cen[head, 2] > base['pos']['Head'][2] + 0.04))
    # Хүзүү: зөвхөн Neck ясанд, үенээс дээш, арьсны өнгөтэй, гадаргууд наалдсан бол арьс (мөрний орой, захвар бол хувцас)
    neck_skin = (bone == 'Neck') & (y > neck + 0.012) & skin_color[head] & (offset < 0.006)
    skin[head[np.where(on_head, ~cap, neck_skin)]] = True
    return ~skin, skin


def fix_arms(pos, skin_tris, tri, base):
    """Хувцастай загварын гарыг мөрийг тойруулан эргүүлж, бугуйг үндсэн биеийнхтэй тааруулна."""
    out = pos.copy()
    skin_pts = pos[np.unique(tri[skin_tris])]
    for side, name in ((1, 'LeftArm'), (-1, 'RightArm')):
        shoulder = base['pos'][name]
        sel = lambda p: p[side * p[:, 0] > 0.43]
        hand_d = sel(skin_pts).mean(0) if len(sel(skin_pts)) > 200 else sel(pos).mean(0)  # (бээлийтэй бол бүх цэгээр)
        hand_b = sel(base['P']).mean(0)
        a = (hand_d - shoulder) / np.linalg.norm(hand_d - shoulder)
        b = (hand_b - shoulder) / np.linalg.norm(hand_b - shoulder)
        axis = np.cross(a, b)
        angle = np.arctan2(np.linalg.norm(axis), a @ b)
        if np.linalg.norm(axis) < 1e-9: continue
        axis /= np.linalg.norm(axis)
        print(f'  {name}: гар {np.degrees(angle):.1f}° эргүүлэв')
        sx = side * shoulder[0]
        w = np.clip((side * pos[:, 0] - (sx - 0.03)) / 0.08, 0.0, 1.0)
        w = w * w * (3 - 2 * w)
        m = w > 0
        theta = angle * w[m]
        d = pos[m] - shoulder
        cos, sin = np.cos(theta)[:, None], np.sin(theta)[:, None]
        rot = d * cos + np.cross(axis, d) * sin + axis * (d @ axis)[:, None] * (1 - cos)
        out[m] = shoulder + rot
    # Хөл: ташааны үеийг тойруулан эргүүлж шагайн төвийг тааруулна (Tripo хөлийг өөр өргөнөөр алцайлгадаг)
    for side, name in ((1, 'LeftUpLeg'), (-1, 'RightUpLeg')):
        hip = base['pos'][name]
        sel = lambda p: p[(side * p[:, 0] > 0.02) & (p[:, 1] > 0.06) & (p[:, 1] < 0.10)]
        ankle_d, ankle_b = sel(pos).mean(0), sel(base['P']).mean(0)
        a = (ankle_d - hip) / np.linalg.norm(ankle_d - hip)
        b = (ankle_b - hip) / np.linalg.norm(ankle_b - hip)
        axis = np.cross(a, b)
        if np.linalg.norm(axis) < 1e-9: continue
        angle = np.arctan2(np.linalg.norm(axis), a @ b)
        axis /= np.linalg.norm(axis)
        print(f'  {name}: хөл {np.degrees(angle):.1f}° эргүүлэв')
        w = np.clip((hip[1] - 0.02 - out[:, 1]) / 0.08, 0.0, 1.0) * np.clip((side * out[:, 0] + 0.01) / 0.02, 0.0, 1.0)
        w = w * w * (3 - 2 * w)
        m = w > 0
        theta = angle * w[m]
        d = out[m] - hip
        cos, sin = np.cos(theta)[:, None], np.sin(theta)[:, None]
        out[m] = hip + d * cos + np.cross(axis, d) * sin + axis * (d @ axis)[:, None] * (1 - cos)
    return out


def foot_frame(P, side):
    """Тавхайн (ул, y < 0.03) төв, чиглэл (өсгийнөөс хуруу руу, xz), урт, өргөн."""
    s = P[(P[:, 1] < 0.03) & (side * P[:, 0] > 0)][:, [0, 2]]
    c = s.mean(0)
    d = np.linalg.svd(s - c, full_matrices=False)[2][0]
    if d[1] < 0: d = -d
    across = np.array([-d[1], d[0]])
    return c, d, np.ptp((s - c) @ d), np.ptp((s - c) @ across)


def fit_feet(pos, ids, base):
    """Гутлын ул хэсгийг үндсэн биеийн тавхай дээр шилжүүлж, эргүүлж, хэмжээг нь тааруулна: Tripo-гийн хувцастай
    загварын тавхай өөр байрлалтай (дотогш, хойш), хуруу нь өөр зүг рүү харсан байдаг тул гутал хөлөөсөө салж харагдана.
    Шагайнаас дээш (түрий) аажим сулруулна: түрий шилбэтэйгээ таарсан (fix_arms хөлийг тааруулсан)."""
    out = pos.copy()
    for side in (1, -1):
        sel = ids[side * pos[ids, 0] > 0]
        cd, dd, ld, wd = foot_frame(pos[sel], side)
        cb, db, lb, wb = foot_frame(base['P'], side)
        yaw = np.arctan2(dd[0], dd[1]) - np.arctan2(db[0], db[1])
        along = max(1.0, lb * 1.04 / ld)   # гутал хөлөөс жаахан урт, өргөн байна
        across = max(1.0, wb * 1.12 / wd)
        print(f'  гутал {side:+d}: шилжилт {np.round(cb - cd, 3)}, эргэлт {np.degrees(-yaw):.1f}°, урт x{along:.2f}, өргөн x{across:.2f}')
        p = pos[sel]
        w = np.clip((0.10 - p[:, 1]) / 0.04, 0.0, 1.0)
        w = w * w * (3 - 2 * w)
        xz = p[:, [0, 2]] - cd
        local = np.stack([xz @ np.array([-dd[1], dd[0]]) * across, xz @ dd * along], 1)
        bx, bd = np.array([-db[1], db[0]]), db
        moved = cb + local[:, :1] * bx + local[:, 1:] * bd
        p[:, 0] += (moved[:, 0] - p[:, 0]) * w
        p[:, 2] += (moved[:, 1] - p[:, 2]) * w
        out[sel] = p
    return out


def push_out(points, base, gap=0.003, reach=0.03):
    """Биеийн гадаргуугаас доош (эсвэл gap-аас ойр) орсон хувцасны цэгийг биеийн хэвийн зүгт гадагш түлхэнэ: хоёр
    загварын биеийн хэлбэр бага зэрэг ялгаатай тул (мөр, ташаа) арьс хувцсыг цоолж гарахгүй."""
    k = nearest(points, base['P'])
    n = base['N'][k]
    delta = points - base['P'][k]
    along = (delta * n).sum(1)
    lateral = np.sqrt(np.maximum((delta * delta).sum(1) - along * along, 0.0))
    move = (along < gap) & (lateral < reach)
    out = points.copy()
    out[move] += (gap - along[move])[:, None] * n[move]
    print(f'  гадагш түлхсэн цэг {move.sum()} / {len(points)}')
    return out


def export_item(name, pos, tri, uv, uvi, mask, base, vprep, target, out_dir, region=None):
    """mask-ийн гурвалжнуудыг нэг бараа болгож бичнэ (_mesh.glb, _hide.bin)."""
    # (орой, uv) хос бүр нэг орой: vehicleprep-ийн түүхий формат
    ct, cu = tri[mask], uvi[mask]
    keys = np.stack([ct.reshape(-1), cu.reshape(-1)], 1)
    uniq, inverse = np.unique(keys, axis=0, return_inverse=True)
    P = pos[uniq[:, 0]].astype(np.float32)
    UV = uv[uniq[:, 1]].astype(np.float32)
    UV[:, 1] = 1.0 - UV[:, 1]
    I = inverse.reshape(-1).astype(np.uint32)
    tmp = tempfile.mkdtemp()
    raw = os.path.join(tmp, name)
    P.tofile(raw + '.pos'); UV.tofile(raw + '.uv'); I.tofile(raw + '.idx')
    height = float(P[:, 1].max() - P[:, 1].min())
    vm = raw + '.vmesh'
    env = dict(os.environ, VPREP_HEIGHT='1')
    env.setdefault('VPREP_ERROR', '0.3')
    # (vehicleprep prop нь өгсөн тооноос ~3 дахин их гурвалжин үлдээдэг: 12000 -> 36000; хүссэн тоонд хүргэхийн тулд хуваана)
    subprocess.run([vprep, 'prop', raw, vm, str(max(100, target // 3)), f'{height:.6f}'], check=True, env=env,
                   stdout=subprocess.DEVNULL)
    data = open(vm, 'rb').read()
    assert data[:4] == b'VMSH'
    _, parts = struct.unpack_from('<II', data, 4)
    off = 12
    Ps, Ns, UVs, Is = [], [], [], []
    base_index = 0
    for _ in range(parts):
        pivot = np.frombuffer(data, np.float32, 3, off); off += 12
        kind, vc, ic = struct.unpack_from('<III', data, off); off += 12
        p = np.frombuffer(data, np.float32, vc * 3, off).reshape(-1, 3) + pivot; off += vc * 12
        n = np.frombuffer(data, np.float32, vc * 3, off).reshape(-1, 3); off += vc * 12
        t = np.frombuffer(data, np.float32, vc * 2, off).reshape(-1, 2); off += vc * 8
        i = np.frombuffer(data, np.uint16, ic, off).astype(np.uint32); off += ic * 2
        Ps.append(p); Ns.append(n); UVs.append(t); Is.append(i + base_index); base_index += vc
    P = np.concatenate(Ps).astype(np.float32)
    N = np.concatenate(Ns).astype(np.float32)
    UV = np.concatenate(UVs).astype(np.float32)
    I = np.concatenate(Is).astype(np.uint32)
    err = abs((P[:, 1].max() - P[:, 1].min()) - height)
    assert err < 0.01, f'хялбаршуулалтын масштаб өөрчлөгдсөн ({err})'

    # Ясны жин: үндсэн биеийн хамгийн ойр орой
    near = nearest(P, base['P'])
    J, W = base['J'][near], base['W'][near]
    write_glb(os.path.join(out_dir, f'{name}_mesh.glb'), P, N, UV, J, W, I, base['order'], base['parent'], base['pos'])

    # Хувцас дор далдлагдах биеийн гурвалжнууд: орой бүр хувцасны гадаргуугаас доор (хэвийн зүгт 0..5 см),
    # хажуу тийш ойрхон (хувцасны ирмэгийн цаана биш)
    dense = pos[np.unique(ct)]
    dense = dense[:: max(1, len(dense) // 120000)]
    k = nearest(base['P'], dense, chunk=400)
    delta = dense[k] - base['P']
    along = (delta * base['N']).sum(1)
    lateral = np.sqrt(np.maximum((delta * delta).sum(1) - along * along, 0.0))
    covered = (along > -0.004) & (along < 0.05) & (lateral < 0.012)
    hide = covered[base['I']].all(1)
    hide &= ~face_zone(base)  # (нүүрийг ямар ч бараа нуухгүй: хувцастай загварын нүд, хөмсөг бараанд орсон байж болно)
    if region is not None: hide |= region(base['P'][base['I']].mean(1))  # (гутал, бээлий: өмссөн хэсэг бүтнээрээ)
    hidden = np.where(hide)[0].astype(np.uint32)
    with open(os.path.join(out_dir, f'{name}_hide.bin'), 'wb') as f:
        f.write(struct.pack('<I', len(hidden)) + hidden.tobytes())
    print(f'  {name}: {len(I) // 3} гурвалжин, биеийн {len(hidden)} гурвалжин нуугдана')


def split_set(pos, tri, cloth, tex_col, base):
    """Иж бүрдлийг бараа болгон хуваана: толгой (биеийн толгойн яс), бээлий (бугуйнаас цааш), гутал (харанхуй ул),
    дээд (бүсний дээд талаас), доод."""
    cen = pos[tri].mean(1)
    y = cen[:, 1]
    hi, lo = tex_col.max(1), tex_col.min(1)
    dark = (hi < 60) & (hi - lo < 14)  # хар гутал (бараан камуфляжийн толбо ханалттай тул орохгүй)
    parts = {}
    idx = np.where(cloth)[0]
    near = nearest(cen[idx], base['P'])
    bone = np.array(base['order'])[base['J'][near, np.argmax(base['W'][near], 1)]]
    head = np.zeros(len(tri), bool)
    head[idx] = np.isin(bone, ['Head', 'HeadTop_End'])
    # Дуулга, малгай: өнгөтэй (ханасан) эсвэл толгойн гадаргуугаас 6 мм-ээс илүү гадагш. Нүүрний бараан саарал хэсэг
    # (нүд, хөмсөг) гадаргуу дээрээ байдаг тул орохгүй.
    hidx = np.where(head)[0]
    hn = nearest(cen[hidx], base['P'])
    offset = ((cen[hidx] - base['P'][hn]) * base['N'][hn]).sum(1)
    tinted = (tex_col[hidx].max(1) - tex_col[hidx].min(1)) >= 14
    # Нүүрний урд тал (нүд, хөмсөг, хамар): дуулганы хүрээнээс доош, гадаргуудаа наалдсан (push_out-ын 3 мм) хэсэг
    head_z = base['pos']['Head'][2]
    front = (cen[hidx, 2] > head_z + 0.01) & (cen[hidx, 1] < 0.915) & (offset < 0.0045)
    drop = ((offset < 0.006) & ~tinted) | front
    head[hidx[drop]] = False
    face = np.zeros(len(tri), bool)
    face[hidx[drop]] = True
    hand_x = abs(base['pos']['LeftHand'][0]) - 0.005
    hands = cloth & (np.abs(cen[:, 0]) > hand_x) & ~head
    def dark_ratio(mask, lo_y, hi_y, step=0.005):
        bins = np.arange(lo_y, hi_y, step)
        ratio = np.array([dark[mask & (y >= b) & (y < b + step)].mean() if (mask & (y >= b) & (y < b + step)).sum() > 50 else 0.0
                          for b in bins])
        return bins, ratio
    # Гутал: доороос дээш харанхуй давамгайлсан хэсэг (өмдний камуфляжийн толбоноос ялгаатай нь бүтэн хар)
    bins, ratio = dark_ratio(cloth & ~hands, 0.0, 0.4)
    boot_top = 0.0
    dark_bins = np.where(ratio >= 0.5)[0]
    if len(dark_bins) and bins[dark_bins[0]] < 0.06:  # (улын хээ цайвар байж болно: хамгийн доод нь заавал биш)
        k = dark_bins[0]
        while k + 1 < len(ratio) and ratio[k + 1] >= 0.5: k += 1
        boot_top = bins[k] + 0.005
    boots = cloth & (y < boot_top) & ~hands & ~head
    # Бүсэлхий: ташаа ба нурууны үений хооронд (бүс камуфляжтай ойролцоо өнгөтэй тул өнгөөр найдваргүй)
    hips, spine = base['pos']['Hips'][1], base['pos']['Spine'][1]
    waist = hips + 0.4 * (spine - hips)
    rest = cloth & ~head & ~face & ~hands & ~boots
    parts['helmet'] = head
    parts['jacket'] = rest & (y >= waist)
    parts['pants'] = rest & (y < waist)
    parts['boots'] = boots
    parts['gloves'] = hands
    print(f'  гутлын орой {boot_top:.3f}, бүс {waist:.3f}')
    # Гутал, бээлий доорх биеийг бүтнээр нь нууна: Tripo-гийн гутал тавхайнаас нарийн, хуруу нь гадагш эргээгүй
    regions = {'boots': lambda c: c[:, 1] < boot_top - 0.01, 'gloves': lambda c: np.abs(c[:, 0]) > hand_x + 0.01}
    # Бүсээр (PUBG-ийн зарчим): цамц их бие, гарыг, өмд бүсэлхийнээс доошхийг бүтнээр нь нууна (дотуур өмд цоолж гарахгүй)
    neck = base['pos']['Neck'][1]
    regions['jacket'] = lambda c: (c[:, 1] >= waist) & (c[:, 1] < neck) & (np.abs(c[:, 0]) < hand_x - 0.01)
    regions['pants'] = lambda c: (c[:, 1] < waist) & (c[:, 1] > max(boot_top, 0.1))
    return {k: v for k, v in parts.items() if v.sum() > 200}, regions


def main():
    args = sys.argv[1:]
    as_set = '--set' in args
    geo = '--geo' in args  # (байрлалаар ангилах: хувцас арьстай ойролцоо өнгөтэй)
    args = [a for a in args if a != '--geo']
    only = args[args.index('--only') + 1] if '--only' in args else None  # (иж бүрдлээс зөвхөн нэг бараа)
    if only: args = args[:args.index('--only')] + args[args.index('--only') + 2:]
    args = [a for a in args if a != '--set']
    name, fbx, color, vprep = args[:4]
    target = int(args[4]) if len(args) > 4 else 10000
    base_name = args[5] if len(args) > 5 else 'man'
    base = read_glb(os.path.join(ROOT, 'assets', f'{base_name}_mesh.glb'))
    tex = np.asarray(Image.open(color).convert('RGB'))
    out_dir = os.path.join(ROOT, 'assets', 'items')
    os.makedirs(out_dir, exist_ok=True)

    pos, tri, uv, uvi = load_dressed(fbx)
    # Иж бүрдэлд дотуур өмд харагдахгүй: саарал/хар өнгө нь гутал, бээлий, бүс
    cloth, skin, tri_col = classify(pos, tri, uv, uvi, tex, underwear=not as_set)
    if geo: cloth, skin = classify_geo(pos, tri, tri_col, base)
    print(f'{len(tri)} гурвалжин: хувцас {cloth.sum()}')
    pos = fix_arms(pos, skin, tri, base)
    parts, regions, boot_ids = None, {}, np.zeros(0, dtype=np.int64)
    if as_set:
        parts, regions = split_set(pos, tri, cloth, tri_col, base)
        if geo and 'gloves' in parts:  # (байрлалаар ангилахад гар арьс: "бээлий" нь ханцуйн ирмэг)
            parts['jacket'] = parts['jacket'] | parts.pop('gloves')
        if 'boots' in parts:
            boot_ids = np.unique(tri[parts['boots']])
            pos = fit_feet(pos, boot_ids, base)
    # Гутлыг түлхэхгүй: доорх хөл нь бүтнээрээ нуугддаг, түлхвэл улын хээ эвдэрнэ
    used = np.setdiff1d(np.unique(tri[cloth]), boot_ids)
    pos[used] = push_out(pos[used], base)

    if as_set:
        # Бараа бүрийн гурвалжны хувь: нийт target-ийг талбайгаар нь хуваана
        total = sum(m.sum() for m in parts.values())
        for part, mask in parts.items():
            if only and part != only: continue
            export_item(f'{name}_{part}', pos, tri, uv, uvi, mask, base, vprep,
                        max(800, int(target * mask.sum() / total)), out_dir, regions.get(part))
    else:
        # Ганц хувцас (цамц, өмд): хүзүүнээс дээш юу ч үгүй (нүд, хөмсөг нь хувцасны өнгөтэй андуурагдаж болно)
        cloth &= pos[tri].mean(1)[:, 1] < base['pos']['Head'][1] + 0.02
        export_item(name, pos, tri, uv, uvi, cloth, base, vprep, target, out_dir)
    # Өнгө: иж бүрдлийн бараанууд нэг зургийг хуваалцана (<name>_color.png)
    # (иж бүрдэл 2048: таван бараа нэг зургийг хуваалцдаг тул бараа бүрийн хэсэг жижиг)
    size = 2048 if as_set else 1024
    Image.open(color).convert('RGB').resize((size, size), Image.LANCZOS).save(os.path.join(out_dir, f'{name}_color.png'), optimize=True)


if __name__ == '__main__':
    main()
