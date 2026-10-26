"""Бизнесийн дүрэм: бүртгэл, нэвтрэлт, аймаг, Gold, дэлгүүр, нэхэмжлэх, төлбөрийн эрх олголт, лог.

Бүх алдаа GameError (код + монгол мессеж + HTTP төлөв): тоглоом кодоор нь ялгаж, мессежийг харуулна.
"""
import base64
import hashlib
import hmac
import json
import re
import secrets
import time
from datetime import timedelta

from django.conf import settings as django_settings
from django.contrib.auth.hashers import make_password
from django.db import transaction
from django.utils import timezone

from . import qpay, sms
from .models import (AuthToken, Cashout, Device, Event, GameSettings, GoldTransaction, Invoice, Player, Room, UnlockedProvince,
                     Verification)

VERIFICATION_TTL = timedelta(minutes=5)  # verify.mn-ийн сесс 5 минут
SMS_PER_HOUR = 5             # нэг дугаарт цагт эхлүүлэх баталгаажуулалтын дээд тоо
LOGIN_FAILURES_LIMIT = 10    # 15 минутад буруу нэвтрэх оролдлогын дээд тоо
MIN_PASSWORD = 6

ROOM_TIMEOUT = timedelta(minutes=10)  # дохио (beat) ийм хугацаанд ирээгүй бол холболт тасарсан (шийтгэлгүй)
ROOM_JOINABLE = timedelta(minutes=3)  # зочныг зөвхөн сүүлд дохио өгсөн хостын өрөөнд оруулна
TICKET_TTL = 6 * 3600               # тасалбар: relay руу (дахин) холбогдох хугацаа, секунд
BATTLE_WAIT = timedelta(seconds=60)   # нэг тал л мэдэгдвэл (нөгөө нь тасарсан, AI тулалдсан) хүлээх хугацаа
MAX_STAGE = 2000                      # өрөөний даалгаврын ахицын мөрийн урт
WEB_DEVICE = "web-topup"              # вэбээр (Gold цэнэглэх хуудас) нэвтэрсэн токен: төхөөрөмж биш
DEFAULT_GOLD_PACKS = [[300, 3000], [550, 5000], [1200, 10000]]  # [Gold, ₮]: admin-аас өөрчилнө
# Gold татах: тоглогчийн сонгох банкууд
CASHOUT_BANKS = ["Хаан банк", "Голомт банк", "Худалдаа хөгжлийн банк", "Төрийн банк", "Хас банк", "Капитрон банк",
                 "Богд банк", "Ариг банк", "Транс банк", "М банк", "Үндэсний хөрөнгө оруулалтын банк", "Чингис хаан банк"]

# Газрын зургийн дэлгүүр: нэр -> (Player-ийн талбар, GameSettings-ийн үнийн талбар, дээд түвшин).
# Дээд түвшин тоглоомын src/Shop.h, src/Weapon.h-тай ижил.
UPGRADES = {
    "damage": ("damage", "price_damage", 5),
    "fire_rate": ("fire_rate", "price_fire_rate", 5),
    "magazine": ("magazine", "price_magazine", 5),
    "health": ("health_level", "price_health", 5),
    "soldier": ("soldiers", "price_soldier", 10),  # цэрэг: тоо (дээд тал 10), нэг бүр ижил үнэтэй, алдвал хасагдана
}


class GameError(Exception):
    def __init__(self, code, message, status=400):
        super().__init__(message)
        self.code = code
        self.message = message
        self.status = status


# ---------------------------------------------------------------- Туслахууд

def normalize_phone(raw):
    """Монгол гар утасны 8 оронтой дугаар -> +976XXXXXXXX."""
    digits = re.sub(r"\D", "", raw or "")
    if len(digits) == 11 and digits.startswith("976"):
        digits = digits[3:]
    if not re.fullmatch(r"[5-9]\d{7}", digits):
        raise GameError("bad_phone", "Утасны дугаар 8 оронтой байх ёстой")
    return "+976" + digits


def mask_phone(phone):
    """+97699112233 -> +976****2233"""
    return phone[:4] + "*" * (len(phone) - 8) + phone[-4:] if len(phone) > 8 else phone


def validate_device(device_id):
    if not re.fullmatch(r"[A-Za-z0-9_-]{8,64}", device_id or ""):
        raise GameError("bad_device", "Төхөөрөмжийн мэдээлэл буруу байна")
    return device_id


def log_event(player, kind, data=None, device_id="", ip=None):
    Event.objects.create(player=player, kind=kind, data=data or {}, device_id=device_id or "", ip=ip)


def issue_token(player, device_id):
    return AuthToken.objects.create(key=AuthToken.new_key(), player=player, device_id=device_id)


def device_ok(player, device_id):
    """Энэ төхөөрөмж дансны идэвхтэй (бүртгэлтэй) төхөөрөмжүүдийн нэг эсэх."""
    return player.active_devices().filter(device_id=device_id).exists()


def bind_device(player, device_id, platform, ip):
    """Дансанд сул байр (GameSettings.max_devices) байвал энэ төхөөрөмжийг үнэгүй бүртгэнэ. Дүүрсэн бол юу ч хийхгүй
    (тоглогч төхөөрөмж солих төлбөр төлнө)."""
    if device_ok(player, device_id):
        return False
    if player.active_devices().count() >= GameSettings.load().max_devices:
        return False
    device = Device.objects.filter(device_id=device_id).first()
    if device is not None and device.player_id != player.pk:
        return False
    if device is None:
        Device.objects.create(device_id=device_id, player=player, platform=(platform or "")[:16])
    else:
        Device.objects.filter(pk=device.pk).update(active=True, bound_at=timezone.now())
    log_event(player, "device_bound", {"platform": (platform or "")[:16]}, device_id, ip)
    return True


def revoke_other_sessions(player, keep=None):
    """Нэг зэрэг нэг л төхөөрөмж: бусад нэвтрэлтийг хүчингүй болгоно (тэр төхөөрөмж дараагийн хүсэлтэд гарна)."""
    others = player.tokens.filter(revoked=False)
    if keep is not None:
        others = others.exclude(pk=keep.pk)
    return others.update(revoked=True)


def account_state(player, device_id):
    settings = GameSettings.load()
    if expire_cashouts(player):
        player.refresh_from_db()  # хугацаа дууссан татах хүсэлтийн Gold буцсан
    return {
        "name": player.name,
        "phone": mask_phone(player.phone),
        # Бүх аймаг үнэгүй (тоглоомын хуучин хувилбар ч бүгдийг нээлттэй гэж харна)
        "provinces": list(range(settings.province_count)),
        "free_left": 0,
        "credits": 0,
        "gold": player.gold,
        "gold_locked": player.locked_gold,
        "gold_packs": gold_packs(),
        "cashout": cashout_info(player),
        "room_entry": settings.room_fee + settings.room_stake,
        "skin_price": settings.skin_price,  # (хуучин тоглоом: бүх онцгой дүр энэ үнээр харагдана)
        "skin_prices": {str(skin): skin_price(settings, skin) for skin in sorted(PREMIUM_SKINS)},
        "premium_skins": sorted(PREMIUM_SKINS),
        "coop_reward": settings.coop_reward,
        "coop_daily_limit": settings.coop_daily_limit,
        "upgrades": {name: getattr(player, field) for name, (field, _, _) in UPGRADES.items()},
        "shop": {name: {"price": getattr(settings, price), "max": top} for name, (_, price, top) in UPGRADES.items()},
        "device_ok": device_ok(player, device_id),
        "capital": settings.capital_index,
        "max_devices": settings.max_devices,
        "prices": {
            "pack": settings.pack_price,
            "pack_size": settings.pack_size,
            "device_change": settings.device_change_price,
            "capital": settings.capital_price,
        },
    }


def _start_verification(phone, purpose, payload, device_id, ip):
    """Урвуу SMS баталгаажуулалт эхлүүлнэ: тоглогч өөрийн утаснаас код илгээх заавар буцаана."""
    since = timezone.now() - timedelta(hours=1)
    if Verification.objects.filter(phone=phone, created__gte=since).count() >= SMS_PER_HOUR:
        raise GameError("too_many_codes", "Хэт олон удаа оролдсон байна. Хэсэг хүлээгээд дахин оролдоно уу", 429)
    verification = Verification(phone=phone, purpose=purpose, payload=payload, expires=timezone.now() + VERIFICATION_TTL)
    try:
        instruction = sms.verifier().start(verification)
    except sms.SmsError as error:
        log_event(None, "sms_failed", {"phone": phone, "error": str(error)[:300]}, device_id, ip)
        raise GameError("sms_unavailable", "Утас баталгаажуулах үйлчилгээ түр ажиллахгүй байна", 503)
    verification.save()
    log_event(None, f"{purpose}_verification_started", {"phone": phone}, device_id, ip)
    return verification, instruction


def _take_verified(verification_id, purpose):
    """Тоглогч мессежээ илгээсэн (баталгаажсан) бол баталгаажуулалтыг транзакц дотор түгжиж авна."""
    verification = Verification.objects.select_for_update().filter(pk=verification_id, purpose=purpose, used=False).first()
    if verification is None or verification.expires < timezone.now():
        raise GameError("code_expired", "Хугацаа дууссан. Дахин эхлүүлнэ үү", 410)
    try:
        verified = sms.verifier().is_verified(verification)
    except sms.SmsError:
        raise GameError("sms_unavailable", "Утас баталгаажуулах үйлчилгээ түр ажиллахгүй байна", 503)
    if not verified:
        raise GameError("not_verified", "Мессеж хүлээж байна", 409)
    return verification


# ---------------------------------------------------------------- Бүртгэл, нэвтрэлт

def start_registration(name, phone_raw, password, device_id, platform, ip):
    name = (name or "").strip()
    if not 2 <= len(name) <= 60:
        raise GameError("bad_name", "Нэр 2-60 тэмдэгт байна")
    if len(password or "") < MIN_PASSWORD:
        raise GameError("bad_password", f"Нууц үг {MIN_PASSWORD}-аас дээш тэмдэгт байна")
    phone = normalize_phone(phone_raw)
    validate_device(device_id)
    if Player.objects.filter(phone=phone).exists():
        raise GameError("phone_taken", "Энэ дугаар бүртгэлтэй байна. Нэвтэрнэ үү", 409)
    if Device.objects.filter(device_id=device_id).exists():
        raise GameError("device_taken", "Энэ төхөөрөмж өөр дансанд бүртгэлтэй байна", 409)
    payload = {"name": name, "password": make_password(password), "device_id": device_id, "platform": (platform or "")[:16]}
    return _start_verification(phone, Verification.REGISTER, payload, device_id, ip)


def confirm_registration(verification_id, ip):
    """Тоглогч мессежээ илгээсний дараа (тоглоом давтан асууна) дансыг үүсгэнэ."""
    with transaction.atomic():
        verification = _take_verified(verification_id, Verification.REGISTER)
        data = verification.payload
        # Код хүлээх хооронд өөр хүн бүртгүүлсэн эсэх
        if Player.objects.filter(phone=verification.phone).exists():
            raise GameError("phone_taken", "Энэ дугаар бүртгэлтэй байна. Нэвтэрнэ үү", 409)
        if Device.objects.filter(device_id=data["device_id"]).exists():
            raise GameError("device_taken", "Энэ төхөөрөмж өөр дансанд бүртгэлтэй байна", 409)
        start = GameSettings.load().start_gold
        player = Player.objects.create(name=data["name"], phone=verification.phone, password=data["password"], gold=start,
                                       bonus_gold=start)  # эхлэлийн Gold: урамшуулал (татагдахгүй)
        GoldTransaction.objects.create(player=player, amount=start, balance=start, reason=GoldTransaction.SIGNUP,
                                       ref=f"signup:{player.pk}")
        Device.objects.create(device_id=data["device_id"], player=player, platform=data.get("platform", ""))
        verification.used = True
        verification.save(update_fields=["used"])
        token = issue_token(player, data["device_id"])
        log_event(player, "registered", {"platform": data.get("platform", "")}, data["device_id"], ip)
    return player, token


def login(phone_raw, password, device_id, platform, ip):
    phone = normalize_phone(phone_raw)
    validate_device(device_id)
    since = timezone.now() - timedelta(minutes=15)
    if Event.objects.filter(kind="login_failed", data__phone=phone, created__gte=since).count() >= LOGIN_FAILURES_LIMIT:
        raise GameError("too_many_attempts", "Хэт олон удаа буруу оруулсан. 15 минутын дараа дахин оролдоно уу", 429)
    player = Player.objects.filter(phone=phone).first()
    if player is None or not player.check_password(password or ""):
        log_event(player, "login_failed", {"phone": phone}, device_id, ip)
        raise GameError("bad_login", "Утас эсвэл нууц үг буруу байна", 401)
    if player.blocked:
        raise GameError("blocked", "Энэ данс хаагдсан байна", 403)
    device = Device.objects.filter(device_id=device_id).first()
    if device is not None and device.player_id != player.pk:
        raise GameError("device_taken", "Энэ төхөөрөмж өөр дансанд бүртгэлтэй байна", 409)
    bind_device(player, device_id, platform, ip)
    token = issue_token(player, device_id)
    replaced = revoke_other_sessions(player, keep=token)
    log_event(player, "login", {"platform": (platform or "")[:16], "device_ok": device_ok(player, device_id),
                                "replaced_sessions": replaced}, device_id, ip)
    return player, token


def web_login(phone_raw, password, ip):
    """Gold цэнэглэх вэб хуудас: төхөөрөмж бүртгэхгүй, тоглоом дээрх нэвтрэлтийг гаргахгүй. Энэ токеноор зөвхөн
    данс харах, Gold-ийн нэхэмжлэх үүсгэнэ (бусад нь төхөөрөмж шаарддаг)."""
    phone = normalize_phone(phone_raw)
    since = timezone.now() - timedelta(minutes=15)
    if Event.objects.filter(kind="login_failed", data__phone=phone, created__gte=since).count() >= LOGIN_FAILURES_LIMIT:
        raise GameError("too_many_attempts", "Хэт олон удаа буруу оруулсан. 15 минутын дараа дахин оролдоно уу", 429)
    player = Player.objects.filter(phone=phone).first()
    if player is None or not player.check_password(password or ""):
        log_event(player, "login_failed", {"phone": phone, "web": True}, WEB_DEVICE, ip)
        raise GameError("bad_login", "Утас эсвэл нууц үг буруу байна", 401)
    if player.blocked:
        raise GameError("blocked", "Энэ данс хаагдсан байна", 403)
    token = issue_token(player, WEB_DEVICE)
    log_event(player, "web_login", {}, WEB_DEVICE, ip)
    return player, token


def gold_packs():
    packs = GameSettings.load().gold_packs or DEFAULT_GOLD_PACKS
    return [{"index": i, "gold": int(gold), "price": int(price)} for i, (gold, price) in enumerate(packs)]


def start_password_reset(phone_raw, device_id, ip):
    phone = normalize_phone(phone_raw)
    if not Player.objects.filter(phone=phone).exists():
        raise GameError("unknown_phone", "Энэ дугаар бүртгэлгүй байна", 404)
    return _start_verification(phone, Verification.RESET, {}, device_id, ip)


def confirm_password_reset(verification_id, password, ip):
    if len(password or "") < MIN_PASSWORD:
        raise GameError("bad_password", f"Нууц үг {MIN_PASSWORD}-аас дээш тэмдэгт байна")
    with transaction.atomic():
        verification = _take_verified(verification_id, Verification.RESET)
        player = Player.objects.select_for_update().get(phone=verification.phone)
        player.set_password(password)
        player.save(update_fields=["password"])
        player.tokens.all().delete()  # бүх төхөөрөмжөөс гаргана
        verification.used = True
        verification.save(update_fields=["used"])
        log_event(player, "password_reset", {}, "", ip)
    return player


# ---------------------------------------------------------------- Аймаг

def _validate_province(province):
    settings = GameSettings.load()
    if not isinstance(province, int) or not 0 <= province < settings.province_count:
        raise GameError("bad_province", "Аймаг буруу байна")
    return settings


def claim_province(player, device_id, province, ip):
    """Бүх аймаг үнэгүй: тоглоомын хуучин хувилбарт зориулж (аймаг нээх цонх) амжилттай хариулна."""
    _validate_province(province)
    if not device_ok(player, device_id):
        raise GameError("device_change_required", "Энэ төхөөрөмж дээр тоглохын тулд төхөөрөмж солих төлбөр төлнө", 402)
    return player, None


# Онцгой дүрүүд (тоглоомын leaderSkin дугаар) ба үнийн тохиргооны талбар: тэдгээрээр аймаг эхлүүлэх бүрд
PREMIUM_SKINS = {3: "skin_price", 4: "skin_price_iron"}  # DEMON, IRON


def skin_price(settings, skin):
    field = PREMIUM_SKINS.get(skin)
    return getattr(settings, field) if field else 0


def start_province(player, device_id, province, ip, skin=0):
    """Аймаг эхлэхэд үргэлж өрөө (найзын кодтой) нээнэ. room_fee + room_stake-ээс их Gold-той бол нээлттэй (тулаан),
    эс бөгөөс зөвхөн найз кодоор орно, Gold өөрчлөгдөхгүй. Онцгой дүрээр (skin) бол эхлээд skin_price."""
    settings = _validate_province(province)
    if not device_ok(player, device_id):
        raise GameError("device_change_required", "Энэ төхөөрөмж дээр тоглохын тулд төхөөрөмж солих төлбөр төлнө", 402)
    with transaction.atomic():
        locked = Player.objects.select_for_update().get(pk=player.pk)
        _drop_rooms(locked, device_id, ip)  # өмнөх өрөө (апп унасан г.м.): холболт тасарсан гэж үзнэ
        locked.refresh_from_db()            # (барьцаа суллагдсан)
        price = skin_price(settings, skin)
        if price:
            if locked.gold - locked.locked_gold < price:
                raise GameError("not_enough_gold", f"Энэ дүрээр тоглоход {price} Gold хэрэгтэй", 402)
            change_gold(locked, -price, GoldTransaction.SKIN, f"skin:{locked.pk}:{time.time_ns()}",
                        {"province": province, "skin": skin})
        # Үргэлж өрөө (найзын кодтой: хамт тоглолт үнэгүй). Gold хүрэлцвэл нээлттэй (өөр тоглогч орж тулалдана),
        # эс бөгөөс зөвхөн найз кодоор орно.
        pvp = locked.gold - locked.locked_gold >= settings.room_fee + settings.room_stake
        room = Room.objects.create(province=province, host=locked, host_seen=timezone.now(), pvp=pvp, code=_new_room_code())
        if pvp:
            _enter_room(locked, room, settings, "host")
            room.host_stake = settings.room_stake
            room.save(update_fields=["host_stake"])
        log_event(locked, "province_start", {"province": province, "room": room.pk, "skin": skin}, device_id, ip)
    return room


# ---------------------------------------------------------------- Нээлттэй өрөө

def _new_room_code():
    """Нээлттэй өрөөнүүдийн дунд давтагдашгүй 4 оронтой код."""
    used = set(Room.objects.filter(status=Room.OPEN).values_list("code", flat=True))
    for _ in range(50):
        code = f"{secrets.randbelow(9000) + 1000}"
        if code not in used:
            return code
    return f"{secrets.randbelow(9000) + 1000}"


def room_ticket(room, player, role):
    """Relay серверт өрөөнд холбогдох тасалбар: base64url(JSON).hex(HMAC-SHA256). server/relay.js шалгана."""
    payload = json.dumps({"r": room.pk, "p": player.pk, "role": role, "exp": int(time.time()) + TICKET_TTL},
                         separators=(",", ":"))
    body = base64.urlsafe_b64encode(payload.encode()).decode().rstrip("=")
    signature = hmac.new(django_settings.RELAY_SECRET.encode(), body.encode(), hashlib.sha256).hexdigest()
    return f"{body}.{signature}"


def room_json(room, player):
    role = "host" if room.host_id == player.pk else "guest"
    return {"id": room.pk, "province": room.province, "role": role, "host": room.host.name, "battle": room.battle,
            "ticket": room_ticket(room, player, role), "stage": room.stage_state, "recruits": room.recruits,
            "code": room.code, "pvp": room.pvp, "coop": room.guest_coop}


def _enter_room(locked, room, settings, seat):
    """Орох төлбөр (ledger) ба барьцаа. locked: түгжсэн тоглогч. seat: "host" эсвэл "b<тулааны дугаар>" (нэг тоглогч
    нэг өрөөнд дахин ороход дахин төлнө)."""
    change_gold(locked, -settings.room_fee, GoldTransaction.ROOM_FEE, f"fee:{room.pk}:{locked.pk}:{seat}", {"room": room.pk})
    locked.locked_gold += settings.room_stake
    locked.save(update_fields=["locked_gold"])


def _release(player_id, amount):
    if amount:
        locked = Player.objects.select_for_update().get(pk=player_id)
        locked.locked_gold = max(0, locked.locked_gold - amount)
        locked.save(update_fields=["locked_gold"])


def _close(room, reason):
    """Өрөөг хааж, үлдсэн барьцааг (шатаагаагүй) эздэд нь буцаана."""
    _release(room.host_id, room.host_stake)
    if room.guest_id:
        _release(room.guest_id, room.guest_stake)
    room.host_stake = room.guest_stake = 0
    room.status = Room.CLOSED
    room.closed_reason = reason
    room.closed = timezone.now()
    room.save()


def _reset_battle(room):
    room.host_report = room.guest_report = ""
    room.report_at = None
    room.guest_left = False


def _drop_guest(room):
    _release(room.guest_id, room.guest_stake)
    room.guest = None
    room.guest_stake = 0
    room.guest_seen = None
    room.guest_coop = False
    _reset_battle(room)
    room.save()


def expire_rooms():
    """Дохио тасарсан өрөө, зочдыг шийтгэлгүй гаргана (холболт тасрахыг санаатай гарсанд тооцохгүй)."""
    stale = timezone.now() - ROOM_TIMEOUT
    for room in Room.objects.select_for_update().filter(status=Room.OPEN, host_seen__lt=stale):
        _close(room, "timeout")
    # Тулааны үеэр санаатай гарсан зочны багийг AI тулалдуулж байгаа: үр дүнг хост мэдэгдэнэ (хостын дохио
    # тасарвал дээр өрөө бүхэлдээ шийтгэлгүй хаагдана).
    for room in Room.objects.select_for_update().filter(status=Room.OPEN, guest__isnull=False, guest_seen__lt=stale,
                                                        guest_left=False):
        _drop_guest(room)


def _drop_rooms(locked, device_id, ip):
    for room in Room.objects.select_for_update().filter(status=Room.OPEN, host=locked):
        _close(room, "replaced")
        log_event(locked, "room_closed", {"room": room.pk, "reason": "replaced"}, device_id, ip)
    for room in Room.objects.select_for_update().filter(status=Room.OPEN, guest=locked):
        _drop_guest(room)


def _own_room(player, room_id):
    room = Room.objects.select_for_update().filter(pk=room_id, status=Room.OPEN).first() if isinstance(room_id, int) else None
    if room is None or player.pk not in (room.host_id, room.guest_id):
        raise GameError("room_closed", "Өрөө хаагдсан байна", 410)
    return room


def join_room(player, device_id, ip):
    """Урилгагүй орох: сүүлд дохио өгсөн, зочингүй нээлттэй өрөөг (өөрийнхөөс бусад) сонгоно."""
    settings = GameSettings.load()
    if not device_ok(player, device_id):
        raise GameError("device_change_required", "Энэ төхөөрөмж дээр тоглохын тулд төхөөрөмж солих төлбөр төлнө", 402)
    entry = settings.room_fee + settings.room_stake
    with transaction.atomic():
        expire_rooms()  # (тусдаа: доорх алдаа гарсан ч хуучирсан өрөөнүүд хаагдсан хэвээр)
    with transaction.atomic():
        locked = Player.objects.select_for_update().get(pk=player.pk)
        _drop_rooms(locked, device_id, ip)
        locked.refresh_from_db()
        if locked.gold - locked.locked_gold < entry:
            raise GameError("not_enough_gold", f"Өрөөнд орохын тулд {entry} Gold хэрэгтэй", 402)
        room = (Room.objects.select_for_update()
                .filter(status=Room.OPEN, pvp=True, guest__isnull=True, host_seen__gte=timezone.now() - ROOM_JOINABLE)
                .exclude(host=locked).order_by("created").first())
        if room is None:
            raise GameError("no_rooms", "Одоогоор нээлттэй өрөө алга. Дараа дахин оролдоно уу", 404)
        _enter_room(locked, room, settings, f"b{room.battle + 1}")
        room.guest = locked
        room.guest_stake = settings.room_stake
        room.guest_seen = timezone.now()
        room.battle += 1  # зочин орж ирмэгц тулаан эхэлнэ
        _reset_battle(room)
        room.save()
        log_event(locked, "room_joined", {"room": room.pk, "province": room.province}, device_id, ip)
    return room


def join_coop(player, device_id, code, ip):
    """Найзын кодоор: тэр өрөөнд хамт тоглоно (тулаангүй, төлбөр, барьцаагүй)."""
    if not device_ok(player, device_id):
        raise GameError("device_change_required", "Энэ төхөөрөмж дээр тоглохын тулд төхөөрөмж солих төлбөр төлнө", 402)
    code = str(code or "").strip()
    if not re.fullmatch(r"\d{4}", code):
        raise GameError("bad_code", "Код 4 оронтой байна")
    with transaction.atomic():
        expire_rooms()
    with transaction.atomic():
        locked = Player.objects.select_for_update().get(pk=player.pk)
        room = (Room.objects.select_for_update()
                .filter(status=Room.OPEN, code=code, host_seen__gte=timezone.now() - ROOM_JOINABLE).first())
        if room is None:
            raise GameError("no_room", "Ийм кодтой өрөө алга (найз тань аймгаа эхлүүлсэн эсэхийг шалгаарай)", 404)
        if room.host_id == locked.pk:
            raise GameError("own_room", "Энэ таны өөрийн өрөө байна", 409)
        if room.guest_id:
            raise GameError("room_full", "Өрөөнд аль хэдийн өөр тоглогч байна", 409)
        _drop_rooms(locked, device_id, ip)
        room.guest = locked
        room.guest_stake = 0
        room.guest_seen = timezone.now()
        room.guest_coop = True
        _reset_battle(room)
        room.save()
        log_event(locked, "coop_joined", {"room": room.pk, "province": room.province, "host": room.host_id}, device_id, ip)
    return room


def room_beat(player, room_id):
    with transaction.atomic():
        room = _own_room(player, room_id)
        if room.host_id == player.pk:
            room.host_seen = timezone.now()
            room.save(update_fields=["host_seen"])
        else:
            room.guest_seen = timezone.now()
            room.save(update_fields=["guest_seen"])
    return room


def room_phase(room):
    """WAITING/MISSION (хост ганцаараа даалгавар хийж, зочин хүлээж байна), BATTLE (зочин орсон), COMPLETED (сүүлийн
    даалгавар биелсэн), CLOSED (хост гарсан, холболт тасарсан). Хүснэгтэд тусдаа талбаргүй: status, guest-ээс."""
    if room.status == Room.CLOSED:
        return "completed" if room.closed_reason == "completed" else "closed"
    return "battle" if room.guest_id else "mission"


def room_state(room):
    """Хост, зочинд: одоогийн тулаан (зочин орсон бол), зочны нэр."""
    return {"battle": room.battle if room.guest_id and not room.guest_coop else 0,
            "guest": room.guest.name if room.guest_id else None, "coop": room.guest_coop}


def save_stage(player, room_id, stage, recruits):
    """Хост өрөөний даалгаврын ахицыг илгээнэ (шинэ хост үргэлжлүүлнэ)."""
    if not isinstance(stage, str) or len(stage) > MAX_STAGE or not re.fullmatch(r"[0-9 ]*", stage):
        raise GameError("bad_stage", "Буруу хүсэлт")
    if not isinstance(recruits, int) or isinstance(recruits, bool) or not 0 <= recruits <= 50:
        raise GameError("bad_stage", "Буруу хүсэлт")
    with transaction.atomic():
        room = _own_room(player, room_id)
        if room.host_id != player.pk:
            raise GameError("not_host", "Зөвхөн хост", 403)
        room.stage_state = stage
        room.recruits = recruits
        room.host_seen = timezone.now()
        room.save(update_fields=["stage_state", "recruits", "host_seen"])


def _pair_battles_today(winner, loser):
    since = timezone.now() - timedelta(days=1)
    return GoldTransaction.objects.filter(player=winner, reason=GoldTransaction.BATTLE_WIN, created__gte=since,
                                          data__opponent=loser.pk).count()


def _settle(room, winner_role, device_id, ip):
    """Тулааны үр дүн: ялагдагчийн барьцаа ялагчид (хосын хязгаараас хэтэрвэл хэнд ч биш: зүгээр суллагдана).
    Ялагдагч өрөөнөөс гарна; зочин ялбал шинэ хост болно (ахиц, ялагдагчийн цэргүүд өрөөнд үлдэнэ)."""
    settings = GameSettings.load()
    winner_id = room.host_id if winner_role == "host" else room.guest_id
    loser_id = room.guest_id if winner_role == "host" else room.host_id
    stake = room.guest_stake if winner_role == "host" else room.host_stake
    winner = Player.objects.select_for_update().get(pk=winner_id)
    loser = Player.objects.select_for_update().get(pk=loser_id)
    paid = _pair_battles_today(winner, loser) < settings.battle_pair_limit
    loser.locked_gold = max(0, loser.locked_gold - stake)
    loser.save(update_fields=["locked_gold"])
    if paid and stake:
        amount = min(stake, loser.gold)
        # Ялагдагчийн урамшууллын Gold (эхлэлийн, шагнал) ялагчид урамшуулал хэвээр очно (татагдахгүй)
        bonus_part = min(amount, loser.bonus_gold)
        change_gold(loser, -amount, GoldTransaction.BATTLE_LOSS, f"battle:{room.pk}:{room.battle}:loss",
                    {"room": room.pk, "opponent": winner.pk})
        change_gold(winner, amount, GoldTransaction.BATTLE_WIN, f"battle:{room.pk}:{room.battle}:win",
                    {"room": room.pk, "opponent": loser.pk, "bonus": bonus_part}, bonus_part=bonus_part)
    if winner_role == "host":
        room.guest = None
        room.guest_stake = 0
        room.guest_seen = None
    else:
        # Зочин ялав: шинэ хост. Өмнөх хостын барьцаа тооцогдсон; ахиц, цэргүүд (recruits) өрөөнд хэвээр.
        room.host = winner
        room.host_stake = room.guest_stake
        room.host_seen = timezone.now()
        room.guest = None
        room.guest_stake = 0
        room.guest_seen = None
    _reset_battle(room)
    room.save()
    log_event(winner, "battle_won", {"room": room.pk, "battle": room.battle, "opponent": loser.pk, "paid": paid},
              device_id, ip)
    log_event(loser, "battle_lost", {"room": room.pk, "battle": room.battle, "opponent": winner.pk, "paid": paid})


def report_battle(player, device_id, room_id, battle, winner, ip):
    """Тулаанд оролцогч бүр үр дүнг (ялагч: host|guest) мэдэгдэнэ. Хоёулаа ижил бол шийдэгдэнэ; зөрвөл хүчингүй
    (зочин гарна, барьцаа нь суллагдана). Нэг л тал мэдэгдсэн бол BATTLE_WAIT-ийн дараа (давтан мэдэгдэхэд)
    тэр үр дүнгээр. Буцаах: (шийдэгдсэн эсэх, ялагч, өрөө)."""
    if winner not in ("host", "guest") or not isinstance(battle, int) or isinstance(battle, bool):
        raise GameError("bad_result", "Буруу хүсэлт")
    with transaction.atomic():
        room = Room.objects.select_for_update().filter(pk=room_id).first() if isinstance(room_id, int) else None
        if room is None or room.battle != battle:
            raise GameError("battle_over", "Тулаан дууссан", 410)
        if room.guest_coop:
            raise GameError("not_battle", "Хамт тоглолтод тулаан байхгүй", 409)
        if room.status == Room.OPEN and not room.guest_id and room.host_id == player.pk:
            # Аль хэдийн шийдэгдсэн, энэ тоглогч хост (ялсан эсвэл зочноос шинэ хост болсон): өрөөг буцаана
            return True, None, room
        if room.status != Room.OPEN:
            # Өрөө хаагдсан (хост сүүлийн даалгаврыг дуусгасан, холболт тасарсан): тулаан хүчингүй, барьцаа буцсан
            raise GameError("room_closed", "Өрөө хаагдсан: тулаан хүчингүй", 410)
        if not room.guest_id or player.pk not in (room.host_id, room.guest_id):
            # Аль хэдийн шийдэгдсэн, энэ тоглогч өрөөнд байхгүй (ялагдсан эсвэл хүчингүй)
            raise GameError("battle_over", "Тулаан дууссан", 410)
        role = "host" if room.host_id == player.pk else "guest"
        if role == "host" and not room.host_report:
            room.host_report = winner
        elif role == "guest" and not room.guest_report:
            room.guest_report = winner
        room.report_at = room.report_at or timezone.now()
        room.save()
        if room.host_report and room.guest_report:
            if room.host_report != room.guest_report:
                log_event(player, "battle_disputed", {"room": room.pk, "battle": room.battle,
                                                      "host": room.host_report, "guest": room.guest_report}, device_id, ip)
                _drop_guest(room)
                return True, "void", room
            _settle(room, room.host_report, device_id, ip)
            return True, winner, room
        if timezone.now() - room.report_at >= BATTLE_WAIT:
            reported = room.host_report or room.guest_report
            _settle(room, reported, device_id, ip)
            return True, reported, room
    return False, None, room


def leave_room(player, device_id, room_id, ip):
    """Санаатай гарах. Тулааны үеэр (зочин байгаа): хост гарвал зочин ялна (шинэ хост болно); зочин гарвал түүний багийг
    AI тулалдуулж, үр дүнг хост мэдэгдэнэ. Тулаангүй үед хост гарвал ялагдал: барьцаа нь хэнд ч очихгүй шатна."""
    with transaction.atomic():
        room = _own_room(player, room_id)
        if room.guest_id and room.guest_coop:
            # Хамт тоглолт: шийтгэлгүй. Зочин гарвал өрөө хэвээр, хост гарвал (барьцаатай бол шатаж) хаагдана.
            if room.guest_id == player.pk:
                _drop_guest(room)
                log_event(player, "coop_left", {"room": room.pk}, device_id, ip)
                return
            _drop_guest(room)
        if room.guest_id and room.guest_id == player.pk:
            room.guest_left = True
            room.save(update_fields=["guest_left"])
            log_event(player, "room_left", {"room": room.pk, "battle": room.battle}, device_id, ip)
            return
        if room.guest_id:
            _settle(room, "guest", device_id, ip)
            log_event(player, "room_left", {"room": room.pk, "battle": room.battle}, device_id, ip)
            return
        locked = Player.objects.select_for_update().get(pk=player.pk)
        if room.host_id == locked.pk:
            stake = room.host_stake
            if stake:
                locked.locked_gold = max(0, locked.locked_gold - stake)
                locked.save(update_fields=["locked_gold"])
                change_gold(locked, -min(stake, locked.gold), GoldTransaction.ROOM_LEAVE, f"leave:{room.pk}:{locked.pk}",
                            {"room": room.pk})
                room.host_stake = 0
            _close(room, "host_left")
        else:
            _drop_guest(room)
        log_event(locked, "room_left", {"room": room.pk}, device_id, ip)


def complete_room(player, device_id, room_id, ip):
    """Хост сүүлийн даалгаврыг биелүүлэв: +stage_bonus, барьцаа суллагдана, өрөө хаагдана."""
    settings = GameSettings.load()
    with transaction.atomic():
        room = _own_room(player, room_id)
        if room.host_id != player.pk:
            raise GameError("not_host", "Зөвхөн хост үе шатыг дуусгана", 403)
        if timezone.now() - room.created < timedelta(minutes=settings.stage_min_minutes):
            raise GameError("too_fast", "Үе шат хэт хурдан дууслаа", 409)
        locked = Player.objects.select_for_update().get(pk=player.pk)
        if room.pvp:  # (өрөөний төлбөр төлсөн)
            change_gold(locked, settings.stage_bonus, GoldTransaction.STAGE_COMPLETE, f"complete:{room.pk}", {"room": room.pk})
        if room.guest_id and room.guest_coop and settings.coop_reward:
            # Найзтайгаа хамт давсан: хоёуланд нь (урамшуулал), тоглогч бүр өдөрт coop_daily_limit удаа хүртэл
            since = timezone.now() - timedelta(days=1)
            for member in (locked, Player.objects.select_for_update().get(pk=room.guest_id)):
                done = GoldTransaction.objects.filter(player=member, reason=GoldTransaction.COOP_WIN, created__gte=since).count()
                if done < settings.coop_daily_limit:
                    change_gold(member, settings.coop_reward, GoldTransaction.COOP_WIN, f"coop:{room.pk}:{member.pk}",
                                {"room": room.pk})
        _close(room, "completed")
        log_event(locked, "room_completed", {"room": room.pk, "province": room.province}, device_id, ip)


# ---------------------------------------------------------------- Gold, дэлгүүр

def change_gold(locked, amount, reason, ref, data=None, bonus_part=0):
    """Gold-ийг өөрчилж бүртгэлд бичнэ. locked: transaction.atomic() дотор select_for_update()-ээр түгжсэн тоглогч.
    Ижил ref өмнө нь бичигдсэн бол юу ч хийхгүй (False). Үлдэгдэл хүрэлцэхгүй бол GameError.
    Хожсон, урамшууллын хэсгийг GoldTransaction.split_gold тооцно (bonus_part: тулааны хожлын урамшууллаас ирсэн нь)."""
    if GoldTransaction.objects.filter(ref=ref).exists():
        return False
    if locked.gold + amount < 0:
        raise GameError("not_enough_gold", "Gold хүрэлцэхгүй байна", 402)
    locked.gold, locked.won_gold, locked.bonus_gold = GoldTransaction.split_gold(
        locked.gold, locked.won_gold, locked.bonus_gold, amount, reason, bonus_part)
    locked.save(update_fields=["gold", "won_gold", "bonus_gold"])
    GoldTransaction.objects.create(player=locked, amount=amount, balance=locked.gold, reason=reason, ref=ref,
                                   data=data or {})
    return True


def shop_buy(player, device_id, item, level, ip):
    """Газрын зургийн дэлгүүр: item-ийн дараагийн түвшинг Gold-оор авна. level нь тоглоомын харж буй одоогийн түвшин:
    таарахгүй бол (давхар дарсан, өөр төхөөрөмж дээр авсан) дахин авахгүй. Давтагдсан хүсэлт (түвшин аль хэдийн
    level + 1) нь Gold хасахгүйгээр амжилттай."""
    if item not in UPGRADES or not isinstance(level, int) or isinstance(level, bool):
        raise GameError("bad_item", "Буруу хүсэлт")
    if not device_ok(player, device_id):
        raise GameError("device_change_required", "Энэ төхөөрөмж дээр тоглохын тулд төхөөрөмж солих төлбөр төлнө", 402)
    field, price_field, top = UPGRADES[item]
    with transaction.atomic():
        locked = Player.objects.select_for_update().get(pk=player.pk)
        current = getattr(locked, field)
        ref = f"shop:{locked.pk}:{item}:{level + 1}"
        if item == "soldier":
            ref += f":{locked.soldiers_lost}"  # алдаад дахин авсан цэрэг өмнөх худалдаатай давхцахгүй
        if current == level + 1 and GoldTransaction.objects.filter(ref=ref).exists():
            return locked
        if current != level:
            raise GameError("shop_changed", "Дэлгүүрийн мэдээлэл шинэчлэгдлээ. Дахин оролдоно уу", 409)
        if current >= top:
            raise GameError("max_level", "Хамгийн дээд түвшинд хүрсэн", 409)
        price = getattr(GameSettings.load(), price_field)
        cost = price if item == "soldier" else price * (current + 1)
        if locked.gold - locked.locked_gold < cost:
            raise GameError("not_enough_gold", "Gold хүрэлцэхгүй байна (нэг хэсэг нь өрөөнд барьцаалагдсан)", 402)
        change_gold(locked, -cost, GoldTransaction.SHOP, ref, {"item": item, "level": current + 1})
        setattr(locked, field, current + 1)
        locked.save(update_fields=[field])
        log_event(locked, "shop_buy", {"item": item, "level": current + 1, "gold": cost}, device_id, ip)
    return locked


def lose_soldiers(player, device_id, remaining, ip):
    """Тоглолтод Gold-оор авсан цэрэг үхэхэд шууд хасна. remaining: тоглоомын тооцсон үлдсэн тоо (зөвхөн буурна, давтан
    илгээсэн хүсэлт дахин хасахгүй)."""
    if not isinstance(remaining, int) or isinstance(remaining, bool) or remaining < 0:
        raise GameError("bad_request", "Буруу хүсэлт")
    with transaction.atomic():
        locked = Player.objects.select_for_update().get(pk=player.pk)
        if remaining < locked.soldiers:
            lost = locked.soldiers - remaining
            locked.soldiers = remaining
            locked.soldiers_lost += lost
            locked.save(update_fields=["soldiers", "soldiers_lost"])
            log_event(locked, "soldier_lost", {"lost": lost, "left": remaining}, device_id, ip)
    return locked


# ---------------------------------------------------------------- Gold татах

def withdrawable(player):
    """Татаж болох Gold: хожсон Gold, гэхдээ өрөөнд барьцаалснаас бусад нь."""
    return max(0, min(player.won_gold, player.gold - player.locked_gold))


def cashout_json(cashout):
    return {"id": cashout.pk, "gold": cashout.gold, "amount": cashout.amount, "bank": cashout.bank,
            "account": cashout.account, "holder": cashout.holder, "status": cashout.status, "note": cashout.note,
            "created": cashout.created.isoformat() if cashout.created else None}


def cashout_info(player):
    settings = GameSettings.load()
    active = player.cashouts.filter(status__in=Cashout.OPEN).order_by("-created").first()
    return {"enabled": settings.cashout_enabled, "min": settings.cashout_min, "rate": float(settings.cashout_rate),
            "withdrawable": withdrawable(player), "won": player.won_gold, "banks": CASHOUT_BANKS,
            "active": cashout_json(active) if active else None}


def start_cashout(player, device_id, gold, bank, account, holder, ip):
    """Татах хүсэлт: Gold шууд хасагдаж, тоглогч утсаа баталгаажуулна (144773 руу SMS)."""
    settings = GameSettings.load()
    if not settings.cashout_enabled:
        raise GameError("cashout_disabled", "Gold татах түр хаалттай байна", 403)
    if not isinstance(gold, int) or isinstance(gold, bool) or gold <= 0:
        raise GameError("bad_request", "Татах Gold-оо оруулна уу")
    if gold < settings.cashout_min:
        raise GameError("cashout_min", f"Хамгийн багадаа {settings.cashout_min} Gold татна")
    if bank not in CASHOUT_BANKS:
        raise GameError("bad_bank", "Банкаа сонгоно уу")
    account = re.sub(r"[\s-]", "", account or "")
    if not re.fullmatch(r"\d{6,20}", account):
        raise GameError("bad_account", "Дансны дугаар 6-20 оронтой тоо байна")
    holder = (holder or "").strip()
    if not 2 <= len(holder) <= 60:
        raise GameError("bad_holder", "Данс эзэмшигчийн нэрийг оруулна уу")
    if not device_ok(player, device_id):
        raise GameError("device_change_required", "Энэ төхөөрөмж дээр тоглохын тулд төхөөрөмж солих төлбөр төлнө", 402)
    if expire_cashouts(player):
        player.refresh_from_db()
    if player.cashouts.filter(status=Cashout.VERIFYING).exists():
        raise GameError("cashout_active", "Өмнөх хүсэлтээ баталгаажуулна уу эсвэл цуцална уу", 409)
    if withdrawable(player) < gold:
        raise GameError("cashout_too_much",
                        f"Зөвхөн тоглоомоос хожсон Gold-оо татна. Одоо {withdrawable(player)} Gold татах боломжтой", 402)
    verification, instruction = _start_verification(player.phone, Verification.CASHOUT, {"player": player.pk}, device_id, ip)
    with transaction.atomic():
        locked = Player.objects.select_for_update().get(pk=player.pk)
        if withdrawable(locked) < gold:
            raise GameError("cashout_too_much", f"Одоо {withdrawable(locked)} Gold татах боломжтой", 402)
        cashout = Cashout.objects.create(player=locked, gold=gold, amount=int(gold * settings.cashout_rate),
                                         rate=settings.cashout_rate, bank=bank, account=account, holder=holder,
                                         verification=verification)
        change_gold(locked, -gold, GoldTransaction.CASHOUT, f"cashout:{cashout.pk}", {"cashout": cashout.pk})
        log_event(locked, "cashout_requested", {"cashout": cashout.pk, "gold": gold, "amount": cashout.amount, "bank": bank},
                  device_id, ip)
    return cashout, verification, instruction


def _close_cashout(cashout_id, status, note="", by=""):
    """Хүсэлтийг хаана. Олгоогүй (paid биш) бол Gold, хожсон Gold буцна. Аль хэдийн хаагдсан бол хэвээр."""
    with transaction.atomic():
        cashout = Cashout.objects.select_for_update().get(pk=cashout_id)
        if cashout.status not in Cashout.OPEN:
            return cashout
        if status != Cashout.PAID:
            locked = Player.objects.select_for_update().get(pk=cashout.player_id)
            change_gold(locked, cashout.gold, GoldTransaction.CASHOUT_REFUND, f"cashout:{cashout.pk}:refund",
                        {"cashout": cashout.pk, "status": status})
        cashout.status = status
        cashout.note = (note or "")[:200]
        cashout.handled_by = by[:150]
        cashout.closed_at = timezone.now()
        cashout.save(update_fields=["status", "note", "handled_by", "closed_at"])
        log_event(cashout.player, f"cashout_{status}", {"cashout": cashout.pk, "gold": cashout.gold, "by": by})
    return cashout


def refresh_cashout(cashout):
    """Утас баталгаажуулж буй хүсэлт: SMS ирсэн бол шилжүүлэг хүлээх, хугацаа дууссан бол Gold буцна."""
    if cashout.status != Cashout.VERIFYING:
        return cashout
    verification = cashout.verification
    if verification is None or verification.expires < timezone.now():
        return _close_cashout(cashout.pk, Cashout.EXPIRED, "Утас баталгаажуулаагүй")
    try:
        verified = sms.verifier().is_verified(verification)
    except sms.SmsError:
        return cashout
    if verified:
        with transaction.atomic():
            locked = Cashout.objects.select_for_update().get(pk=cashout.pk)
            if locked.status == Cashout.VERIFYING:
                locked.status = Cashout.PENDING
                locked.verified_at = timezone.now()
                locked.save(update_fields=["status", "verified_at"])
                Verification.objects.filter(pk=verification.pk).update(used=True)
                log_event(locked.player, "cashout_verified", {"cashout": locked.pk})
            return locked
    return cashout


def expire_cashouts(player=None):
    """Баталгаажуулалтын хугацаа дууссан хүсэлтүүдийн Gold-ийг буцаана."""
    qs = Cashout.objects.filter(status=Cashout.VERIFYING, verification__expires__lt=timezone.now())
    if player is not None:
        qs = qs.filter(player=player)
    expired = list(qs.values_list("pk", flat=True)[:50])
    for cashout_id in expired:
        _close_cashout(cashout_id, Cashout.EXPIRED, "Утас баталгаажуулаагүй")
    return len(expired)


def player_cashout(player, cashout_id):
    cashout = Cashout.objects.select_related("verification").filter(pk=cashout_id, player=player).first()
    if cashout is None:
        raise GameError("not_found", "Хүсэлт олдсонгүй", 404)
    return cashout


def cancel_cashout(player, cashout_id):
    cashout = player_cashout(player, cashout_id)
    if cashout.status != Cashout.VERIFYING:
        raise GameError("cashout_locked", "Баталгаажсан хүсэлтийг цуцлах боломжгүй", 409)
    return _close_cashout(cashout.pk, Cashout.CANCELLED, "Тоглогч цуцалсан")


def admin_pay_cashout(cashout, by, note):
    if cashout.status != Cashout.PENDING:
        raise GameError("cashout_not_pending", "Зөвхөн баталгаажсан (шилжүүлэг хүлээж буй) хүсэлтийг олгоно", 409)
    return _close_cashout(cashout.pk, Cashout.PAID, note, by)


def admin_reject_cashout(cashout, by, note):
    if cashout.status not in Cashout.OPEN:
        raise GameError("cashout_closed", "Хүсэлт аль хэдийн шийдэгдсэн", 409)
    return _close_cashout(cashout.pk, Cashout.REJECTED, note, by)


# ---------------------------------------------------------------- Төлбөр

def create_invoice(player, device_id, kind, platform, ip, pack=None):
    settings = GameSettings.load()
    if kind in (Invoice.PACK, Invoice.CAPITAL):
        # Бүх аймаг үнэгүй болсон: аймгийн багц, Улаанбаатарыг худалдахгүй (хуучин төлсөн нэхэмжлэх хэвээр)
        raise GameError("not_for_sale", "Бүх аймаг үнэгүй болсон", 410)
    gold = 0
    if kind == Invoice.GOLD:
        packs = gold_packs()
        if not isinstance(pack, int) or isinstance(pack, bool) or not 0 <= pack < len(packs):
            raise GameError("bad_pack", "Багц буруу байна")
        amount, credits, gold = packs[pack]["price"], 0, packs[pack]["gold"]
        description = f"Zombie Mongolia: {gold} Gold"
    elif device_id == WEB_DEVICE:
        raise GameError("game_only", "Үүнийг тоглоом дотроос хийнэ", 403)
    elif kind == Invoice.DEVICE:
        if device_ok(player, device_id):
            raise GameError("device_already_active", "Энэ төхөөрөмж аль хэдийн идэвхтэй байна", 409)
        validate_device(device_id)
        other = Device.objects.filter(device_id=device_id).exclude(player=player).exists()
        if other:
            raise GameError("device_taken", "Энэ төхөөрөмж өөр дансанд бүртгэлтэй байна", 409)
        amount, credits = settings.device_change_price, 0
        description = "Zombie Mongolia: төхөөрөмж солих"
    else:
        raise GameError("bad_kind", "Төлбөрийн төрөл буруу байна")

    # Өмнөх хүлээгдэж буй ижил нэхэмжлэхүүд цуцлагдана (тоглогч дахин дарсан)
    player.invoices.filter(kind=kind, status=Invoice.PENDING).update(status=Invoice.CANCELLED)
    invoice = Invoice.objects.create(player=player, kind=kind, amount=amount, credits=credits, gold=gold,
                                     device_id=device_id if kind == Invoice.DEVICE else "", platform=(platform or "")[:16])
    try:
        info = qpay.client().create_invoice(invoice, description)
    except qpay.QPayError as error:
        invoice.status = Invoice.CANCELLED
        invoice.save(update_fields=["status"])
        log_event(player, "invoice_failed", {"invoice": invoice.pk, "error": str(error)[:300]}, device_id, ip)
        raise GameError("payment_unavailable", "Төлбөрийн үйлчилгээ түр ажиллахгүй байна", 503)
    invoice.qpay_invoice_id = info["invoice_id"]
    invoice.save(update_fields=["qpay_invoice_id"])
    log_event(player, "invoice_created", {"invoice": invoice.pk, "kind": kind, "amount": amount}, device_id, ip)
    return invoice, info


def refresh_invoice(invoice, source):
    """Хүлээгдэж буй нэхэмжлэхийг QPay-ээс шалгаж, төлөгдсөн бол эрх олгоно."""
    if invoice.status != Invoice.PENDING or not invoice.qpay_invoice_id:
        return invoice
    try:
        paid, payment_id = qpay.client().check_paid(invoice)
    except qpay.QPayError as error:
        log_event(invoice.player, "payment_check_failed", {"invoice": invoice.pk, "error": str(error)[:300]})
        return invoice
    if paid:
        return apply_payment(invoice.pk, payment_id, source)
    return invoice


def apply_payment(invoice_id, payment_id, source):
    """Төлөгдсөн нэхэмжлэхийн эрхийг нэг л удаа олгоно (QPay-ийн мэдэгдэл, тоглоомын асуулга, админ зэрэг)."""
    with transaction.atomic():
        invoice = Invoice.objects.select_for_update().get(pk=invoice_id)
        if invoice.applied:
            return invoice
        player = Player.objects.select_for_update().get(pk=invoice.player_id)
        invoice.status = Invoice.PAID
        invoice.paid_at = invoice.paid_at or timezone.now()
        invoice.qpay_payment_id = payment_id or invoice.qpay_payment_id
        if invoice.kind == Invoice.GOLD:
            change_gold(player, invoice.gold, GoldTransaction.PURCHASE, f"invoice:{invoice.pk}",
                        {"invoice": invoice.pk, "amount": invoice.amount})
            invoice.applied = True
        elif invoice.kind == Invoice.PACK:
            player.credits += invoice.credits
            player.save(update_fields=["credits"])
            invoice.applied = True
        elif invoice.kind == Invoice.CAPITAL:
            capital = GameSettings.load().capital_index
            UnlockedProvince.objects.get_or_create(player=player, province=capital, defaults={"source": UnlockedProvince.PAID})
            log_event(player, "province_unlocked", {"province": capital, "source": UnlockedProvince.PAID}, "")
            invoice.applied = True
        else:
            existing = Device.objects.filter(device_id=invoice.device_id).first()
            if existing is not None and existing.player_id != player.pk:
                # Төлбөр хүлээх хооронд тэр төхөөрөмж өөр дансанд бүртгэгдсэн: админ шийднэ
                invoice.save(update_fields=["status", "paid_at", "qpay_payment_id"])
                log_event(player, "device_change_conflict", {"invoice": invoice.pk, "device": invoice.device_id})
                return invoice
            # Байр дүүрсэн бол хамгийн эрт бүртгэгдсэн төхөөрөмжийг чөлөөлж шинийг бүртгэнэ
            limit = max(1, GameSettings.load().max_devices)
            active = list(player.devices.filter(active=True).exclude(device_id=invoice.device_id).order_by("bound_at"))
            for old in active[:max(0, len(active) - limit + 1)]:
                Device.objects.filter(pk=old.pk).update(active=False)
                AuthToken.objects.filter(player=player, device_id=old.device_id).update(revoked=True)
            if existing is None:
                Device.objects.create(device_id=invoice.device_id, player=player, platform=invoice.platform)
            else:
                Device.objects.filter(pk=existing.pk).update(active=True, bound_at=timezone.now())
            invoice.applied = True
        invoice.save(update_fields=["status", "paid_at", "qpay_payment_id", "applied"])
        log_event(player, "payment", {"invoice": invoice.pk, "kind": invoice.kind, "amount": invoice.amount,
                                      "credits": invoice.credits, "payment": invoice.qpay_payment_id, "source": source},
                  invoice.device_id)
    return invoice
