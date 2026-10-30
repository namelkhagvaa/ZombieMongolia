# Мэдэгдлийн дуу хоолой (дэлгэцэд гарах мэдэгдлийг уншина: src/Audio.cpp Announce) -> assets/sounds/narration/<key>.wav
#
# Nomad Labs, Nomad Voice (https://developers.nomadlabs.mn) Монгол text-to-speech. Төлбөртэй (кредит):
#   1) API түлхүүрийг ЗӨВХӨН орчны хувьсагчид тавина (чатад, файлд, git-д бүү бич):
#        PowerShell:  $env:NOMAD_API_KEY = "..."     (тухайн цонхонд л)
#   2) python tools/gen_narration.py --voices                 хоолойнуудыг жагсаана
#   3) python tools/gen_narration.py --voice nlv_... --estimate   зардлыг тооцно (төлбөргүй)
#   4) python tools/gen_narration.py --voice nlv_... --yes        үүсгэнэ (төлбөртэй; аль хэдийн байгааг алгасна)
# Ажлын ID-г build_web/narration_jobs.json-д хадгална: тасарвал ижил Idempotency-Key-ээр үргэлжилж, давхар төлбөр гарахгүй.
import argparse
import io
import json
import os
import sys
import time
import urllib.error
import urllib.request
import uuid

import soundfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "sounds", "narration")
STATE = os.path.join(ROOT, "build_web", "narration_jobs.json")
API = "https://api.nomadlabs.mn/v1"

MEMBERS = ["Дайчин нэг", "Дайчин хоёр", "Мэргэн буудагч"]  # src/Team.cpp TeamMemberName (уншихад тоог үгээр)

LINES = {
    # Дэлхийн үйл явдал
    "airdrop": "Нисэх онгоц ачаа хаялаа! Сум, гранат, эм байна. Ногоон утааг хай.",
    "crash": "Урд талд сүйрсэн машин байна. Ачааг нь ав!",
    "horde": "Урд талд зомбины бөөгнөрөл байна. Болгоомжтой!",
    "survivor": "Аврах хүн байна! Газрын зураг дээрх тэмдэг рүү оч.",
    "crate": "Ачаа авлаа. Сум, гранат, эм нэмэгдлээ.",
    "rescued": "Аврагдлаа! Танай багт нэгдлээ.",
    "night": "Шөнө боллоо. Болгоомжтой.",
    "dawn": "Үүр цайлаа.",
    "faster": "Зомбинууд хурдсалаа!",
    "fastest": "Зомбинууд хамгийн хурдан боллоо!",
    "medkit": "Эмийн хайрцаг унав! Газрын зураг дээрх улаан загалмайг хай.",
    "full_health": "Амь бүрэн сэргэлээ.",
    # Унаа, нисдэг тэрэг, хуягт машин
    "land_first": "Эхлээд газардана уу.",
    "wait_pilot": "Нисгэгч газардтал хүлээнэ үү.",
    "heli_gunner": "Нисдэг тэргэнд буудагчаар суулаа. Нисгэгчийг дагана.",
    "heli_controls": "Нисдэг тэрэг. Өндөр авах товчийг дараад жолоодоорой.",
    "heli_hit": "Нисдэг тэрэг оногдлоо! Унаж байна!",
    "heli_crash": "Нисдэг тэрэг сүйрлээ!",
    "horse_dead": "Морь үхлээ!",
    "vehicle_broken": "Унаа эвдэрлээ!",
    "fuel_out": "Бензин дууслаа! Шатахуун түгээх газарт очиж бензин хийнэ үү.",
    "driver_down": "Жолооч унасан тул унаанаас буулаа.",
    "armor_drive": "Хуягт машин. Та жолоодно, ойрын цэрэг пулемётод сууна.",
    "armor_no_driver": "Жолоодох цэрэг ойролцоо алга. Жолоочоор сууна уу.",
    # Зэрэг
    "rank_up": "Баяр хүргэе! Таны зэрэг ахилаа.",
}
for i, name in enumerate(MEMBERS):
    LINES[f"member_drives_{i}"] = f"{name} жолоодож байна, та буудна."
    LINES[f"you_drive_{i}"] = f"Та жолоодож байна, {name} буудна."
    LINES[f"gunner_seated_{i}"] = f"{name} буудагчаар суулаа."
    LINES[f"armor_gunner_{i}"] = f"Хуягт машин. Та пулемётод, {name} жолоодож ойрын зомби руу явна."


def request(method, path, key, body=None, idempotency=None):
    data = json.dumps(body).encode("utf-8") if body is not None else None
    req = urllib.request.Request(API + path, data=data, method=method)
    req.add_header("Authorization", "Bearer " + key)
    req.add_header("User-Agent", "ZombieMongolia-narration/1.0 (python)")  # Cloudflare анхдагч python-urllib-ийг хаадаг
    req.add_header("Accept", "application/json")
    if data is not None:
        req.add_header("Content-Type", "application/json")
    if idempotency:
        req.add_header("Idempotency-Key", idempotency)
    for attempt in range(6):
        try:
            with urllib.request.urlopen(req, timeout=60) as response:
                return response.status, json.loads(response.read() or b"null")
        except urllib.error.HTTPError as error:
            payload = error.read().decode("utf-8", "replace")
            if error.code in (429, 503) or error.code >= 500:
                wait = float(error.headers.get("Retry-After") or min(20, 5 * 2 ** attempt))
                time.sleep(wait)
                continue
            raise SystemExit(f"API {method} {path}: HTTP {error.code} {payload[:300]}")
    raise SystemExit(f"API {method} {path}: олон удаа оролдоод бүтсэнгүй")


def load_state():
    try:
        return json.load(open(STATE, encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def save_state(state):
    os.makedirs(os.path.dirname(STATE), exist_ok=True)
    json.dump(state, open(STATE, "w", encoding="utf-8"), ensure_ascii=False, indent=1)


def pending(state, voice):
    out = []
    for key, text in LINES.items():
        done = state.get(key, {})
        if os.path.exists(os.path.join(OUT, key + ".wav")) and done.get("text") == text and done.get("voice") == voice:
            continue
        out.append((key, text))
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--voices", action="store_true", help="хоолойнуудыг жагсаах")
    parser.add_argument("--voice", help="хоолойн ID (nlv_...)")
    parser.add_argument("--estimate", action="store_true", help="зардлыг тооцох (төлбөргүй)")
    parser.add_argument("--yes", action="store_true", help="төлбөртэй үүсгэлтийг зөвшөөрөх")
    parser.add_argument("--sample", help="зөвхөн энэ мэдэгдлийг жишээ болгон үүсгэх (build_web/narration_samples, тоглоомд орохгүй)")
    args = parser.parse_args()

    key = os.environ.get("NOMAD_API_KEY", "").strip()
    if not key:
        raise SystemExit("NOMAD_API_KEY орчны хувьсагч алга. PowerShell: $env:NOMAD_API_KEY = \"...\" (чатад бүү бич)")

    if args.voices:
        page = 1
        while True:
            _, result = request("GET", f"/voices?page={page}", key)
            for v in result.get("data", []):
                print(f"{v['id']}  {v.get('name', '')}  ({v.get('gender', '')}, {'Pro' if v.get('is_pro') else 'энгийн'})  "
                      f"{(v.get('description') or '')[:70]}")
            if len(result.get("data", [])) < result.get("page_size", 50):
                break
            page += 1
        return

    if not args.voice:
        raise SystemExit("--voice nlv_... заана уу (--voices-оор жагсаана)")
    state = load_state()
    todo = pending(state, args.voice)
    out_folder = OUT
    if args.sample:
        if args.sample not in LINES:
            raise SystemExit("Ийм мэдэгдэл алга: " + args.sample)
        # Хоолой сонгох жишээ: тусдаа төлөв, тусдаа хавтас (тоглоомын бичлэгүүдийг хөндөхгүй)
        global STATE
        STATE = os.path.join(ROOT, "build_web", "narration_samples.json")
        out_folder = os.path.join(ROOT, "build_web", "narration_samples")
        state = load_state()
        tag = args.voice[-8:]
        todo = [] if os.path.exists(os.path.join(out_folder, f"{args.sample}_{tag}.wav")) else [(f"{args.sample}_{tag}", LINES[args.sample])]
    _, caps = request("GET", "/capabilities", key)
    multiplier = float(caps.get("credit_multiplier", 1) or 1)
    chars = sum(len(text) for _, text in todo)
    print(f"{len(todo)} мэдэгдэл үүсгэх, {chars} тэмдэгт, ойролцоогоор {chars * multiplier:.0f} кредит "
          f"(үржүүлэгч {multiplier}); {len(LINES) - len(todo)} нь аль хэдийн бэлэн")
    if args.estimate or not todo:
        return
    if not args.yes:
        raise SystemExit("Төлбөртэй үүсгэлт: зардлыг харсны дараа --yes нэмж ажиллуулна уу")

    os.makedirs(out_folder, exist_ok=True)
    for name, text in todo:
        entry = state.get(name, {})
        if entry.get("text") != text or entry.get("voice") != args.voice or "idempotency" not in entry:
            entry = {"text": text, "voice": args.voice, "idempotency": str(uuid.uuid4())}
            state[name] = entry
            save_state(state)  # POST-оос өмнө: тасарвал ижил түлхүүрээр давтана
        if "job" not in entry:
            payload = {"voice_id": args.voice, "text": text, "stability": 0.5, "similarity": 0.75, "pacing": "normal",
                       "speed": 1, "language_code": "mn", "variants": 1, "title": "Zombie Mongolia: " + name}
            _, job = request("POST", "/generations", key, payload, entry["idempotency"])
            job = job[0] if isinstance(job, list) else job
            entry["job"] = job["id"]
            save_state(state)
        # Төлөв: 5 секунд тутам, 3 минут хүртэл
        status = ""
        for _ in range(36):
            _, info = request("GET", f"/generations/{entry['job']}", key)
            status = info.get("status", "")
            if status in ("completed", "failed", "needs_review"):
                break
            time.sleep(5)
        if status != "completed":
            print(f"{name}: төлөв {status or 'хүлээгдэж байна'} — дараа дахин ажиллуулбал үргэлжилнэ")
            continue
        _, audio = request("GET", f"/generations/{entry['job']}/audio", key)
        download = urllib.request.Request(audio["url"], headers={"User-Agent": "ZombieMongolia-narration/1.0 (python)"})
        with urllib.request.urlopen(download, timeout=60) as response:
            mp3 = response.read()
        samples, rate = soundfile.read(io.BytesIO(mp3), dtype="float32", always_2d=True)
        soundfile.write(os.path.join(out_folder, name + ".wav"), samples.mean(axis=1), rate, subtype="PCM_16")
        print(f"{name}: {len(samples) / rate:.1f} с")
    if args.sample:
        print("жишээ:", os.path.join(out_folder, todo[0][0] + ".wav") if todo else "аль хэдийн бий")
        return
    sources = os.path.join(OUT, "SOURCES.txt")
    with open(sources, "w", encoding="utf-8") as f:
        f.write("Мэдэгдлийн дуу хоолой: Nomad Labs, Nomad Voice (nomad-voice-v1) text-to-speech, "
                "https://developers.nomadlabs.mn (tools/gen_narration.py: MP3 -> моно 16 бит WAV)\n")


if __name__ == "__main__":
    main()
