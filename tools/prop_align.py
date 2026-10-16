# tools/glb_extract.py-ийн гаргасан зэвсгийн түүхий өгөгдлийг (<prefix>.pos) тоглоомын чиглэлд оруулна:
#   урт тэнхлэг нь +Z (ам нь +Z тал, бөгс нь z = 0), босоо нь +Y, гол төмрийн тэнхлэг y = 0, x = 0 дээр.
# Tripo-гийн загвар хэвтээ хавтгайд ташуу хэвтэж ирдэг тул XZ хавтгайд гол тэнхлэгийг (PCA) олж эргүүлнэ.
# Амны тал: хоёр үзүүрээс нимгэн (босоо хэмжээ нь бага) нь.
#
# Босоо зогсоосон загвар (гильз, сум): --upright үед +Y тэнхлэгийг +Z болгож (ёроол z = 0), x, y-г төвд нь тавина.
#
# Хэрэглээ: python tools/prop_align.py <prefix> <out_prefix> [muzzle_sample_m | --upright]
#   <out_prefix>.pos шинээр бичигдэнэ; .uv, .idx-ийг хуулна.
import shutil, sys
import numpy as np

source, out = sys.argv[1], sys.argv[2]
upright = len(sys.argv) > 3 and sys.argv[3] == '--upright'
sample = float(sys.argv[3]) if len(sys.argv) > 3 and not upright else 0.05

p = np.fromfile(source + '.pos', dtype=np.float32).reshape(-1, 3).astype(np.float64)

def write(x, y, z):
    np.stack([x, y, z], axis=1).astype(np.float32).tofile(out + '.pos')
    for ext in ('.uv', '.idx'):
        shutil.copyfile(source + ext, out + ext)

if upright:
    # X тэнхлэгийг тойрон -90°: (x, y, z) -> (x, -z, y)
    x, y, z = p[:, 0], -p[:, 2], p[:, 1]
    x, y, z = x - (x.max() + x.min()) / 2, y - (y.max() + y.min()) / 2, z - z.min()
    write(x, y, z)
    print('upright: length %.3f  width %.3f' % (z.max(), x.max() - x.min()))
    sys.exit(0)
center = p[:, [0, 2]].mean(0)
xz = p[:, [0, 2]] - center
_, vectors = np.linalg.eigh(np.cov(xz.T))
d = vectors[:, 1]  # гол тэнхлэг (x, z)
along = xz @ d
low, high = along.min(), along.max()

def height(mask):
    return p[mask, 1].max() - p[mask, 1].min()

# Амны тал нимгэн: тэр тал руу +Z болгоно
if height(along > high - 0.08 * (high - low)) > height(along < low + 0.08 * (high - low)):
    d = -d
# (x, z) -> (r·p, d·p): тодорхойлогч нь +1 тул Y тэнхлэгийг тойрсон эргэлт (толин тусгал биш)
r = np.array([d[1], -d[0]])
x = xz @ r
z = xz @ d
y = p[:, 1].copy()

# Бөгс z = 0; гол төмрийн тэнхлэг: амны ойролцоох оройнуудын дундаж (x) ба медиан (y; урд хараа дээш цухуйдаг)
z -= z.min()
muzzle = z > z.max() - sample
x -= x[muzzle].mean()
y -= np.median(y[muzzle])

write(x, y, z)
print('length %.3f  x %.3f..%.3f  y %.3f..%.3f  (axis %.1f deg)' % (z.max(), x.min(), x.max(), y.min(), y.max(),
                                                                     np.degrees(np.arctan2(d[1], d[0]))))
