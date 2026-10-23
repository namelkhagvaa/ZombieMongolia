"""Тоглогчийн бүртгэл, аймгийн эрх, төхөөрөмж, төлбөр, лог.

Зарчим:
  * Бүртгэл: нэр, утас, нууц үг; утсыг SMS кодоор баталгаажуулна. Нэг утасны дугаар = нэг данс.
  * Нэг төхөөрөмж = нэг данс (Device.device_id давтагдашгүй, данснаас салгагдсан ч хадгалагдана),
    дансны идэвхтэй төхөөрөмж нэг. Төхөөрөмж солих нь төлбөртэй (GameSettings.device_change_price).
  * Аймаг: бүгд үнэгүй. (Өмнө нь free_provinces үнэгүй, дараа нь багцаар (pack_*), Улаанбаатар тусдаа (capital_*)
    байсан: талбарууд хуучин нэхэмжлэх, тоглоомын хуучин хувилбарт зориулж үлдсэн.)
  * Нээлттэй өрөө (Room): room_fee + room_stake (20 + 50) Gold-той тоглогч аймаг эхлүүлэхэд заавал өрөө нээгдэж,
    өөр тоглогч урилгагүй орж ирнэ. Орохдоо хоёулаа room_fee төлж, room_stake-ийг барьцаална (Player.locked_gold).
    Хост даалгавар дуусаагүй байхад гарвал барьцаа нь шатна; сүүлийн даалгавар биелвэл +stage_bonus. Дохио
    (beat) тасарвал холболт тасарсан гэж үзэж барьцааг шийтгэлгүй суллана.
  * Gold: тоглогчийн байнгын мөнгө, зөвхөн сервер өөрчилнө. Өөрчлөлт бүр GoldTransaction-д (ref нь давтагдашгүй тул нэг
    үйл явдлыг хоёр удаа тооцохгүй). Gold-оор газрын зургийн дэлгүүрийн сайжруулалт (Player.damage ... soldiers) авна.
  * Event: тоглогчийн бүх үйлдлийн түүх (засахгүй, нэмэгдэнэ).
"""
import secrets

from django.contrib.auth.hashers import check_password, make_password
from django.db import models


class GameSettings(models.Model):
    """Үнэ, тоо: админаас өөрчилнө (ганц мөр)."""

    free_provinces = models.PositiveSmallIntegerField("Үнэгүй аймгийн тоо", default=2)
    pack_size = models.PositiveSmallIntegerField("Багц дахь аймгийн эрх", default=3)
    pack_price = models.PositiveIntegerField("Багцын үнэ (₮)", default=3000)
    device_change_price = models.PositiveIntegerField("Төхөөрөмж солих үнэ (₮)", default=5000)
    province_count = models.PositiveSmallIntegerField("Аймгийн тоо (тоглоомд)", default=22)
    capital_index = models.SmallIntegerField("Улаанбаатарын дугаар (тусдаа үнэтэй)", default=15)
    capital_price = models.PositiveIntegerField("Улаанбаатарын үнэ (₮)", default=5000)
    max_devices = models.PositiveSmallIntegerField("Нэг дансны төхөөрөмжийн тоо", default=2)
    start_gold = models.PositiveIntegerField("Шинэ тоглогчийн Gold", default=500)
    room_fee = models.PositiveIntegerField("Өрөөнд орох төлбөр (Gold)", default=20)
    room_stake = models.PositiveIntegerField("Өрөөний барьцаа (Gold)", default=50)
    stage_bonus = models.PositiveIntegerField("Үе шат дуусгасны шагнал (Gold)", default=40)
    stage_min_minutes = models.PositiveSmallIntegerField("Шагнал авах хамгийн бага хугацаа (минут)", default=5)
    # Сайжруулалтын үнэ: дараагийн түвшин = үнэ x (одоогийн түвшин + 1) Gold
    price_damage = models.PositiveIntegerField("Хохирол (Gold)", default=60)
    price_fire_rate = models.PositiveIntegerField("Буудах хурд (Gold)", default=60)
    price_magazine = models.PositiveIntegerField("Сумны хайрцаг (Gold)", default=40)
    price_health = models.PositiveIntegerField("Амь (Gold)", default=50)
    price_soldier = models.PositiveIntegerField("Цэрэг (Gold)", default=120)

    class Meta:
        verbose_name = "Тохиргоо (үнэ)"
        verbose_name_plural = "Тохиргоо (үнэ)"

    def __str__(self):
        return "Тохиргоо"

    def save(self, *args, **kwargs):
        self.pk = 1
        super().save(*args, **kwargs)

    @classmethod
    def load(cls):
        obj, _ = cls.objects.get_or_create(pk=1)
        return obj


class Player(models.Model):
    name = models.CharField("Нэр", max_length=60)
    phone = models.CharField("Утас", max_length=16, unique=True)  # +976XXXXXXXX
    password = models.CharField("Нууц үг (hash)", max_length=128)
    free_used = models.PositiveSmallIntegerField("Ашигласан үнэгүй эрх", default=0)
    credits = models.PositiveIntegerField("Үлдсэн худалдаж авсан эрх", default=0)
    blocked = models.BooleanField("Хаагдсан", default=False)
    gold = models.PositiveIntegerField("Gold", default=500)
    locked_gold = models.PositiveIntegerField("Барьцаалсан Gold", default=0)  # нээлттэй өрөөнд (gold-ийн нэг хэсэг)
    # Газрын зургийн дэлгүүрийн сайжруулалт (Gold-оор; тоглоомын src/Shop.h-тай ижил дээд түвшин)
    damage = models.PositiveSmallIntegerField("Хохирлын түвшин", default=0)
    fire_rate = models.PositiveSmallIntegerField("Буудах хурдны түвшин", default=0)
    magazine = models.PositiveSmallIntegerField("Сумны хайрцгийн түвшин", default=0)
    health_level = models.PositiveSmallIntegerField("Амийн түвшин", default=0)
    soldiers = models.PositiveSmallIntegerField("Цэргийн тоо", default=0)
    progress = models.TextField("Тоглоомын хадгалалт", blank=True)
    progress_updated = models.DateTimeField("Хадгалалт шинэчлэгдсэн", null=True, blank=True)
    created = models.DateTimeField("Бүртгүүлсэн", auto_now_add=True)

    class Meta:
        verbose_name = "Тоглогч"
        verbose_name_plural = "Тоглогчид"

    def __str__(self):
        return f"{self.name} ({self.phone})"

    def set_password(self, raw):
        self.password = make_password(raw)

    def check_password(self, raw):
        return check_password(raw, self.password)

    def active_devices(self):
        return self.devices.filter(active=True)


class Device(models.Model):
    """Төхөөрөмж: бүртгэгдсэн бол өөр данс нээж чадахгүй (данснаас салгагдсан ч). Суллахыг админ шийднэ."""

    device_id = models.CharField("Төхөөрөмжийн ID", max_length=64, unique=True)
    player = models.ForeignKey(Player, verbose_name="Тоглогч", related_name="devices", on_delete=models.CASCADE)
    platform = models.CharField("Платформ", max_length=16, blank=True)
    active = models.BooleanField("Идэвхтэй", default=True)
    bound_at = models.DateTimeField("Холбогдсон", auto_now_add=True)

    class Meta:
        verbose_name = "Төхөөрөмж"
        verbose_name_plural = "Төхөөрөмжүүд"

    def __str__(self):
        return f"{self.device_id[:12]}… ({self.platform or '?'})"


class UnlockedProvince(models.Model):
    FREE, PAID, ADMIN = "free", "paid", "admin"
    SOURCES = [(FREE, "Үнэгүй"), (PAID, "Худалдаж авсан"), (ADMIN, "Админ")]

    player = models.ForeignKey(Player, related_name="provinces", on_delete=models.CASCADE)
    province = models.PositiveSmallIntegerField("Аймаг (аяны дугаар 0-оос)")
    source = models.CharField("Эх үүсвэр", max_length=8, choices=SOURCES)
    created = models.DateTimeField("Нээсэн", auto_now_add=True)

    class Meta:
        verbose_name = "Нээсэн аймаг"
        verbose_name_plural = "Нээсэн аймгууд"
        unique_together = [("player", "province")]

    def __str__(self):
        return f"{self.province} ({self.source})"


class Verification(models.Model):
    """SMS кодоор утас баталгаажуулах оролдлого (бүртгэл, нууц үг сэргээх)."""

    REGISTER, RESET = "register", "reset"
    PURPOSES = [(REGISTER, "Бүртгэл"), (RESET, "Нууц үг сэргээх")]

    phone = models.CharField(max_length=16, db_index=True)
    purpose = models.CharField(max_length=10, choices=PURPOSES)
    code_hash = models.CharField(max_length=128, blank=True)  # өөрсдөө үүсгэсэн код (console горим)
    provider_ref = models.CharField(max_length=128, blank=True)  # SMS үйлчилгээний хүсэлтийн дугаар
    payload = models.JSONField(default=dict, blank=True)  # бүртгэлийн мэдээлэл (нэр, нууц үгийн hash, төхөөрөмж)
    attempts = models.PositiveSmallIntegerField(default=0)
    used = models.BooleanField(default=False)
    created = models.DateTimeField(auto_now_add=True)
    expires = models.DateTimeField()

    class Meta:
        verbose_name = "Утас баталгаажуулалт"
        verbose_name_plural = "Утас баталгаажуулалт"


class AuthToken(models.Model):
    key = models.CharField(max_length=64, unique=True)
    player = models.ForeignKey(Player, related_name="tokens", on_delete=models.CASCADE)
    device_id = models.CharField(max_length=64)
    created = models.DateTimeField(auto_now_add=True)
    last_used = models.DateTimeField(auto_now=True)
    # Өөр төхөөрөмж дээр нэвтэрсэн: энэ нэвтрэлт хүчингүй (нэг зэрэг нэг л төхөөрөмж тоглоно)
    revoked = models.BooleanField(default=False)

    class Meta:
        verbose_name = "Нэвтрэлт"
        verbose_name_plural = "Нэвтрэлтүүд"

    @staticmethod
    def new_key():
        return secrets.token_hex(24)


def _callback_key():
    return secrets.token_hex(16)


class Invoice(models.Model):
    PACK, DEVICE, CAPITAL = "pack", "device", "capital"
    KINDS = [(PACK, "Аймгийн багц"), (DEVICE, "Төхөөрөмж солих"), (CAPITAL, "Улаанбаатар")]
    PENDING, PAID, CANCELLED = "pending", "paid", "cancelled"
    STATUSES = [(PENDING, "Хүлээгдэж буй"), (PAID, "Төлсөн"), (CANCELLED, "Цуцалсан")]

    player = models.ForeignKey(Player, verbose_name="Тоглогч", related_name="invoices", on_delete=models.CASCADE)
    kind = models.CharField("Төрөл", max_length=8, choices=KINDS)
    amount = models.PositiveIntegerField("Дүн (₮)")
    credits = models.PositiveSmallIntegerField("Олгох эрх", default=0)  # багц: үнэ өөрчлөгдсөн ч худалдсан үеийнхээр
    device_id = models.CharField("Шилжих төхөөрөмж", max_length=64, blank=True)
    platform = models.CharField(max_length=16, blank=True)
    status = models.CharField("Төлөв", max_length=10, choices=STATUSES, default=PENDING)
    qpay_invoice_id = models.CharField("QPay нэхэмжлэх", max_length=64, blank=True, db_index=True)
    qpay_payment_id = models.CharField("QPay гүйлгээ", max_length=64, blank=True)
    callback_key = models.CharField(max_length=48, default=_callback_key)  # QPay-ийн мэдэгдлийг баталгаажуулна
    applied = models.BooleanField("Эрх олгосон", default=False)
    created = models.DateTimeField("Үүсгэсэн", auto_now_add=True)
    paid_at = models.DateTimeField("Төлсөн", null=True, blank=True)

    class Meta:
        verbose_name = "Нэхэмжлэх"
        verbose_name_plural = "Нэхэмжлэхүүд"

    def __str__(self):
        return f"#{self.pk} {self.get_kind_display()} {self.amount}₮ ({self.get_status_display()})"


class GoldTransaction(models.Model):
    """Gold-ийн өөрчлөлт бүр (засахгүй, нэмэгдэнэ). ref давтагдашгүй: нэг үйл явдлыг (худалдан авалт, тулаан, төлбөр)
    хоёр дахь удаа тооцохыг өгөгдлийн сан өөрөө хориглоно."""

    SIGNUP, SHOP, ADMIN = "signup", "shop", "admin"
    ROOM_FEE, ROOM_LEAVE, STAGE_COMPLETE = "room_fee", "room_leave", "stage_complete"
    REASONS = [(SIGNUP, "Эхлэл"), (SHOP, "Дэлгүүр"), (ADMIN, "Админ"), (ROOM_FEE, "Өрөөнд орсон"),
               (ROOM_LEAVE, "Өрөөнөөс гарсан (барьцаа шатсан)"), (STAGE_COMPLETE, "Үе шат дуусгасан")]

    player = models.ForeignKey(Player, verbose_name="Тоглогч", related_name="gold_transactions", on_delete=models.CASCADE)
    amount = models.IntegerField("Дүн")
    balance = models.PositiveIntegerField("Үлдэгдэл")
    reason = models.CharField("Шалтгаан", max_length=16, choices=REASONS)
    ref = models.CharField("Үйл явдал", max_length=80, unique=True)
    data = models.JSONField("Мэдээлэл", default=dict, blank=True)
    created = models.DateTimeField("Цаг", auto_now_add=True, db_index=True)

    class Meta:
        verbose_name = "Gold-ийн гүйлгээ"
        verbose_name_plural = "Gold-ийн гүйлгээ"
        ordering = ["-created", "-id"]

    def __str__(self):
        return f"{self.created:%Y-%m-%d %H:%M} {self.amount:+d} ({self.reason})"


class Room(models.Model):
    """Нээлттэй өрөө: нэг хост (аймгийг тоглож буй), хамгийн ихдээ нэг зочин. Тоглоомын тооцооллыг хостын төхөөрөмж
    хийнэ; сервер нь хэн орсон, барьцаа, шагналыг л хариуцна."""

    OPEN, CLOSED = "open", "closed"
    STATUSES = [(OPEN, "Нээлттэй"), (CLOSED, "Хаагдсан")]

    province = models.PositiveSmallIntegerField("Аймаг")
    status = models.CharField("Төлөв", max_length=8, choices=STATUSES, default=OPEN, db_index=True)
    host = models.ForeignKey(Player, verbose_name="Хост", related_name="hosted_rooms", on_delete=models.CASCADE)
    guest = models.ForeignKey(Player, verbose_name="Зочин", related_name="guest_rooms", null=True, blank=True,
                              on_delete=models.SET_NULL)
    host_stake = models.PositiveIntegerField("Хостын барьцаа", default=0)
    guest_stake = models.PositiveIntegerField("Зочны барьцаа", default=0)
    host_seen = models.DateTimeField("Хостын сүүлийн дохио")
    guest_seen = models.DateTimeField("Зочны сүүлийн дохио", null=True, blank=True)
    closed_reason = models.CharField("Хаагдсан шалтгаан", max_length=16, blank=True)
    created = models.DateTimeField("Нээсэн", auto_now_add=True)
    closed = models.DateTimeField("Хаагдсан", null=True, blank=True)

    class Meta:
        verbose_name = "Өрөө"
        verbose_name_plural = "Өрөөнүүд"
        ordering = ["-created"]

    def __str__(self):
        return f"#{self.pk} аймаг {self.province} ({self.get_status_display()})"


class Event(models.Model):
    """Тоглогчийн үйлдлийн түүх (лог)."""

    player = models.ForeignKey(Player, verbose_name="Тоглогч", related_name="events", null=True, blank=True,
                               on_delete=models.SET_NULL)
    kind = models.CharField("Үйлдэл", max_length=40, db_index=True)
    data = models.JSONField("Мэдээлэл", default=dict, blank=True)
    device_id = models.CharField("Төхөөрөмж", max_length=64, blank=True)
    ip = models.GenericIPAddressField("IP", null=True, blank=True)
    created = models.DateTimeField("Цаг", auto_now_add=True, db_index=True)

    class Meta:
        verbose_name = "Лог"
        verbose_name_plural = "Лог"
        ordering = ["-created"]

    def __str__(self):
        return f"{self.created:%Y-%m-%d %H:%M} {self.kind}"
