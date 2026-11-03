# Гутал: Tripo-оор ганцаар нь (биегүй) үүсгэсэн хос гутлыг үндсэн биеийн тавхайд өмсгөнө.
#   assets/items/<name>_mesh.glb, _color.png, _hide.bin (tools/cloth_import.py-тэй ижил)
#
#   python tools/boots_import.py boots1 assets/cloth/boots/boots-01.glb <vehicleprep.exe> [гурвалжин=5000] [--tint 34,32,30]
# --tint: саарал арьсыг тэр өнгөөр будна (бор ул өнгөө хадгална)
#
# Загвар: хоёр гутал зэрэгцээ босоо, хуруу нь +Z, +X талынх нь зүүн хөлийнх (дүр +Z рүү харахад зүүн гар +X).
# Tripo хэсэгчилсэн загвар (tripo_part_N: ул, түрий, үдээс...) гаргадаг тул бүх хэсгийг нийлүүлж, текстуруудыг нэг
# зурагт багтаана (том хэсгүүд том нүдэнд).
# Гутал бүрийг тавхайн хэмжээ, байрлал, чиглэлд тааруулж (ул: тавхайн төв, хуруу; түрий: шилбэний төв), ясны жинг
# өндрөөр нь өгнө: шагайнаас доош Foot (хурууны хэсэг ToeBase), дээш Leg (шилбэ), дундаа зөөлөн.
import json, os, struct, subprocess, sys, tempfile
import numpy as np
from PIL import Image
from io import BytesIO

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cloth_import import read_glb, face_zone
from mixamo_import import write_glb
from rigid_import import recolor

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')


def load_parts(path):
    data = open(path, 'rb').read()
    n = struct.unpack_from('<I', data, 12)[0]
    js = json.loads(data[20:20 + n])
    binary = data[20 + n + 8:]
    def acc(i):
        a = js['accessors'][i]
        v = js['bufferViews'][a['bufferView']]
        dtype = {5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}[a['componentType']]
        width = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[a['type']]
        arr = np.frombuffer(binary, dtype=dtype, count=a['count'] * width, offset=v.get('byteOffset', 0) + a.get('byteOffset', 0))
        return arr.reshape(a['count'], width) if width > 1 else arr
    parts = []
    for mesh in js['meshes']:
        for prim in mesh['primitives']:
            mat = js['materials'][prim['material']]
            tex = js['textures'][mat['pbrMetallicRoughness']['baseColorTexture']['index']]
            view = js['bufferViews'][js['images'][tex['source']]['bufferView']]
            image = Image.open(BytesIO(binary[view.get('byteOffset', 0):view.get('byteOffset', 0) + view['byteLength']])).convert('RGB')
            parts.append(dict(P=acc(prim['attributes']['POSITION']).astype(np.float64),
                              UV=acc(prim['attributes']['TEXCOORD_0']).astype(np.float64),
                              I=acc(prim['indices']).astype(np.uint32).reshape(-1, 3), image=image))
    return parts


def pack(parts, size=2048):
    """Хэсгүүдийн текстурыг нэг зурагт: 6 том хэсэг дээд 2/3-д (3x2), бусад нь доод 1/3-д жижиг нүднүүдэд."""
    order = sorted(range(len(parts)), key=lambda i: -len(parts[i]['I']))
    atlas = Image.new('RGB', (size, size))
    big, small = order[:6], order[6:]
    cells = []
    bw, bh = size // 3, (size * 2 // 3) // 2
    for k, i in enumerate(big): cells.append((i, (k % 3) * bw, (k // 3) * bh, bw, bh))
    cols = max(1, (len(small) + 1) // 2)
    sw, sh = size // cols, size - 2 * bh
    sh //= 2
    for k, i in enumerate(small): cells.append((i, (k % cols) * sw, 2 * bh + (k // cols) * sh, sw, sh))
    for i, x, y, w, h in cells:
        atlas.paste(parts[i]['image'].resize((w - 4, h - 4), Image.LANCZOS), (x + 2, y + 2))
        uv = np.clip(parts[i]['UV'], 0.0, 1.0)
        parts[i]['UV'] = np.stack([(x + 2 + uv[:, 0] * (w - 4)) / size, (y + 2 + uv[:, 1] * (h - 4)) / size], 1)
    return atlas


def leather(image, tint):
    """Саарал (ханалт багатай) хэсгийг tint арьс болгоно: Tripo арьсыг гэрлийн тусгалтай саарал-цагаанаар буддаг тул
    тоглоомын нарны гэрэлд цагаан харагдана. Өнгөтэй хэсэг (бор ул) өнгөө хадгалж бага зэрэг бараан болно."""
    a = np.asarray(image).astype(np.float64)
    lum = a @ [0.299, 0.587, 0.114]
    sat = a.max(2) - a.min(2)
    rel = lum / max(1.0, np.median(lum[lum > 8]))
    rel = np.where(rel > 1.0, 1.0 + 0.3 * np.log(np.maximum(rel, 1.0)), rel)
    dyed = rel[..., None] * np.array(tint, dtype=np.float64)
    grey = np.clip((40.0 - sat) / 20.0, 0.0, 1.0)[..., None]  # 20-оос бага ханалттай бол бүрэн будна
    # Өнгөтэй хэсэг (шаргал ул, доторлогоо): бараг хар, өнгө нь бага зэрэг үлдэнэ (цайвар толбо харагдахгүй)
    out = grey * dyed + (1.0 - grey) * (0.3 * a + 0.7 * dyed) * 0.8
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))


def foot_frame(P, side):
    s = P[(P[:, 1] < P[:, 1].min() + 0.03) & (side * P[:, 0] > 0)][:, [0, 2]]
    c = s.mean(0)
    d = np.linalg.svd(s - c, full_matrices=False)[2][0]
    if d[1] < 0: d = -d
    return c, d, np.ptp((s - c) @ d)


def main():
    args = sys.argv[1:]
    tint = None
    if '--tint' in args:
        k = args.index('--tint'); tint = [int(v) for v in args[k + 1].split(',')]; args = args[:k] + args[k + 2:]
    name, path, vprep = args[:3]
    target = int(args[3]) if len(args) > 3 else 5000
    base = read_glb(os.path.join(ROOT, 'assets', 'man_mesh.glb'))
    parts = load_parts(path)
    atlas = pack(parts)
    offset = 0
    Ps, UVs, Is = [], [], []
    for p in parts:
        Ps.append(p['P']); UVs.append(p['UV']); Is.append(p['I'] + offset); offset += len(p['P'])
    P, UV, I = np.concatenate(Ps), np.concatenate(UVs), np.concatenate(Is)
    print(f'  {len(parts)} хэсэг, {len(P)} орой, {len(I)} гурвалжин')

    B = base['P']
    out = P.copy()
    boot_top = {}
    for side, foot, leg in ((1, 'LeftFoot', 'LeftLeg'), (-1, 'RightFoot', 'RightLeg')):
        sel = side * P[:, 0] > 0
        cd, dd, ld = foot_frame(P[sel], side)
        cb, db, lb = foot_frame(B, side)
        scale = lb * 1.08 / ld  # гутал тавхайнаас 8% урт
        yaw = np.arctan2(db[0], db[1]) - np.arctan2(dd[0], dd[1])
        q = P[sel].copy()
        q[:, [0, 2]] -= cd
        q[:, 1] -= q[:, 1].min()
        q *= scale
        c, s = np.cos(yaw), np.sin(yaw)
        x, z = q[:, 0].copy(), q[:, 2].copy()
        q[:, 0], q[:, 2] = x * c + z * s, -x * s + z * c
        q[:, [0, 2]] += cb
        # Түрий: шилбэний төвтэй (хөлийн дээд хэсэг) тааруулна; ул тавхайн төвтэй хэвээр
        top = q[:, 1].max()
        band = lambda pts, lo, hi: pts[(pts[:, 1] > lo) & (pts[:, 1] < hi) & (side * pts[:, 0] > 0)]
        shin_b = band(B, top * 0.7, top * 0.9)[:, [0, 2]].mean(0)
        shin_d = band(q, top * 0.7, top * 0.9)[:, [0, 2]].mean(0)
        w = np.clip((q[:, 1] - 0.04) / (top * 0.6 - 0.04), 0.0, 1.0)[:, None]
        q[:, [0, 2]] += w * (shin_b - shin_d)
        # Хэмжээ: Tripo-гийн гутал болхи (ул тавхайнаас 26% урт, 2 дахин өргөн, түрий шилбэнээс 50% зузаан, 38 см өндөр).
        # Ул: тавхайн уртын 112%; түрий: өндөр бүрт шилбэний хэмжээ + 1.2 см (тоглоомд), 30 см өндөр.
        ankle = base['pos'][foot][1]
        sole_b = B[(B[:, 1] < 0.02) & (side * B[:, 0] > 0)]
        sole_d = q[q[:, 1] < 0.02]
        rel = lambda pts: (pts[:, [0, 2]] - cb) @ db
        k_len = np.ptp(rel(sole_b)) * 1.12 / np.ptp(rel(sole_d))
        across = np.array([-db[1], db[0]])
        k_wide = np.ptp((sole_b[:, [0, 2]] - cb) @ across) * 1.15 / np.ptp((sole_d[:, [0, 2]] - cb) @ across)
        xz = q[:, [0, 2]] - cb
        q[:, [0, 2]] = cb + np.outer(xz @ db * min(1.0, k_len), db) + np.outer(xz @ across * min(1.0, k_wide), across)
        k_up = min(1.0, (0.155 - ankle) / (top - ankle))
        q[:, 1] = np.where(q[:, 1] > ankle, ankle + (q[:, 1] - ankle) * k_up, q[:, 1])
        top = q[:, 1].max()
        gap = 0.012 / 1.95
        for y0 in np.arange(ankle, top + 0.01, 0.01):
            shin = band(B, y0 - 0.005, y0 + 0.015)
            m = (q[:, 1] >= y0) & (q[:, 1] < y0 + 0.01)
            if len(shin) < 20 or m.sum() < 20: continue
            c = shin[:, [0, 2]].mean(0)
            for axis in (0, 1):
                want = np.ptp(shin[:, [0, 2]][:, axis]) + 2 * gap
                have = np.ptp(q[m][:, [0, 2]][:, axis])
                k = min(1.0, want / max(have, 1e-6))
                ramp = np.clip((q[m, 1] - ankle) / 0.02, 0.0, 1.0)  # шагайнаас дээш аажим
                col = [0, 2][axis]
                q[m, col] = c[axis] + (q[m, col] - c[axis]) * (1.0 - ramp * (1.0 - k))
        print(f'    урт x{k_len:.2f}, өргөн x{k_wide:.2f}, өндөр x{k_up:.2f}')
        out[sel] = q
        boot_top[side] = top
        print(f'  гутал {side:+d}: томруулалт {scale:.3f}, эргэлт {np.degrees(yaw):.1f}°, түрийн орой {top:.3f} '
              f'(тоглоомд {top * 1.95:.2f} м), түрий {np.round(shin_b - shin_d, 3)} шилжив')
    # Түрийн оройн 1.2 см-ийг (тоглоомд) тайрна: Tripo-гийн гутлын хэл, доторлогоо сэгсгэр шовх ирмэгтэй
    cut = 0.012 / 1.95
    tri_y = out[I][:, :, 1].max(1)
    tri_x = out[I][:, :, 0].mean(1)
    I = I[tri_y < np.where(tri_x > 0, boot_top[1], boot_top[-1]) - cut]
    boot_top = {k: v - cut for k, v in boot_top.items()}
    # (биеийн гадаргуугаас гадагш түлхэхгүй: гутал доорх хөл бүтнээрээ нуугддаг; түлхвэл улын хээ газар руу сунаж эвдэрнэ)

    # Хялбаршуулна
    tmp = tempfile.mkdtemp()
    raw = os.path.join(tmp, name)
    out.astype(np.float32).tofile(raw + '.pos'); UV.astype(np.float32).tofile(raw + '.uv'); I.astype(np.uint32).tofile(raw + '.idx')
    height = float(np.ptp(out[:, 1]))
    env = dict(os.environ, VPREP_HEIGHT='1')
    env.setdefault('VPREP_ERROR', '0.3')
    vm = raw + '.vmesh'
    subprocess.run([vprep, 'prop', raw, vm, str(max(100, target // 3)), f'{height:.6f}'], check=True, env=env, stdout=subprocess.DEVNULL)
    data = open(vm, 'rb').read()
    _, count = struct.unpack_from('<II', data, 4)
    off, at = 12, 0
    Ps, Ns, UVs, Is = [], [], [], []
    for _ in range(count):
        pivot = np.frombuffer(data, np.float32, 3, off); off += 12
        kind, vc, ic = struct.unpack_from('<III', data, off); off += 12
        Ps.append(np.frombuffer(data, np.float32, vc * 3, off).reshape(-1, 3) + pivot); off += vc * 12
        Ns.append(np.frombuffer(data, np.float32, vc * 3, off).reshape(-1, 3)); off += vc * 12
        UVs.append(np.frombuffer(data, np.float32, vc * 2, off).reshape(-1, 2)); off += vc * 8
        Is.append(np.frombuffer(data, np.uint16, ic, off).astype(np.uint32) + at); off += ic * 2; at += vc
    Pm, Nm, UVm, Im = (np.concatenate(x) for x in (Ps, Ns, UVs, Is))

    # Ясны жин: шагайнаас доош тавхай (урд хэсэг нь хуруу), дээш шилбэ
    order = base['order']
    J = np.zeros((len(Pm), 4), np.uint8)
    W = np.zeros((len(Pm), 4), np.float32)
    for side, prefix in ((1, 'Left'), (-1, 'Right')):
        m = side * Pm[:, 0] > 0
        ankle = base['pos'][prefix + 'Foot']
        toe = base['pos'][prefix + 'ToeBase']
        up = np.clip((Pm[m, 1] - (ankle[1] - 0.015)) / 0.045, 0.0, 1.0)   # 0 тавхай .. 1 шилбэ
        fwd_dir = (toe - ankle)[[0, 2]] / np.linalg.norm((toe - ankle)[[0, 2]])
        along = (Pm[m][:, [0, 2]] - toe[[0, 2]]) @ fwd_dir
        toe_w = np.clip(along / 0.02 + 0.5, 0.0, 1.0) * (1.0 - up)         # хурууны үеэс урагш
        J[m, 0], W[m, 0] = order.index(prefix + 'Leg'), up
        J[m, 1], W[m, 1] = order.index(prefix + 'Foot'), (1.0 - up) - toe_w
        J[m, 2], W[m, 2] = order.index(prefix + 'ToeBase'), toe_w
    out_dir = os.path.join(ROOT, 'assets', 'items')
    write_glb(os.path.join(out_dir, f'{name}_mesh.glb'), Pm.astype(np.float32), Nm.astype(np.float32), UVm.astype(np.float32), J, W,
              Im.astype(np.uint32), order, base['parent'], base['pos'])

    # Гутал доорх хөл (түрийн оройгоос доош) бүтнээрээ нуугдана
    c = B[base['I']].mean(1)
    hide = np.zeros(len(c), bool)
    for side in (1, -1):
        hide |= (side * c[:, 0] > 0.02) & (c[:, 1] < boot_top[side] - 0.01)
    hidden = np.where(hide & ~face_zone(base))[0].astype(np.uint32)
    with open(os.path.join(out_dir, f'{name}_hide.bin'), 'wb') as f:
        f.write(struct.pack('<I', len(hidden)) + hidden.tobytes())
    if tint: atlas = leather(atlas, tint)
    atlas.save(os.path.join(out_dir, f'{name}_color.png'), optimize=True)
    print(f'  {name}: {len(Im) // 3} гурвалжин, хөлийн {len(hidden)} гурвалжин нуугдана')


if __name__ == '__main__':
    main()
