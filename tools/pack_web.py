# Хөтчийн build-ийн файлуудыг багцална (tools/build_web.ps1 дуудна).
#
#   python tools/pack_web.py <staged assets хавтас> <гаралтын хавтас> <manifest.json>
#
# Оролт: CMake-ийн бэлтгэсэн build_web/web_assets (тоглоомд хэрэгтэй файлууд л, хуудасны FS-ийн "/" үндэс).
# Хэмжээг багасгана:
#   - том текстурууд (*.png, HUD/icon-оос бусад) -> JPG (85 чанар; тунгалаг хэсэг нь зөвхөн UV-ийн хоосон зай)
#   - дуу (*.wav) -> OGG Vorbis (тоглоом .wav байхгүй бол .ogg-ийг ачаалдаг, src/Audio.cpp)
# Багцууд (web/shell.html татна):
#   core       - эхлэхэд заавал (цэс, дүрүүд, зэвсэг, газар, дуу, холын мод)
#   world      - унаа, мод, торх: тоглоом эхэлсний дараа ар талд татагдаж, анх аймагт орохдоо ачаалагдана
#   city/<нэр> - аймгийн төв бүрийн газрын зураг: тухайн аймагт орохдоо л
# Багцын нэрэнд агуулгын хэш орно (packs/world.1a2b3c4d.data): өөрчлөгдөөгүй багцыг шинэ хувилбарт дахин
# татахгүй (хөтчийн кэш). Хувиргасан файлуудыг build_web/pack_cache-д хадгалж дараагийн build-д дахин хийхгүй.
#
# Багцын бүтэц: "ZMPK", uint32 JSON-ий урт, JSON [[зам, эхлэл, хэмжээ], ...] (эхлэл нь өгөгдлийн хэсгээс), өгөгдөл.
import hashlib
import io
import json
import os
import struct
import sys

from PIL import Image
import soundfile

KEEP_PNG = ("assets/huds/", "assets/icons/")  # жижиг, тунгалаг дүрсүүд
WORLD = ("assets/cars/", "assets/trees/", "assets/pooshig/")
JPEG_QUALITY = 85
OGG_LEVEL = 0.5  # libsndfile: 0 = хамгийн өндөр чанар, 1 = хамгийн бага хэмжээ


def convert(source, rel, cache):
    """(шинэ зам, өгөгдөл): хөрвүүлэх шаардлагагүй бол эх файл."""
    data = open(source, "rb").read()
    if rel.endswith(".png") and not rel.startswith(KEEP_PNG):
        target, kind = rel[:-4] + ".jpg", "jpg"
    elif rel.endswith(".wav"):
        target, kind = rel[:-4] + ".ogg", "ogg"
    else:
        return rel, data
    key = hashlib.sha1(data).hexdigest()[:16]
    cached = os.path.join(cache, key + "." + kind)
    if os.path.exists(cached):
        return target, open(cached, "rb").read()
    out = io.BytesIO()
    if kind == "jpg":
        image = Image.open(io.BytesIO(data))
        if image.mode in ("RGBA", "LA", "P"):
            image = image.convert("RGBA")
            # Тунгалаг хэсгийг (UV-ийн хоосон зай) хар биш, ойролцоох өнгөөр: шүүлтүүрийн үед ирмэг харлахгүй
            background = image.resize((max(1, image.width // 16), max(1, image.height // 16)), Image.BOX)
            background = background.resize(image.size, Image.BILINEAR)
            background.paste(image, (0, 0), image)
            image = background
        image.convert("RGB").save(out, "JPEG", quality=JPEG_QUALITY, optimize=True, progressive=False)
    else:
        samples, rate = soundfile.read(io.BytesIO(data), dtype="float32")
        soundfile.write(out, samples, rate, format="OGG", subtype="VORBIS", compression_level=OGG_LEVEL)
    os.makedirs(cache, exist_ok=True)
    open(cached, "wb").write(out.getvalue())
    return target, out.getvalue()


def group_of(rel):
    if rel.endswith("_far.bin"):
        return "core"  # холын мод (жижиг): анхны газар нутгийг тоглоом эхлэхэд байгуулдаг (src/Structures.cpp)
    if rel.startswith(WORLD):
        return "world"
    if rel.startswith("assets/city/"):
        return "city/" + os.path.splitext(os.path.basename(rel))[0]
    return "core"


def main():
    staged, out_dir, manifest_path = sys.argv[1:4]
    cache = os.path.join(os.path.dirname(os.path.abspath(staged)), "pack_cache")
    groups = {}
    raw_total = 0
    for folder, _, names in os.walk(staged):
        for name in sorted(names):
            source = os.path.join(folder, name)
            rel = os.path.relpath(source, staged).replace("\\", "/")
            raw_total += os.path.getsize(source)
            path, data = convert(source, rel, cache)
            groups.setdefault(group_of(rel), []).append(("/" + path, data))

    packs_dir = os.path.join(out_dir, "packs")
    os.makedirs(packs_dir, exist_ok=True)
    for old in os.listdir(packs_dir):
        os.remove(os.path.join(packs_dir, old))
    manifest = {}
    total = 0
    for name in sorted(groups):
        files = sorted(groups[name])
        table, offset = [], 0
        for path, data in files:
            table.append([path, offset, len(data)])
            offset += len(data)
        header = json.dumps(table, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        blob = b"ZMPK" + struct.pack("<I", len(header)) + header + b"".join(data for _, data in files)
        digest = hashlib.sha256(blob).hexdigest()[:10]
        file_name = name.replace("/", "_") + "." + digest + ".data"
        open(os.path.join(packs_dir, file_name), "wb").write(blob)
        manifest[name] = {"url": "packs/" + file_name, "size": len(blob)}
        total += len(blob)
        if not name.startswith("city/"):
            print("  %-6s %6.1f MB  %d files" % (name, len(blob) / 1048576, len(files)))
    cities = [m["size"] for n, m in manifest.items() if n.startswith("city/")]
    print("  city   %6.1f MB  %d packs" % (sum(cities) / 1048576, len(cities)))
    print("  total  %6.1f MB (was %.1f MB)" % (total / 1048576, raw_total / 1048576))
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, separators=(",", ":"))


if __name__ == "__main__":
    main()
