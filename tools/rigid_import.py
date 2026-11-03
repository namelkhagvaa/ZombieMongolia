# Хатуу эдлэл (дуулга, малгай): Tripo-оор ганцаар нь (биегүй) үүсгэсэн загварыг үндсэн биеийн толгойд тааруулж,
# Head ясанд бүрэн (жин 1) бэхэлнэ. Хувцастай загвараас тасдахаас ялгаатай нь нүүр, үс наалдахгүй, хэлбэр нь гажихгүй.
#   assets/items/<name>_mesh.glb, _color.png, _hide.bin (tools/cloth_import.py-тэй ижил)
#
#   python tools/glb_extract.py assets/cloth/helmet/helmet-01.glb tmp/helm
#   python tools/rigid_import.py helmet1 tmp/helm <vehicleprep.exe> [гурвалжин=3000] [--tint 78,84,52]
# --tint: өнгийг тэр өнгөөр будна (Tripo гэрлийн тусгалыг өнгөнд "шатаадаг": саарал металл дуулга тоглоомын нарны
#         гэрэлд цагаан харагдана). Тодролын ялгааг (хээ, сүүдэр) хадгалж, гялалзсан хэсгийг шахна.
#
# Загвар: урд тал +Z, дээд тал +Y (Tripo-гийн анхдагч). Тааруулалт:
#   - Хэвтээ төвийг толгойн (хөмсөгнөөс дээших) төвтэй тааруулна.
#   - Томруулалт: толгойн өргөнөөс эхэлж, толгойн оройн цэгүүд дуулганы дотор (бүрхүүлээс гадагш гараагүй) болтол.
#   - Өндөр: дуулганы орой толгойн оройгоос ~1.5 см (тоглоомын хэмжээгээр 3 см) дээр.
import os, struct, subprocess, sys, tempfile
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cloth_import import read_glb, nearest, face_zone
from mixamo_import import write_glb

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')


def load_raw(prefix):
    P = np.fromfile(prefix + '.pos', np.float32).reshape(-1, 3).astype(np.float64)
    UV = np.fromfile(prefix + '.uv', np.float32).reshape(-1, 2)
    I = np.fromfile(prefix + '.idx', np.uint32)
    color = [prefix + '_color' + e for e in ('.jpg', '.png') if os.path.exists(prefix + '_color' + e)][0]
    return P, UV, I, color


def inside(points, shell, center):
    """Цэг бүр бүрхүүлийн дотор уу: төвөөс тэр чиглэлд хамгийн ойр бүрхүүлийн цэгээс ойрхон (чиглэлээр нь хайна)."""
    d = shell - center
    r = np.linalg.norm(d, axis=1)
    u = d / r[:, None]
    q = points - center
    rq = np.linalg.norm(q, axis=1)
    k = nearest(q / rq[:, None], u)
    close = (u[k] * (q / rq[:, None])).sum(1) > 0.995  # (тэр чиглэлд бүрхүүл байгаа: нүүрний урд, дуулганы ирмэгээс доош биш)
    return close & (rq < r[k])


def recolor(image, tint):
    """Тодролыг (медианаар хэвийн болгож, гялалзсан хэсгийг шахаж) tint өнгөөр будна."""
    a = np.asarray(image).astype(np.float64)
    lum = a @ [0.299, 0.587, 0.114]
    rel = lum / max(1.0, np.median(lum[lum > 8]))
    rel = np.where(rel > 1.0, 1.0 + 0.35 * np.log(np.maximum(rel, 1.0)), rel)  # гялалзсан хэсэг (шатаасан тусгал) зөөлөн
    return Image.fromarray(np.clip(rel[..., None] * np.array(tint, dtype=np.float64), 0, 255).astype(np.uint8))


def main():
    args = sys.argv[1:]
    tint = None
    if '--tint' in args:
        k = args.index('--tint')
        tint = [int(v) for v in args[k + 1].split(',')]
        args = args[:k] + args[k + 2:]
    name, prefix, vprep = args[:3]
    target = int(args[3]) if len(args) > 3 else 3000
    base = read_glb(os.path.join(ROOT, 'assets', 'man_mesh.glb'))
    P, UV, I, color = load_raw(prefix)

    # Толгой: хүзүүнээс дээш, хөмсөгнөөс дээших хэсэг (гавал)
    B = base['P']
    top = B[:, 1].max()
    skull = B[B[:, 1] > top - 0.045]
    head_c = np.array([skull[:, 0].mean(), 0.0, (skull[:, 2].min() + skull[:, 2].max()) / 2])
    head_w = np.ptp(skull[:, 0])

    lo, hi = P.min(0), P.max(0)
    width = hi[0] - lo[0]
    sample = P[:: max(1, len(P) // 60000)]
    scale = head_w * 1.25 / width
    for _ in range(40):
        T = (sample - [(lo[0] + hi[0]) / 2, hi[1], (lo[2] + hi[2]) / 2]) * scale
        T += [head_c[0], top + 0.015, head_c[2]]
        center = np.array([head_c[0], top - 0.05, head_c[2]])
        poke = ~inside(skull, T, center)
        if poke.mean() < 0.01: break
        scale *= 1.03
    print(f'  томруулалт {scale:.4f} (толгойн өргөн {head_w:.3f}, дуулга {width * scale:.3f}), гадагш гарсан {poke.mean() * 100:.1f}%')
    P = (P - [(lo[0] + hi[0]) / 2, hi[1], (lo[2] + hi[2]) / 2]) * scale + [head_c[0], top + 0.015, head_c[2]]

    # Хялбаршуулна (vehicleprep prop; масштаб 1 байхаар өндрийг нь өгнө)
    tmp = tempfile.mkdtemp()
    raw = os.path.join(tmp, name)
    P.astype(np.float32).tofile(raw + '.pos'); UV.tofile(raw + '.uv'); I.tofile(raw + '.idx')
    height = float(P[:, 1].max() - P[:, 1].min())
    env = dict(os.environ, VPREP_HEIGHT='1')
    env.setdefault('VPREP_ERROR', '0.3')
    vm = raw + '.vmesh'
    subprocess.run([vprep, 'prop', raw, vm, str(max(100, target // 3)), f'{height:.6f}'], check=True, env=env, stdout=subprocess.DEVNULL)
    data = open(vm, 'rb').read()
    _, parts = struct.unpack_from('<II', data, 4)
    off = 12
    Ps, Ns, UVs, Is, at = [], [], [], [], 0
    for _ in range(parts):
        pivot = np.frombuffer(data, np.float32, 3, off); off += 12
        kind, vc, ic = struct.unpack_from('<III', data, off); off += 12
        Ps.append(np.frombuffer(data, np.float32, vc * 3, off).reshape(-1, 3) + pivot); off += vc * 12
        Ns.append(np.frombuffer(data, np.float32, vc * 3, off).reshape(-1, 3)); off += vc * 12
        UVs.append(np.frombuffer(data, np.float32, vc * 2, off).reshape(-1, 2)); off += vc * 8
        Is.append(np.frombuffer(data, np.uint16, ic, off).astype(np.uint32) + at); off += ic * 2; at += vc
    Pm, Nm, UVm, Im = (np.concatenate(x) for x in (Ps, Ns, UVs, Is))
    assert abs(np.ptp(Pm[:, 1]) - height) < 0.01

    head = base['order'].index('Head')
    J = np.zeros((len(Pm), 4), np.uint8); J[:, 0] = head
    W = np.zeros((len(Pm), 4), np.float32); W[:, 0] = 1.0
    out_dir = os.path.join(ROOT, 'assets', 'items')
    os.makedirs(out_dir, exist_ok=True)
    write_glb(os.path.join(out_dir, f'{name}_mesh.glb'), Pm.astype(np.float32), Nm.astype(np.float32), UVm.astype(np.float32), J, W,
              Im.astype(np.uint32), base['order'], base['parent'], base['pos'])

    # Дуулга доорх гавлын гурвалжнууд нуугдана (толгой дуулгыг нэвт гарахгүй)
    shell = P[:: max(1, len(P) // 120000)]
    center = np.array([head_c[0], top - 0.05, head_c[2]])
    covered = np.zeros(len(B), bool)
    near_head = B[:, 1] > top - 0.12
    covered[near_head] = inside(B[near_head], shell, center)
    hidden = np.where(covered[base['I']].all(1) & ~face_zone(base))[0].astype(np.uint32)
    with open(os.path.join(out_dir, f'{name}_hide.bin'), 'wb') as f:
        f.write(struct.pack('<I', len(hidden)) + hidden.tobytes())
    image = Image.open(color).convert('RGB').resize((1024, 1024), Image.LANCZOS)
    if tint: image = recolor(image, tint)
    image.save(os.path.join(out_dir, f'{name}_color.png'), optimize=True)
    print(f'  {name}: {len(Im) // 3} гурвалжин, гавлын {len(hidden)} гурвалжин нуугдана')


if __name__ == '__main__':
    main()
