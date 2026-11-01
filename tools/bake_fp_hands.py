# 1-р этгээдийн гар: assets/rigged_arms.glb (хэсэг хэсгээрээ тусдаа, ясгүй гар) -> бариул атгасан баруун гар,
# шуу. Хэсэг бүр (алга, хурууны үе, эрхий хуруу) хатуу бие тул үеэр нь эргүүлж бариулыг ороож атгуулна.
#
#   python tools/bake_fp_hands.py [--preview out.png]
#
# Гаралт (assets/fp_hands/): fist_tight.txt (бууны бариул, радиус 1.5 см), fist_round.txt (гранат, 3 см),
# forearm.txt. Мөр бүр нэг гурвалжин: "r g b  x y z  x y z  x y z" (метр).
# Атгасан гарын орон зай (Humanoid.cpp BuildFist-тэй ижил): бариул +Z (долоовор хурууны тал), алга -X талд
# бариул руу (+X) харна, бугуй +Y. Шуу: бугуй эх, тохой +Y чиглэлд.
import json, os, struct, sys
import numpy as np

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
SCALE = 0.0416  # м / нэгж (гарын урт 4.57 нэгж = 19 см)

GLOVE_DARK = (54, 56, 50)
GLOVE_MID = (78, 80, 70)
GLOVE_PAD = (40, 40, 38)
SKIN = (208, 160, 130)
SLEEVE = (86, 94, 70)
CUFF = (64, 70, 52)


def load(path):
    f = open(path, 'rb').read()
    clen = struct.unpack('<I', f[12:16])[0]
    j = json.loads(f[20:20 + clen])
    bo = 20 + clen + 8

    def acc(i):
        a = j['accessors'][i]
        bv = j['bufferViews'][a['bufferView']]
        t, sz = {5126: ('f', 4), 5123: ('H', 2), 5125: ('I', 4)}[a['componentType']]
        n = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3}[a['type']]
        s = bo + bv.get('byteOffset', 0) + a.get('byteOffset', 0)
        arr = np.frombuffer(f[s:s + a['count'] * n * sz], dtype='<' + t)
        return arr.reshape(a['count'], n) if n > 1 else arr

    p = j['meshes'][0]['primitives'][0]
    return acc(p['attributes']['POSITION']).astype(float), acc(p['indices']).reshape(-1, 3)


def pieces(P, I):
    key = {}
    canon = np.array([key.setdefault(tuple(np.round(v, 5)), k) for k, v in enumerate(P)])
    parent = list(range(len(P)))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    for t in I:
        a = find(canon[t[0]])
        for x in t[1:]:
            parent[find(canon[x])] = a
    comp = {}
    for t in I:
        comp.setdefault(find(canon[t[0]]), []).append(P[t])
    return [np.array(v) for v in comp.values()]  # (n, 3, 3) гурвалжнууд


def rot_z(deg, pivot):
    a = np.radians(deg)
    c, s = np.cos(a), np.sin(a)
    R = np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])
    return lambda v: (v - pivot) @ R.T + pivot


def rot_axis(axis, deg, pivot):
    axis = np.asarray(axis, float) / np.linalg.norm(axis)
    a = np.radians(deg)
    K = np.array([[0, -axis[2], axis[1]], [axis[2], 0, -axis[0]], [-axis[1], axis[0], 0]])
    R = np.eye(3) + np.sin(a) * K + (1 - np.cos(a)) * K @ K
    return lambda v: (v - pivot) @ R.T + pivot


def place(tris, start, end, new_start, new_end):
    """Хэсгийг хатуу биеэр шилжүүлнэ: start->end чиглэлийг new_start->new_end руу (хамгийн бага эргэлт)."""
    a = (end - start) / np.linalg.norm(end - start)
    b = (new_end - new_start) / np.linalg.norm(new_end - new_start)
    v = np.cross(a, b)
    c = float(np.dot(a, b))
    if np.linalg.norm(v) < 1e-9:
        R = np.eye(3) if c > 0 else -np.eye(3)
    else:
        K = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
        R = np.eye(3) + K + K @ K * (1.0 / (1.0 + c))
    return (tris - start) @ R.T + new_start


def wrap_next(joint, length, radius, direction):
    """Бариулын тэнхлэг (Z) тойрсон radius тойрог дээрх, joint-оос length зайтай, өнцөг нь өсөх (direction > 0)
    чиглэлд байх дараагийн цэг."""
    angle = np.arctan2(joint[1], joint[0])
    best, best_err = None, 1e9
    for step in np.linspace(0.02, 3.0, 600):
        a = angle + direction * step
        p = np.array([np.cos(a) * radius, np.sin(a) * radius, joint[2]])
        err = abs(np.linalg.norm(p - joint) - length)
        if err < best_err:
            best, best_err = p, err
        if err < 0.0004:
            return p
    return best


def bake(rh, parts):
    """Баруун гар (glb-ийн x > 2.5 бүлэг) -> атгасан гарын орон зай. rh: бариулын радиус (м)."""
    right = [p for p in parts if p[:, :, 0].mean() > 2.5]
    centre_x, knuckle_z, palm_in = 5.2, -1.1, -0.21
    # glb -> гарын орон зай (атгахаас өмнө, хуруу -Y чиглэлд): -Z_g -> -Y_f, -Y_g (алга) -> +X_f, -X_g (долоовор) -> +Z_f
    def to_hand(v):
        x = -(rh + 0.003) - (v[..., 1] - palm_in) * SCALE
        y = (v[..., 2] - knuckle_z) * SCALE - 0.005
        z = (centre_x - v[..., 0]) * SCALE
        return np.stack([x, y, z], axis=-1)

    out = []
    palm = arm = None
    fingers = {}
    thumb = []
    for p in right:
        cx, cz = p[:, :, 0].mean(), p[:, :, 2].mean()
        if cz > 6.9 - 0.1 and p[:, :, 2].min() > 6.0:
            continue  # бугалга (дэлгэцийн гадна)
        if p[:, :, 2].min() > 1.0:
            arm = p  # шуу
            continue
        if cx < 4.2 and p[:, :, 2].max() > -0.3 or (cx < 4.2):
            thumb.append(p)
            continue
        if p[:, :, 2].min() > -1.05:
            palm = p
            continue
        column = int(round((cx - 4.4) / 0.53))  # 0 долоовор .. 3 чигчий
        fingers.setdefault(column, []).append(p)

    hand = [(to_hand(palm), GLOVE_DARK)]
    # Хуруунууд: үе бүрийн эхлэл бариулыг тойрсон тойрог (радиус R) дээр, дараагийн үе рүү чиглэнэ:
    # хуруу бариулыг ороож атгана (тогтмол өнцгөөр нугалахад бариулаас доош унжиж байв)
    R = rh + 0.0085
    colors = (GLOVE_DARK, GLOVE_MID, SKIN)
    for column, segs in sorted(fingers.items()):
        segs = sorted(segs, key=lambda s: -s[:, :, 2].mean())  # ойрын үеэс (z их) үзүүр рүү
        tris = [to_hand(s) for s in segs]
        joint = None
        for k, t in enumerate(tris):
            start = np.array([t[:, :, 0].mean(), t[:, :, 1].max(), t[:, :, 2].mean()])
            end = np.array([t[:, :, 0].mean(), t[:, :, 1].min(), t[:, :, 2].mean()])
            length = np.linalg.norm(end - start) + 0.002
            if joint is None:
                joint = np.array([-(R + 0.002), start[1], start[2]])  # үе мөч: алганы ёроол, бариулын хажууд
            target = wrap_next(joint, length, R, 1.0)
            tris[k] = place(t, start, end, joint, target)
            hand.append((tris[k], colors[min(k, 2)]))
            joint = target
    # Эрхий хуруу: суурь нь алганы долоовор талд, хоёр үе нь бариулын дээгүүр (+X) гарч үзүүр нь доош нугална
    thumb = sorted(thumb, key=lambda s: -s[:, :, 2].mean())
    t_tris = [to_hand(s) for s in thumb]
    z_t = 0.036
    j0 = np.array([-(rh + 0.006), 0.042 if rh < 0.02 else 0.05, z_t])
    for k, t in enumerate(t_tris):
        start = np.array([t[:, :, 0].mean(), t[:, :, 1].max(), t[:, :, 2].mean()])
        end = np.array([t[:, :, 0].mean(), t[:, :, 1].min(), t[:, :, 2].mean()])
        length = np.linalg.norm(end - start) + 0.002
        if k == 0:
            target_start, target_end = start, j0  # суурь: алганаас эрхий хурууны эхлэл хүртэл
        else:
            direction = np.array([0.9, -0.3, -0.05]) if k == 1 else np.array([0.45, -0.85, -0.1])
            target_start = j0
            target_end = j0 + direction / np.linalg.norm(direction) * length
            j0 = target_end
        hand.append((place(t, start, end, target_start, target_end), GLOVE_DARK if k == 0 else GLOVE_MID if k == 1 else SKIN))
    wrist_g = np.array([[centre_x, 0.08, 1.0]])
    wrist = to_hand(wrist_g)[0]
    return hand, arm, wrist


def forearm(arm):
    # Шуу: бугуй (z 1.17) эх, тохой (z 6.74) +Y; хөндлөн огтлол: нуруу (+Y_g) -> +Z (дээш)
    cx = arm[:, :, 0].mean()
    z0 = arm[:, :, 2].min()
    x = (arm[..., 0] - cx) * SCALE * 1.15
    y = (arm[..., 2] - z0) * SCALE
    z = arm[..., 1] * SCALE * 1.35  # (загварын шуу нимгэн: зузааныг нэмнэ)
    return [(np.stack([x, y, z], axis=-1), SLEEVE)]


def write(path, tris):
    with open(path, 'w') as f:
        for t, c in tris:
            for tri in t:
                f.write('%d %d %d  %s\n' % (c[0], c[1], c[2], '  '.join('%.5f %.5f %.5f' % tuple(v) for v in tri)))


def preview(path, hands):
    from PIL import Image, ImageDraw
    img = Image.new('RGB', (1200, 420), (235, 235, 235))
    d = ImageDraw.Draw(img)
    for col, (name, tris, rh) in enumerate(hands):
        for view, (a, b, depth) in enumerate(((0, 1, 2), (2, 1, 0))):
            ox, oy = 150 + col * 600 + view * 300, 200
            sc = 1800
            d.ellipse((ox - rh * sc, oy - rh * sc, ox + rh * sc, oy + rh * sc), outline=(200, 0, 0)) if view == 0 else \
                d.rectangle((ox - 0.06 * sc, oy - rh * sc, ox + 0.06 * sc, oy + rh * sc), outline=(200, 0, 0))
            flat = [(t, c) for ts, c in tris for t in ts]
            flat.sort(key=lambda tc: tc[0][:, depth].mean() * (1 if view == 0 else -1))
            for t, c in flat:
                n = np.cross(t[1] - t[0], t[2] - t[0])
                n = n / (np.linalg.norm(n) + 1e-9)
                k = 0.55 + 0.45 * abs(n[depth])
                d.polygon([(ox + v[a] * sc, oy - v[b] * sc) for v in t], fill=tuple(int(x * k) for x in c), outline=(30, 30, 30))
            d.text((ox - 120, 20), '%s  %s' % (name, 'side (along grip)' if view == 0 else 'front'), fill=(0, 0, 0))
    img.save(path)


def main():
    P, I = load(os.path.join(ROOT, 'assets', 'rigged_arms.glb'))
    parts = pieces(P, I)
    out = os.path.join(ROOT, 'assets', 'fp_hands')
    os.makedirs(out, exist_ok=True)
    results = []
    arm = None
    for name, rh in (('fist_tight', 0.015), ('fist_round', 0.03)):
        hand, arm, wrist = bake(rh, parts)
        write(os.path.join(out, name + '.txt'), hand)
        results.append((name, hand, rh))
        print(name, 'triangles', sum(len(t) for t, _ in hand), 'wrist', np.round(wrist, 4))
    write(os.path.join(out, 'forearm.txt'), forearm(arm))
    if '--preview' in sys.argv:
        preview(sys.argv[sys.argv.index('--preview') + 1], results)


if __name__ == '__main__':
    main()
