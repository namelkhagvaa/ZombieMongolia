"""Бизнесийн дүрэм: бүртгэл, нэвтрэлт, аймаг, Gold, дэлгүүр, нэхэмжлэх, төлбөрийн эрх олголт, лог.

Бүх алдаа GameError (код + монгол мессеж + HTTP төлөв): тоглоом кодоор нь ялгаж, мессежийг харуулна.
"""
import re
from datetime import timedelta

from django.contrib.auth.hashers import make_password
from django.db import transaction
from django.utils import timezone

from . import qpay, sms
from .models import (AuthToken, Device, Event, GameSettings, GoldTransaction, Invoice, Player, UnlockedProvince,
                     Verification)

VERIFICATION_TTL = timedelta(minutes=5)  # verify.mn-ийн сесс 5 минут
SMS_PER_HOUR = 5             # нэг дугаарт цагт эхлүүлэх баталгаажуулалтын дээд тоо
LOGIN_FAILURES_LIMIT = 10    # 15 минутад буруу нэвтрэх оролдлогын дээд тоо
MIN_PASSWORD = 6

# Газрын зургийн дэлгүүр: нэр -> (Player-ийн талбар, GameSettings-ийн үнийн талбар, дээд түвшин).
# Дээд түвшин тоглоомын src/Shop.h, src/Weapon.h-тай ижил.
UPGRADES = {
    "damage": ("damage", "price_damage", 5),
    "fire_rate": ("fire_rate", "price_fire_rate", 5),
    "magazine": ("magazine", "price_magazine", 5),
    "health": ("health_level", "price_health", 5),
    "soldier": ("soldiers", "price_soldier", 4),
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
    return {
        "name": player.name,
        "phone": mask_phone(player.phone),
        # Бүх аймаг үнэгүй (тоглоомын хуучин хувилбар ч бүгдийг нээлттэй гэж харна)
        "provinces": list(range(settings.province_count)),
        "free_left": 0,
        "credits": 0,
        "gold": player.gold,
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
        player = Player.objects.create(name=data["name"], phone=verification.phone, password=data["password"], gold=start)
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


def start_province(player, device_id, province, ip):
    _validate_province(province)
    if not device_ok(player, device_id):
        raise GameError("device_change_required", "Энэ төхөөрөмж дээр тоглохын тулд төхөөрөмж солих төлбөр төлнө", 402)
    log_event(player, "province_start", {"province": province}, device_id, ip)


# ---------------------------------------------------------------- Gold, дэлгүүр

def change_gold(locked, amount, reason, ref, data=None):
    """Gold-ийг өөрчилж бүртгэлд бичнэ. locked: transaction.atomic() дотор select_for_update()-ээр түгжсэн тоглогч.
    Ижил ref өмнө нь бичигдсэн бол юу ч хийхгүй (False). Үлдэгдэл хүрэлцэхгүй бол GameError."""
    if GoldTransaction.objects.filter(ref=ref).exists():
        return False
    if locked.gold + amount < 0:
        raise GameError("not_enough_gold", "Gold хүрэлцэхгүй байна", 402)
    locked.gold += amount
    locked.save(update_fields=["gold"])
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
        if current == level + 1 and GoldTransaction.objects.filter(ref=ref).exists():
            return locked
        if current != level:
            raise GameError("shop_changed", "Дэлгүүрийн мэдээлэл шинэчлэгдлээ. Дахин оролдоно уу", 409)
        if current >= top:
            raise GameError("max_level", "Хамгийн дээд түвшинд хүрсэн", 409)
        cost = getattr(GameSettings.load(), price_field) * (current + 1)
        change_gold(locked, -cost, GoldTransaction.SHOP, ref, {"item": item, "level": current + 1})
        setattr(locked, field, current + 1)
        locked.save(update_fields=[field])
        log_event(locked, "shop_buy", {"item": item, "level": current + 1, "gold": cost}, device_id, ip)
    return locked


# ---------------------------------------------------------------- Төлбөр

def create_invoice(player, device_id, kind, platform, ip):
    settings = GameSettings.load()
    if kind in (Invoice.PACK, Invoice.CAPITAL):
        # Бүх аймаг үнэгүй болсон: аймгийн багц, Улаанбаатарыг худалдахгүй (хуучин төлсөн нэхэмжлэх хэвээр)
        raise GameError("not_for_sale", "Бүх аймаг үнэгүй болсон", 410)
    if kind == Invoice.DEVICE:
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
    invoice = Invoice.objects.create(player=player, kind=kind, amount=amount, credits=credits,
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
        if invoice.kind == Invoice.PACK:
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
