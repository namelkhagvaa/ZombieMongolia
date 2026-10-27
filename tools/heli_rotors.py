"""Нисдэг тэрэгний Tripo загвар: чиглэлийг тааруулж (урд = +Z), үндсэн ба сүүлний сэнсийг их биеэс салгах шошго.
python rotors.py <prefix> <rotate: 0 | 90> analyze
python rotors.py <prefix> <rotate> label <blade_y_min> <hub_r> <tail_z_max> <tail_y_min>
Гаралт: <prefix>_r.pos/.uv/.idx (эргүүлсэн), <prefix>_r.labels (гурвалжин бүрд u8: 0 их бие, 1 үндсэн сэнс, 2 сүүлний сэнс),
<prefix>_r.pivots (мөр бүр: kind x y z, эх нэгжээр), <prefix>_labels.png (шалгах зураг)."""
import sys, shutil
import numpy as np
from PIL import Image

prefix, rotate, mode = sys.argv[1], int(sys.argv[2]), sys.argv[3]
pos = np.fromfile(prefix + '.pos', dtype=np.float32).reshape(-1, 3).copy()
uv = np.fromfile(prefix + '.uv', dtype=np.float32).reshape(-1, 2)
idx = np.fromfile(prefix + '.idx', dtype=np.uint32).reshape(-1, 3)
if rotate == 90:
    # урд нь -X: Y тэнхлэгийг тойрон 90° (x' = z, z' = -x)
    x, z = pos[:, 0].copy(), pos[:, 2].copy()
    pos[:, 0], pos[:, 2] = z, -x
tex = np.asarray(Image.open(prefix + '_color.png').convert('RGB')).astype(np.float32)
th, tw = tex.shape[:2]
col = tex[np.clip((uv[:, 1] * th).astype(int), 0, th - 1), np.clip((uv[:, 0] * tw).astype(int), 0, tw - 1)]
cent = pos[idx].mean(axis=1)
tcol = col[idx].mean(axis=1)
bright = tcol.mean(axis=1)
sat = tcol.max(axis=1) - tcol.min(axis=1)
pale = (bright > 150) & (sat < 45)  # цайвар саарал / цагаан (сэнсний далавч)

ymax = pos[:, 1].max()
zmin, zmax = pos[:, 2].min(), pos[:, 2].max()
L = zmax - zmin
print('bounds', pos.min(axis=0), pos.max(axis=0), 'pale tris', pale.sum(), '/', len(idx))

if mode == 'analyze':
    # Үндсэн сэнсний ступиц: сүүлнээс бусад хэсгийн хамгийн өндөр цэгүүд
    front = cent[:, 2] > zmin + 0.25 * L
    top = cent[front][cent[front][:, 1] > ymax - 0.06]
    print('top cluster (x, y, z) mean', top.mean(axis=0), 'n', len(top))
    hub = top.mean(axis=0)
    r = np.hypot(cent[:, 0] - hub[0], cent[:, 2] - hub[2])
    for lo in np.arange(0.15, 0.45, 0.01):
        sel = (cent[:, 1] >= lo) & (cent[:, 1] < lo + 0.01)
        far = sel & (r > 0.2) & front
        print('y %.2f all %7d far %7d farpale %7d' % (lo, sel.sum(), far.sum(), (far & pale).sum()))
    # Сүүлний хэсэг
    tail = cent[:, 2] < zmin + 0.12 * L
    print('tail pale centroid', cent[tail & pale].mean(axis=0) if (tail & pale).any() else None, (tail & pale).sum())
    print('tail y range', cent[tail][:, 1].min(), cent[tail][:, 1].max(), 'x range', cent[tail][:, 0].min(), cent[tail][:, 0].max())
    sys.exit()

blade_y, hub_r, main_z, tail_z, tail_y, tail_x, tail_pale = map(float, sys.argv[4:11])
front = cent[:, 2] > main_z
labels = np.zeros(len(idx), dtype=np.uint8)
# Үндсэн сэнс: далавчны өндрөөс дээш (сүүлнээс бусад), ступицын тэнхлэгийн ойр бол өнгө харгалзахгүй
hubsel = front & (cent[:, 1] > blade_y)
cand = np.where(front)[0]
hub_guess = cent[cand[np.argsort(cent[cand, 1])[-3000:]]].mean(axis=0)  # сүүлнээс бусад хамгийн өндөр хэсэг: ступиц
r = np.hypot(cent[:, 0] - hub_guess[0], cent[:, 2] - hub_guess[2])
main = hubsel
labels[main] = 1
# Сүүлний сэнс: сүүлний үзүүрт, өндөрт, цайвар
tail = (cent[:, 2] < tail_z) & (cent[:, 1] > tail_y) & (cent[:, 0] > tail_x)
if tail_pale > 0: tail &= pale
labels[tail] = 2
mainv = np.unique(idx[labels == 1])
hub = np.array([hub_guess[0], pos[mainv, 1].mean(), hub_guess[2]])
# сэнсний тэнхлэгийн өндөр: далавчны дундаж
tailv = np.unique(idx[labels == 2])
tail_pivot = (pos[tailv].min(axis=0) + pos[tailv].max(axis=0)) / 2 if len(tailv) else np.zeros(3)
print('main tris', (labels == 1).sum(), 'hub', hub, 'tail tris', (labels == 2).sum(), 'tail pivot', tail_pivot,
      'tail extent', (pos[tailv].max(axis=0) - pos[tailv].min(axis=0)) if len(tailv) else None)

out = prefix + '_r'
pos.tofile(out + '.pos')
shutil.copy(prefix + '.uv', out + '.uv')
shutil.copy(prefix + '.idx', out + '.idx')
labels.tofile(out + '.labels')
with open(out + '.pivots', 'w') as f:
    f.write('3 %f %f %f\n' % tuple(hub))
    f.write('4 %f %f %f\n' % tuple(tail_pivot))

# Шалгах зураг: хажуу (z, y) ба дээрээс (z, x); их бие саарал, сэнс улаан, сүүлний сэнс шар
S = 700
mn, mx = pos.min(axis=0), pos.max(axis=0)
W = int((mx[2] - mn[2]) * S) + 40
H1, H2 = int((mx[1] - mn[1]) * S) + 40, int((mx[0] - mn[0]) * S) + 40
img = np.full((H1 + H2, W, 3), 30, dtype=np.uint8)
palette = np.array([[140, 150, 160], [255, 60, 60], [255, 220, 40]], dtype=np.uint8)
order = np.argsort(labels)  # сэнсийг дээр нь зурна
px = (20 + (cent[order, 2] - mn[2]) * S).astype(int)
img[H1 - 20 - ((cent[order, 1] - mn[1]) * S).astype(int), px] = palette[labels[order]]
img[H1 + H2 - 20 - ((cent[order, 0] - mn[0]) * S).astype(int), px] = palette[labels[order]]
Image.fromarray(img).save(prefix + '_labels.png')
