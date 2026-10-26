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
  * Тулаан: зочин орж ирмэгц хоёр баг тулална (Room.battle). Хоёр тал үр дүнг мэдэгдэнэ: таарвал ялагдагчийн
    барьцаа ялагчид шилжинэ; зөрвөл хүчингүй. Нэг тал л мэдэгдвэл (нөгөө нь тасарсан) battle_wait-ийн дараа
    тэр үр дүнгээр. Зочин ялбал шинэ хост болж, өрөөний даалгаврын ахиц (stage_state), ялагдагчийн элсүүлсэн
    цэргүүд (recruits) түүнд шилжинэ. Ижил хоёр тоглогчийн хооронд хоногт battle_pair_limit-ээс олон тулаанд
    Gold шилжихгүй (зориуд ээлжлэн ялагдах).
  * Gold: тоглогчийн байнгын мөнгө, зөвхөн сервер өөрчилнө. Өөрчлөлт бүр GoldTransaction-д (ref нь давтагдашгүй тул нэг
    үйл явдлыг хоёр удаа тооцохгүй). Gold-оор газрын зургийн дэлгүүрийн сайжруулалт (Player.damage ... soldiers) авна.
  * Gold татах (Cashout): зөвхөн тоглоомоос хожсон Gold-ийг (Player.won_gold) банкны данс руу татна; цэнэглэсэн Gold
    татагдахгүй. Хүсэлт илгээхэд Gold шууд хасагдаж, тоглогч утсаа verify.mn-ээр (144773 руу SMS) баталгаажуулна.
    Баталгаажсан хүсэлтийг админ банкаар гараар шилжүүлээд "Олгосон" гэж тэмдэглэнэ; татгалзвал, баталгаажуулалтын
    хугацаа дуусвал, тоглогч цуцалбал Gold (ба хожсон Gold) буцна.
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
    # Найзтайгаа кодоор хамт тоглож үе давсан: хоёуланд нь (урамшуулал, татагдахгүй), нэг тоглогч өдөрт хязгаартай
    coop_reward = models.PositiveIntegerField("Хамт давсны шагнал, хүн бүрд (Gold)", default=30)
    coop_daily_limit = models.PositiveSmallIntegerField("Хамт давсны шагнал өдөрт (удаа)", default=5)
    stage_min_minutes = models.PositiveSmallIntegerField("Шагнал авах хамгийн бага хугацаа (минут)", default=5)
    battle_pair_limit = models.PositiveSmallIntegerField("Нэг хосын хоногийн төлбөртэй тулаан", default=3)
    stage_daily_limit = models.PositiveSmallIntegerField("Үе шатны шагнал хоногт (тоглогч бүрд)", default=10)
    verify_bonus = models.PositiveIntegerField("Утас баталгаажуулсны урамшуулал (Gold)", default=100)
    # Утасны операторууд олон хэрэглэгчийг нэг IP-аар гаргадаг (CGNAT): хэт бага бол жинхэнэ хэрэглэгч бүртгүүлж чадахгүй
    register_per_ip_day = models.PositiveSmallIntegerField("Нэг IP-ээс хоногт нээх данс (SMS-гүй бүртгэл)", default=20)
    block_same_ip = models.BooleanField("Ижил IP-тай тоглогчдын тулаанд Gold шилжүүлэхгүй (олон дансаар)",
                                        default=True)
    # Вэбээр Gold худалдах багцууд: [[Gold, үнэ ₮], ...]
    gold_packs = models.JSONField("Gold-ийн багцууд", default=list, blank=True)
    # Сайжруулалтын үнэ: дараагийн түвшин = үнэ x (одоогийн түвшин + 1) Gold
    price_damage = models.PositiveIntegerField("Хохирол (Gold)", default=60)
    price_fire_rate = models.PositiveIntegerField("Буудах хурд (Gold)", default=60)
    price_magazine = models.PositiveIntegerField("Сумны хайрцаг (Gold)", default=40)
    price_health = models.PositiveIntegerField("Амь (Gold)", default=50)
    price_soldier = models.PositiveIntegerField("Цэрэг (Gold)", default=120)
    # Онцгой дүр: тэр дүрээр аймаг эхлүүлэх бүрд (дүр тус бүр өөрийн үнэтэй)
    skin_price = models.PositiveIntegerField("DEMON дүрээр тоглох (Gold)", default=50)
    skin_price_iron = models.PositiveIntegerField("IRON дүрээр тоглох (Gold)", default=50)
    # Gold татах (хожсон Gold-оос л): доод хэмжээ, 1 Gold-ийн ₮ ханш
    cashout_enabled = models.BooleanField("Gold татах нээлттэй", default=True)
    cashout_min = models.PositiveIntegerField("Татах доод хэмжээ (Gold)", default=20000)
    cashout_rate = models.DecimalField("1 Gold = ₮", max_digits=8, decimal_places=2, default=1)

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
    # +976XXXXXXXX. Бүртгүүлэхэд баталгаажуулахгүй; дугаарын жинхэнэ эзэн SMS-ээр баталгаажуулбал дугаар түүнийх болж
    # өмнө нь эзэлсэн (баталгаажаагүй) данс утасгүй (null) үлдэнэ.
    phone = models.CharField("Утас", max_length=16, unique=True, null=True, blank=True)
    # Утсаа SMS-ээр баталгаажуулсан: Gold нэмэгдэх бүх зүйл (шагнал, тулааны хожил, татах) зөвхөн үүнд
    phone_verified = models.BooleanField("Утас баталгаажсан", default=False)
    password = models.CharField("Нууц үг (hash)", max_length=128)
    free_used = models.PositiveSmallIntegerField("Ашигласан үнэгүй эрх", default=0)
    credits = models.PositiveIntegerField("Үлдсэн худалдаж авсан эрх", default=0)
    blocked = models.BooleanField("Хаагдсан", default=False)
    gold = models.PositiveIntegerField("Gold", default=500)
    locked_gold = models.PositiveIntegerField("Барьцаалсан Gold", default=0)  # нээлттэй өрөөнд (gold-ийн нэг хэсэг)
    # Gold гурван хэсэгтэй (gold = won_gold + bonus_gold + цэнэглэсэн, GoldTransaction.split_gold):
    # won_gold: бусад тоглогчоос тулаанд хожсон бодит (цэнэглэсэн, хожсон) Gold, зөвхөн үүнийг татна.
    # bonus_gold: эхлэлийн, үе шатны шагнал, админы Gold: тоглоход хэрэглэнэ, татагдахгүй; тулаанд алдвал ялагчид
    # мөн урамшуулал болж очно (олон дансаар үнэгүй Gold-ийг мөнгө болгохгүй).
    won_gold = models.PositiveIntegerField("Хожсон Gold (татах боломжтой)", default=0)
    bonus_gold = models.PositiveIntegerField("Урамшууллын Gold (татагдахгүй)", default=0)
    # Газрын зургийн дэлгүүрийн сайжруулалт (Gold-оор; тоглоомын src/Shop.h-тай ижил дээд түвшин)
    damage = models.PositiveSmallIntegerField("Хохирлын түвшин", default=0)
    fire_rate = models.PositiveSmallIntegerField("Буудах хурдны түвшин", default=0)
    magazine = models.PositiveSmallIntegerField("Сумны хайрцгийн түвшин", default=0)
    health_level = models.PositiveSmallIntegerField("Амийн түвшин", default=0)
    soldiers = models.PositiveSmallIntegerField("Цэргийн тоо", default=0)
    # Тоглолтод алдсан (шууд хасагдсан) Gold-ийн цэргийн нийт тоо: дахин авахад худалдааны ref давхцахгүй
    soldiers_lost = models.PositiveIntegerField("Алдсан цэрэг", default=0)
    progress = models.TextField("Тоглоомын хадгалалт", blank=True)
    progress_updated = models.DateTimeField("Хадгалалт шинэчлэгдсэн", null=True, blank=True)
    created = models.DateTimeField("Бүртгүүлсэн", auto_now_add=True)

    class Meta:
        verbose_name = "Тоглогч"
        verbose_name_plural = "Тоглогчид"

    def __str__(self):
        return f"{self.name} ({self.contact})"

    @property
    def contact(self):
        """Админд харуулах: утас (баталгаажаагүй бол тэмдэглэнэ)."""
        if not self.phone:
            return "утасгүй"
        return self.phone if self.phone_verified else f"{self.phone} (баталгаажаагүй)"

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

    REGISTER, RESET, CASHOUT, VERIFY = "register", "reset", "cashout", "verify"
    PURPOSES = [(REGISTER, "Бүртгэл"), (RESET, "Нууц үг сэргээх"), (CASHOUT, "Gold татах"), (VERIFY, "Утас баталгаажуулах")]

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
    PACK, DEVICE, CAPITAL, GOLD = "pack", "device", "capital", "gold"
    KINDS = [(PACK, "Аймгийн багц"), (DEVICE, "Төхөөрөмж солих"), (CAPITAL, "Улаанбаатар"), (GOLD, "Gold")]
    PENDING, PAID, CANCELLED = "pending", "paid", "cancelled"
    STATUSES = [(PENDING, "Хүлээгдэж буй"), (PAID, "Төлсөн"), (CANCELLED, "Цуцалсан")]

    player = models.ForeignKey(Player, verbose_name="Тоглогч", related_name="invoices", on_delete=models.CASCADE)
    kind = models.CharField("Төрөл", max_length=8, choices=KINDS)
    amount = models.PositiveIntegerField("Дүн (₮)")
    credits = models.PositiveSmallIntegerField("Олгох эрх", default=0)  # багц: үнэ өөрчлөгдсөн ч худалдсан үеийнхээр
    gold = models.PositiveIntegerField("Олгох Gold", default=0)  # Gold-ийн нэхэмжлэх (үнэ өөрчлөгдсөн ч худалдсан үеийнхээр)
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
    BATTLE_WIN, BATTLE_LOSS, PURCHASE = "battle_win", "battle_loss", "purchase"
    CASHOUT, CASHOUT_REFUND = "cashout", "cashout_refund"
    SKIN = "skin"
    COOP_WIN = "coop_win"
    ROOM_REFUND = "room_refund"
    VERIFY_BONUS = "verify_bonus"
    REASONS = [(SIGNUP, "Эхлэл"), (SHOP, "Дэлгүүр"), (ADMIN, "Админ"), (ROOM_FEE, "Өрөөнд орсон"),
               (ROOM_LEAVE, "Өрөөнөөс гарсан (барьцаа шатсан)"), (STAGE_COMPLETE, "Үе шат дуусгасан"),
               (BATTLE_WIN, "Тулаанд ялсан"), (BATTLE_LOSS, "Тулаанд ялагдсан"), (PURCHASE, "Худалдаж авсан"),
               (CASHOUT, "Gold татсан"), (CASHOUT_REFUND, "Татах хүсэлт буцсан"), (SKIN, "Онцгой дүрээр тоглосон"),
               (COOP_WIN, "Найзтайгаа хамт давсан"), (ROOM_REFUND, "Өрөөнд холбогдож чадаагүй (төлбөр буцсан)"),
               (VERIFY_BONUS, "Утас баталгаажуулсан")]
    # Хожсон Gold (Player.won_gold) нэмэгдэх шалтгаан; урамшууллын Gold (Player.bonus_gold) нэмэгдэх шалтгаан.
    # Бусад орлого (худалдан авалт) цэнэглэсэн Gold. Үе шатны шагналыг хожсонд тооцохгүй: тоглоомгүйгээр (API-гаар)
    # өрөө нээж 5 минут хүлээгээд "дууслаа" гэж олборлож болдог байсан.
    WON_GAIN = (BATTLE_WIN, CASHOUT_REFUND)
    BONUS_GAIN = (SIGNUP, STAGE_COMPLETE, ADMIN, COOP_WIN, ROOM_REFUND, VERIFY_BONUS)
    # Утсаа баталгаажуулаагүй тоглогчид нэмэгдэхгүй (шагнал, хожил). Эхлэлийн Gold, худалдан авалт, буцаалт, админ хамаарахгүй.
    REWARDS = (STAGE_COMPLETE, COOP_WIN, BATTLE_WIN)

    @classmethod
    def split_gold(cls, gold, won, bonus, amount, reason, bonus_part=0):
        """Gold-ийн өөрчлөлтийн дараах (gold, won, bonus). Зарцуулалт эхлээд урамшууллаас, дараа нь цэнэглэснээс,
        эцэст нь хожсоноос (татах Gold хамгаалагдана); татах (CASHOUT) зөвхөн хожсоноос. Тулаанд хожсоны
        bonus_part хэсэг нь ялагдагчийн урамшууллаас ирсэн тул урамшуулал хэвээр."""
        if amount > 0:
            if reason in cls.WON_GAIN:
                part = min(max(0, bonus_part), amount)
                won += amount - part
                bonus += part
            elif reason in cls.BONUS_GAIN:
                bonus += amount
        elif amount < 0:
            spend = -amount
            if reason == cls.CASHOUT:
                won = max(0, won - spend)
            else:
                paid = max(0, gold - won - bonus)
                take = min(spend, bonus)
                bonus -= take
                spend -= take
                spend -= min(spend, paid)
                won = max(0, won - spend)
        gold += amount
        won = min(won, gold)
        bonus = min(bonus, gold - won)
        return gold, won, bonus

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


class Cashout(models.Model):
    """Хожсон Gold-ийг банкны данс руу татах хүсэлт. Gold хүсэлт үүсэхэд хасагдана (verifying), утсаа баталгаажуулбал
    админ шилжүүлэхийг хүлээнэ (pending). Олгосон (paid); татгалзсан, хугацаа дууссан, цуцалсан бол Gold буцна."""

    VERIFYING, PENDING, PAID, REJECTED, EXPIRED, CANCELLED = "verifying", "pending", "paid", "rejected", "expired", "cancelled"
    STATUSES = [(VERIFYING, "Утас баталгаажуулж буй"), (PENDING, "Шилжүүлэг хүлээж буй"), (PAID, "Олгосон"),
                (REJECTED, "Татгалзсан"), (EXPIRED, "Хугацаа дууссан"), (CANCELLED, "Цуцалсан")]
    OPEN = (VERIFYING, PENDING)

    player = models.ForeignKey(Player, verbose_name="Тоглогч", related_name="cashouts", on_delete=models.CASCADE)
    gold = models.PositiveIntegerField("Gold")
    amount = models.PositiveIntegerField("Олгох дүн (₮)")
    rate = models.DecimalField("Ханш (1 Gold = ₮)", max_digits=8, decimal_places=2)
    bank = models.CharField("Банк", max_length=60)
    account = models.CharField("Дансны дугаар", max_length=32)
    holder = models.CharField("Данс эзэмшигч", max_length=60)
    status = models.CharField("Төлөв", max_length=10, choices=STATUSES, default=VERIFYING, db_index=True)
    verification = models.ForeignKey(Verification, null=True, blank=True, on_delete=models.SET_NULL)
    note = models.CharField("Тэмдэглэл", max_length=200, blank=True)
    handled_by = models.CharField("Шийдсэн админ", max_length=150, blank=True)
    created = models.DateTimeField("Үүссэн", auto_now_add=True, db_index=True)
    verified_at = models.DateTimeField("Баталгаажсан", null=True, blank=True)
    closed_at = models.DateTimeField("Шийдэгдсэн", null=True, blank=True)

    class Meta:
        verbose_name = "Gold татах хүсэлт"
        verbose_name_plural = "Gold татах хүсэлт"
        ordering = ["-created", "-id"]

    def __str__(self):
        return f"{self.player} {self.gold} Gold -> {self.bank} {self.account} ({self.status})"


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
    battle = models.PositiveIntegerField("Тулааны дугаар", default=0)  # зочин орох бүрд нэмэгдэнэ
    host_report = models.CharField("Хостын мэдэгдсэн ялагч", max_length=8, blank=True)
    guest_report = models.CharField("Зочны мэдэгдсэн ялагч", max_length=8, blank=True)
    report_at = models.DateTimeField("Анхны мэдэгдэл", null=True, blank=True)
    guest_left = models.BooleanField("Зочин тулааны үеэр гарсан (AI тулалдана)", default=False)
    # Найзын код (6 орон): энэ кодоор орсон зочин хамт тоглоно (тулаангүй, төлбөргүй). pvp: Gold хүрэлцсэн тул өөр
    # тоглогч урилгагүй орж тулалдана (эс бөгөөс зөвхөн найз кодоор).
    code = models.CharField("Найзын код", max_length=6, blank=True, db_index=True)
    pvp = models.BooleanField("Нээлттэй (тулаан)", default=True)
    guest_coop = models.BooleanField("Зочин кодоор орсон найз", default=False)
    # Өрөөний даалгаврын ахиц: хост илгээнэ (тоглоомын StageSave-ийн мөр), шинэ хост үргэлжлүүлнэ
    stage_state = models.TextField("Даалгаврын ахиц", blank=True)
    stage_updates = models.PositiveIntegerField("Ахиц өөрчлөгдсөн удаа", default=0)  # үе шатны шагналын шалгалт
    recruits = models.PositiveSmallIntegerField("Хостын элсүүлсэн цэрэг", default=0)
    closed_reason = models.CharField("Хаагдсан шалтгаан", max_length=16, blank=True)
    # Тоглоомын сүлжээний протокол (Net::kProtocolVersion): зөвхөн ижил хувилбартай тоглогчдыг тааруулна
    protocol = models.PositiveSmallIntegerField("Протокол", default=0)
    host_ip = models.GenericIPAddressField("Хостын IP", null=True, blank=True)
    guest_ip = models.GenericIPAddressField("Зочны IP", null=True, blank=True)
    # Зочин тоглоомоор (relay) холбогдсоныг хост баталгаажуулсан: түүнээс өмнө гарвал төлбөр буцна, тулаан тоологдохгүй
    guest_confirmed = models.BooleanField("Зочин холбогдсон", default=False)
    guest_joined = models.DateTimeField("Зочин орсон", null=True, blank=True)
    guest_fee = models.PositiveIntegerField("Зочны төлсөн орох төлбөр", default=0)
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
