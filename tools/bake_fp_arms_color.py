# 1-р этгээдийн гарын загварт (assets/fp_arms.glb, Tripo) өнгөний текстур үүсгэнэ: загварт зөвхөн normal map байгаа
# (өнгөгүй, бүхэлдээ саарал) тул гурвалжин бүрийг ясны жингээр нь бээлий (гар, хурууны яс) эсвэл ханцуй гэж
# ялгаж будна. Normal map-ийн хазайлтаас (атираа, оёдол, үений хамгаалалт) сүүдрийг өнгөнд шингээнэ.
#
#   python tools/bake_fp_arms_color.py
#
# Гаралт: assets/fp_arms_color.png (tools/prepare_character.ps1-ийн үүсгэдэг нэртэй ижил; тоглоом ачаалдаг)
import io, json, os, struct
import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
SIZE = 1024
GLOVE = np.array([52, 54, 50], float)      # бараан саарал синтетик арьс
GLOVE_PAD = np.array([34, 35, 33], float)  # үений хамгаалалт, хатуу хэсэг (normal-ийн хүчтэй товгор)
SLEEVE = np.array([86, 94, 66], float)     # чидун ногоон цэргийн хүрэм (тоглоомын ханцуйн өнгө)
OTHER = np.array([60, 58, 52], float)      # хөл, гутал (1-р этгээдэд харагдахгүй)


def main():
    path = os.path.join(ROOT, 'assets', 'fp_arms.glb')
    f = open(path, 'rb').read()
    clen = struct.unpack('<I', f[12:16])[0]
    j = json.loads(f[20:20 + clen])
    bo = 20 + clen + 8

    def view(i):
        bv = j['bufferViews'][i]
        s = bo + bv.get('byteOffset', 0)
        return f[s:s + bv['byteLength']]

    def acc(i):
        a = j['accessors'][i]
        bv = j['bufferViews'][a['bufferView']]
        t, sz = {5126: ('f', 4), 5123: ('H', 2), 5125: ('I', 4), 5121: ('B', 1)}[a['componentType']]
        n = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[a['type']]
        s = bo + bv.get('byteOffset', 0) + a.get('byteOffset', 0)
        arr = np.frombuffer(f[s:s + a['count'] * n * sz], dtype='<' + t)
        arr = arr.reshape(a['count'], n) if n > 1 else arr
        if a.get('normalized') and t in ('B', 'H'):
            arr = arr / (255.0 if t == 'B' else 65535.0)
        return arr

    joints = [j['nodes'][i].get('name', '') for i in j['skins'][0]['joints']]
    is_hand = np.array(['Hand' in name for name in joints])
    is_arm = np.array([('Arm' in name or 'Shoulder' in name) and 'Hand' not in name for name in joints])

    normal = None
    for m in j['materials']:
        if 'normalTexture' in m:
            image = j['images'][j['textures'][m['normalTexture']['index']]['source']]
            normal = np.asarray(Image.open(io.BytesIO(view(image['bufferView']))).convert('RGB').resize((SIZE, SIZE)), float) / 255.0

    # Хэсэг бүрийн өнгө (UV зураг дээр), ирмэг хүрээгүй хэсэг ханцуйн өнгөтэй
    base = Image.new('RGB', (SIZE, SIZE), tuple(int(c) for c in SLEEVE))
    draw = ImageDraw.Draw(base)
    counts = {'glove': 0, 'sleeve': 0, 'other': 0}
    for mesh in j['meshes']:
        for p in mesh['primitives']:
            uv = acc(p['attributes']['TEXCOORD_0'])
            ids = acc(p['attributes']['JOINTS_0']).astype(int)
            weights = acc(p['attributes']['WEIGHTS_0']).astype(float)
            hand = (weights * is_hand[ids]).sum(1)
            arm = (weights * is_arm[ids]).sum(1)
            for tri in acc(p['indices']).reshape(-1, 3):
                h, a = hand[tri].mean(), arm[tri].mean()
                if h > 0.5:
                    color, kind = GLOVE, 'glove'
                elif a > 0.5 or h + a > 0.5:
                    color, kind = SLEEVE, 'sleeve'
                else:
                    color, kind = OTHER, 'other'
                counts[kind] += 1
                pts = [(float(uv[v][0] * SIZE), float(uv[v][1] * SIZE)) for v in tri]
                # (1 px томруулна: UV хэсгийн ирмэгт завсар үлдэхгүй)
                cx = sum(x for x, _ in pts) / 3.0
                cy = sum(y for _, y in pts) / 3.0
                grown = [(x + (x - cx) * 0.08 + (0.5 if x > cx else -0.5), y + (y - cy) * 0.08 + (0.5 if y > cy else -0.5)) for x, y in pts]
                draw.polygon(grown, fill=tuple(int(c) for c in color))
    color = np.asarray(base, float)

    if normal is not None:
        # Normal map: хазайлт их (атираа, оёдол, хамгаалалтын ирмэг) газар бараан, хавтгай газар бага зэрэг цайвар
        n = normal * 2.0 - 1.0
        tilt = np.clip(np.sqrt(n[..., 0] ** 2 + n[..., 1] ** 2), 0.0, 1.0)
        light = np.clip(0.5 + 0.5 * (n[..., 0] * -0.4 + n[..., 1] * 0.6 + n[..., 2] * 0.7), 0.0, 1.0)
        shade = (0.72 + 0.45 * light) * (1.0 - 0.45 * tilt)
        is_glove = np.all(np.abs(color - GLOVE) < 1.0, axis=-1)
        color = np.where((is_glove & (tilt > 0.45))[..., None], GLOVE_PAD, color)
        color = color * shade[..., None]
        # Даавууны жижиг ширхэг (ханцуй): normal-ийн өндөр давтамжийн хэлбэлзэл
        color += (n[..., 0:1] * 6.0)
    out = Image.fromarray(np.clip(color, 0, 255).astype(np.uint8))
    target = os.path.join(ROOT, 'assets', 'fp_arms_color.png')
    out.save(target)
    print('triangles', counts, '->', target)


if __name__ == '__main__':
    main()
