# Үндсэн биеийг 4 талаас (урд, зүүн, ар, баруун) ортографик, ижил масштабаар зурна: AI зургийн хэрэгслээр хувцас
# өмсгөөд Tripo-гийн Multiview to 3D-д өгөхөд (хувцастай загвар манай биеийн хэмжээ, байрлалтай гарна).
#
#   python tools/render_views.py [base=man] [px=1024] [out=assets/cloth/base_views]
#
# Програм хангамжийн растержуулагч (numpy): z-buffer, uv-ээр текстур, зөөлөн гэрэлтүүлэг, цагаан дэвсгэр.
import os, sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cloth_import import read_glb
import json, struct

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')


def read_uv(path):
    data = open(path, 'rb').read()
    n = struct.unpack_from('<I', data, 12)[0]
    js = json.loads(data[20:20 + n])
    binary = data[20 + n + 8:]
    a = js['accessors'][js['meshes'][0]['primitives'][0]['attributes']['TEXCOORD_0']]
    v = js['bufferViews'][a['bufferView']]
    return np.frombuffer(binary, np.float32, a['count'] * 2, v.get('byteOffset', 0)).reshape(-1, 2)


def render(P, N, UV, I, tex, right, toward, size):
    """right: дэлгэцийн баруун тэнхлэг (ертөнцөд), toward: камер руу чиглэсэн тэнхлэг."""
    up = np.array([0.0, 1.0, 0.0])
    lo, hi = P[:, 1].min(), P[:, 1].max()
    span = hi - lo
    scale = size * 0.9 / span
    sx = (P @ right) * scale + size / 2
    sy = (hi + span * 0.0555 - P[:, 1]) * scale  # (дээр, доор ижил зай)
    depth = P @ toward
    image = np.full((size, size, 3), 255.0)
    zbuf = np.full((size, size), -1e9)
    light = toward * 0.75 + up * 0.55 + right * 0.25
    light /= np.linalg.norm(light)
    H, W = tex.shape[:2]
    for t in I:
        x, y, z = sx[t], sy[t], depth[t]
        x0, x1 = int(max(0, np.floor(x.min()))), int(min(size - 1, np.ceil(x.max())))
        y0, y1 = int(max(0, np.floor(y.min()))), int(min(size - 1, np.ceil(y.max())))
        if x1 < x0 or y1 < y0: continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        d = (y[1] - y[2]) * (x[0] - x[2]) + (x[2] - x[1]) * (y[0] - y[2])
        if abs(d) < 1e-9: continue
        w0 = ((y[1] - y[2]) * (gx - x[2]) + (x[2] - x[1]) * (gy - y[2])) / d
        w1 = ((y[2] - y[0]) * (gx - x[2]) + (x[0] - x[2]) * (gy - y[2])) / d
        w2 = 1 - w0 - w1
        inside = (w0 >= -1e-4) & (w1 >= -1e-4) & (w2 >= -1e-4)
        if not inside.any(): continue
        zz = w0 * z[0] + w1 * z[1] + w2 * z[2]
        rows, cols = gy.astype(int), gx.astype(int)
        better = inside & (zz > zbuf[rows, cols])
        if not better.any(): continue
        r, c = rows[better], cols[better]
        b0, b1, b2 = w0[better], w1[better], w2[better]
        uv = b0[:, None] * UV[t[0]] + b1[:, None] * UV[t[1]] + b2[:, None] * UV[t[2]]
        col = tex[np.clip((uv[:, 1] * H).astype(int), 0, H - 1), np.clip((uv[:, 0] * W).astype(int), 0, W - 1)]
        n = b0[:, None] * N[t[0]] + b1[:, None] * N[t[1]] + b2[:, None] * N[t[2]]
        n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
        if (n @ toward).mean() < 0: n = -n
        shade = 0.55 + 0.6 * np.clip(n @ light, 0, 1)
        image[r, c] = np.clip(col * shade[:, None], 0, 255)
        zbuf[r, c] = zz[better]
    return Image.fromarray(image.astype(np.uint8))


def main():
    base_name = sys.argv[1] if len(sys.argv) > 1 else 'man'
    size = int(sys.argv[2]) if len(sys.argv) > 2 else 1024
    out = sys.argv[3] if len(sys.argv) > 3 else os.path.join(ROOT, 'assets', 'cloth', 'base_views')
    os.makedirs(out, exist_ok=True)
    glb = os.path.join(ROOT, 'assets', f'{base_name}_mesh.glb')
    g = read_glb(glb)
    UV = read_uv(glb).astype(np.float64)
    tex = np.asarray(Image.open(os.path.join(ROOT, 'assets', f'{base_name}_color.png')).convert('RGB')).astype(np.float64)
    X, Z = np.array([1.0, 0, 0]), np.array([0, 0, 1.0])
    views = {'front': (X, Z), 'left': (-Z, X), 'back': (-X, -Z), 'right': (Z, -X)}  # (дэлгэцийн баруун, камер руу)
    images = []
    for name, (right, toward) in views.items():
        im = render(g['P'], g['N'], UV, g['I'], tex, right, toward, size)
        im.save(os.path.join(out, f'{name}.png'))
        images.append(im)
        print(' ', name)
    sheet = Image.new('RGB', (size * 4, size), 'white')
    for k, im in enumerate(images): sheet.paste(im, (size * k, 0))
    sheet.save(os.path.join(out, 'sheet.png'))
    print(out)


if __name__ == '__main__':
    main()
