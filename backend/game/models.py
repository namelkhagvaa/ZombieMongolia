"""Тоглогчийн бүртгэл, аймгийн эрх, төхөөрөмж, төлбөр, лог.

Зарчим:
  * Бүртгэл: нэр, утас, нууц үг; утсыг SMS кодоор баталгаажуулна. Нэг утасны дугаар = нэг данс.
  * Нэг төхөөрөмж = нэг данс (Device.device_id давтагдашгүй, данснаас салгагдсан ч хадгалагдана),
    дансны идэвхтэй төхөөрөмж нэг. Төхөөрөмж солих нь төлбөртэй (GameSettings.device_change_price).
  * Аймаг: анх сонгосон GameSettings.free_provinces аймаг үнэгүй (Улаанбаатар орохгүй); дараа нь багц
    худалдаж авна (pack_price-аар pack_size эрх), аймаг нээх бүрд нэг эрх зарцуулна.
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
    capital_index = models.SmallIntegerField("Улаанбаатарын дугаар (үнэгүй сонгож болохгүй)", default=15)
    max_devices = models.PositiveSmallIntegerField("Нэг дансны төхөөрөмжийн тоо", default=2)

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
    PACK, DEVICE = "pack", "device"
    KINDS = [(PACK, "Аймгийн багц"), (DEVICE, "Төхөөрөмж солих")]
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
