# Байшин, гудамж, байгалийн гадаргуугийн бүтцийн текстуруудыг бэлтгэнэ (ambientCG, CC0 1.0):
#   ambientCG-ийн 1K-JPG багцыг татаж (build_web/surface_cache), өнгөний зургийг саарал болгон, дундажийг 0.5,
#   тодролыг тогтмол болгоно -> assets/surfaces/<нэр>.jpg (1024 px, саарал JPG).
# Тоглоом 4 саарал зургийг нэг RGBA текстурт нийлүүлж (src/Environment.cpp LoadSurfaceTextures), оройн өнгөнд
# "бүтэц" болгон үржүүлнэ: өнгө нь байгууламжийнхаа өнгө хэвээр, бодит ширхэг (тоосго, шавардлага, асфальт г.м.) нэмэгдэнэ.
#
# Хэрэглээ:  python tools/prep_surfaces.py
import io
import os
import urllib.request
import zipfile

from PIL import Image, ImageOps, ImageStat

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(ROOT, "build_web", "surface_cache")
OUT = os.path.join(ROOT, "assets", "surfaces")
SIZE = 1024
QUALITY = 72 # саарал бүтэц: 72-оос дээш чанар нүдэнд ялгагдахгүй, хэмжээ ~40% бага

# Тоглоомын нэр -> ambientCG материал. Дараалал нь текстурын суваг (Environment.cpp-тэй ижил):
#   walls  (R шавардлага, G тоосго, B бетон, A банз)
#   roofs  (R дээврийн вааран хавтан, G атираат төмөр, B асфальт, A хавтан зам)
#   nature (R элс, G хайрга, B жижиг чулуу, A хагарсан хөрс)
MATERIALS = [
    ("plaster", "PaintedPlaster017"), ("brick", "Bricks085"), ("concrete", "Concrete034"), ("planks", "WoodFloor043"),
    ("roof_tiles", "RoofingTiles013A"), ("corrugated", "CorrugatedSteel007A"), ("asphalt", "Asphalt033"),
    ("paving", "PavingStones138"),
    ("sand", "Ground080"), ("gravel", "Ground111"), ("pebbles", "Gravel023"), ("cracked", "Ground031"),
]


def color_map(asset):
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, asset + "_1K-JPG.zip")
    if not os.path.exists(path):
        url = f"https://ambientcg.com/get?file={asset}_1K-JPG.zip"
        request = urllib.request.Request(url, headers={"User-Agent": "ZombieMongolia-build"})
        with urllib.request.urlopen(request, timeout=120) as response:
            data = response.read()
        open(path, "wb").write(data)
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        color = Image.open(io.BytesIO(archive.read(next(n for n in names if n.endswith("_Color.jpg"))))).convert("RGB")
        # Гүдгэр (displacement): атираат төмөр, вааран дээвэр, тоосгоны зуурмагийн хэлбэр өнгөнд бараг харагддаггүй
        height = next((n for n in names if n.endswith("_Displacement.jpg")), None)
        height = Image.open(io.BytesIO(archive.read(height))).convert("L") if height else None
        return color, height


def normalized(gray, deviation_target=34.0):
    stat = ImageStat.Stat(gray)
    mean, deviation = stat.mean[0], max(1.0, stat.stddev[0])
    scale = deviation_target / deviation
    return gray.point(lambda v: max(0, min(255, int(round(128 + (v - mean) * scale)))))


def detail(maps):
    # Саарал, SIZE px; дундаж 128, стандарт хазайлт ~34 (материал бүрийн бүтэц ижил хүчтэй харагдана).
    # Гүдгэр байвал өнгөний гэрэлтэй 50/50 холино (хэлбэр нь тод харагдана).
    image, height = maps
    gray = normalized(ImageOps.grayscale(image).resize((SIZE, SIZE), Image.LANCZOS))
    if height is not None:
        gray = Image.blend(gray, normalized(height.resize((SIZE, SIZE), Image.LANCZOS)), 0.5)
    stat = ImageStat.Stat(gray)
    mean, deviation = stat.mean[0], max(1.0, stat.stddev[0])
    scale = 34.0 / deviation
    return gray.point(lambda v: max(0, min(255, int(round(128 + (v - mean) * scale)))))


def main():
    os.makedirs(OUT, exist_ok=True)
    total = 0
    for name, asset in MATERIALS:
        out = os.path.join(OUT, name + ".jpg")
        detail(color_map(asset)).save(out, "JPEG", quality=QUALITY, optimize=True)
        total += os.path.getsize(out)
        print(f"{name:11s} <- {asset:20s} {os.path.getsize(out) // 1024} KB")
    sources = os.path.join(OUT, "SOURCES.txt")
    with open(sources, "w", encoding="utf-8") as f:
        f.write("Zombie Mongolia — гадаргуугийн бүтцийн текстурын эх сурвалж (бүгд CC0 / public domain)\n\n")
        f.write("ambientCG (https://ambientcg.com), CC0 1.0, 1K Color -> саарал бүтэц (tools/prep_surfaces.py):\n")
        for name, asset in MATERIALS:
            f.write(f"  {name}.jpg  <- {asset}\n")
    print(f"total {total / 1e6:.2f} MB")


if __name__ == "__main__":
    main()
