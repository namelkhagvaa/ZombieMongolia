"""Google Play-ийн App Bundle (.aab)-ийн "base" модулийг бүтээнэ (tools/build_android.ps1 -PlayStore).

aapt2 link --proto-format-ийн гаргасан APK-г bundletool-ийн хүлээдэг бүтэц рүү шилжүүлнэ:
    manifest/AndroidManifest.xml, resources.pb, res/..., assets/..., lib/<abi>/*.so, dex/classes.dex
Хэрэглээ: make_aab_module.py <proto.apk> <lib хавтас> <classes.dex> <гаралт base.zip>
"""
import os
import sys
import zipfile


def main():
    proto_apk, lib_dir, dex, output = sys.argv[1:5]
    count = 0
    with zipfile.ZipFile(proto_apk) as source, zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as module:
        for item in source.infolist():
            if item.is_dir():
                continue
            name = item.filename
            if name == "AndroidManifest.xml":
                name = "manifest/AndroidManifest.xml"
            elif not (name == "resources.pb" or name.startswith(("res/", "assets/"))):
                raise SystemExit(f"unexpected entry in proto APK: {name}")
            module.writestr(name, source.read(item))
            count += 1
        # Native сан: lib/<abi>/*.so (bundletool өөрөө шахахгүй, 16 KB-аар зэрэгцүүлнэ)
        for abi in sorted(os.listdir(lib_dir)):
            for so in sorted(os.listdir(os.path.join(lib_dir, abi))):
                module.write(os.path.join(lib_dir, abi, so), f"lib/{abi}/{so}")
                count += 1
        module.write(dex, "dex/classes.dex")
        count += 1
    print(f"AAB module: {output} ({count} files)")


if __name__ == "__main__":
    main()
