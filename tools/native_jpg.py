# Android, Windows-ийн багцад бэлтгэсэн assets хавтасны том PNG текстуруудыг JPG болгоно (tools/build_android.ps1,
# tools/build_installer.ps1 дуудна). Вебийн багцлагчтай (tools/pack_web.py) ижил дүрэм, ижил кэш:
#   - huds/, icons/ доторх тунгалаг жижиг дүрсүүд, window_icon.png PNG хэвээр
#   - бусад *.png -> *.jpg (85 чанар), PNG-г устгана. Тоглоом .png олдохгүй бол .jpg-г ачаална (src/Platform.cpp ImageFile).
#   - sounds/narration/*.wav -> *.ogg (мэдэгдлийн дуу хоолой; бусад дуу WAV хэвээр)
#
#   python tools/native_jpg.py <assets хавтас>
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pack_web import convert  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(ROOT, "build_web", "pack_cache")
KEEP = ("huds/", "icons/")
KEEP_FILES = ("window_icon.png",)


def main():
    folder = sys.argv[1]
    before = after = count = 0
    for base, _, files in os.walk(folder):
        for name in files:
            if not name.endswith(".png") or name in KEEP_FILES:
                continue
            path = os.path.join(base, name)
            rel = os.path.relpath(path, folder).replace("\\", "/")
            if rel.startswith(KEEP):
                continue
            # convert() нь "assets/..." замын дүрэм хэрэглэдэг (KEEP_PNG)
            target, data = convert(path, "assets/" + rel, CACHE)
            if not target.endswith(".jpg"):
                continue
            before += os.path.getsize(path)
            out = path[:-4] + ".jpg"
            open(out, "wb").write(data)
            after += len(data)
            os.remove(path)
            count += 1
    print(f"native_jpg: {count} textures {before / 1e6:.1f} MB -> {after / 1e6:.1f} MB")
    # Мэдэгдлийн дуу хоолой (sounds/narration/*.wav) -> OGG: тоглоом .wav олдохгүй бол .ogg-г ачаална (src/Audio.cpp)
    voices = os.path.join(folder, "sounds", "narration")
    before = after = count = 0
    if os.path.isdir(voices):
        for name in os.listdir(voices):
            if not name.endswith(".wav"):
                continue
            path = os.path.join(voices, name)
            target, data = convert(path, "assets/sounds/narration/" + name, CACHE)
            before += os.path.getsize(path)
            open(path[:-4] + ".ogg", "wb").write(data)
            after += len(data)
            os.remove(path)
            count += 1
    print(f"native_jpg: {count} narration voices {before / 1e6:.1f} MB -> {after / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
