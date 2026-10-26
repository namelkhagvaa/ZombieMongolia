"""Админ самбарын JSON API (/api/manage/...): admin/ хавтсан дахь React вэб ашиглана.

Нэвтрэлт: Django-ийн staff хэрэглэгч (createsuperuser) нэр, нууц үгээр -> гарын үсэгтэй токен (12 цаг).
Хүсэлтүүд: толгойд "Authorization: Bearer <token>". Админы бүх өөрчлөлт Event логт "admin_..." гэж бичигдэнэ.
"""
import json
import secrets
from datetime import timedelta
from decimal import Decimal
from functools import wraps

from django.contrib.auth import authenticate, get_user_model
from django.core import signing
from django.core.paginator import Paginator
from django.db import transaction
from django.db.models import Count, Q, Sum
from django.db.models.functions import TruncDate
from django.http import JsonResponse
from django.utils import timezone
from django.views.decorators.csrf import csrf_exempt

from . import logic
from .models import AuthToken, Cashout, Device, Event, GameSettings, GoldTransaction, Invoice, Player, Room, UnlockedProvince

TOKEN_SALT = "zm-manage"
TOKEN_AGE = 12 * 3600
LOGIN_FAILURES_LIMIT = 10
PAGE_SIZE_MAX = 100


class ManageError(Exception):
    def __init__(self, message, status=400):
        super().__init__(message)
        self.message = message
        self.status = status


def client_ip(request):
    forwarded = request.META.get("HTTP_X_FORWARDED_FOR", "")
    return forwarded.split(",")[0].strip() if forwarded else request.META.get("REMOTE_ADDR")


def staff_api(methods, auth=True):
    def decorator(view):
        @csrf_exempt
        @wraps(view)
        def wrapper(request, *args, **kwargs):
            if request.method not in methods:
                return JsonResponse({"ok": False, "message": "Буруу хүсэлт"}, status=405)
            try:
                request.json = json.loads(request.body) if request.body else {}
                if not isinstance(request.json, dict):
                    raise ValueError
            except ValueError:
                return JsonResponse({"ok": False, "message": "Буруу хүсэлт"}, status=400)
            request.staff = None
            if auth:
                header = request.META.get("HTTP_AUTHORIZATION", "")
                try:
                    data = signing.loads(header[7:], salt=TOKEN_SALT, max_age=TOKEN_AGE) if header.startswith("Bearer ") else None
                except signing.BadSignature:
                    data = None
                user = get_user_model().objects.filter(pk=data.get("uid")).first() if data else None
                if user is None or not user.is_active or not user.is_staff:
                    return JsonResponse({"ok": False, "error": "auth", "message": "Дахин нэвтэрнэ үү"}, status=401)
                request.staff = user
            try:
                return view(request, *args, **kwargs)
            except ManageError as error:
                return JsonResponse({"ok": False, "message": error.message}, status=error.status)
            except logic.GameError as error:
                return JsonResponse({"ok": False, "message": error.message}, status=error.status)

        return wrapper

    return decorator


def ok(**data):
    return JsonResponse({"ok": True, **data})


def audit(request, player, kind, data=None):
    logic.log_event(player, "admin_" + kind, {**(data or {}), "admin": request.staff.username}, "", client_ip(request))


def page_of(request, queryset, serialize):
    try:
        size = min(PAGE_SIZE_MAX, max(1, int(request.GET.get("size", 20))))
        number = max(1, int(request.GET.get("page", 1)))
    except ValueError:
        size, number = 20, 1
    paginator = Paginator(queryset, size)
    page = paginator.get_page(number)
    return {"items": [serialize(item) for item in page.object_list], "total": paginator.count, "page": page.number,
            "size": size}


def iso(value):
    return value.isoformat() if value else None


# ---------------------------------------------------------------- Хувиргагч

def player_row(player):
    return {
        "id": player.pk,
        "name": player.name,
        "phone": player.phone,
        "free_used": player.free_used,
        "credits": player.credits,
        "gold": player.gold,
        "gold_locked": player.locked_gold,
        "gold_won": player.won_gold,
        "gold_bonus": player.bonus_gold,
        "blocked": player.blocked,
        "created": iso(player.created),
        "provinces": getattr(player, "province_total", None),
        "paid_total": getattr(player, "paid_total", None) or 0,
        "last_seen": iso(getattr(player, "last_seen", None)),
    }


def room_row(room):
    return {
        "id": room.pk,
        "province": room.province,
        "status": room.status,
        "phase": logic.room_phase(room),
        "host": {"id": room.host_id, "name": room.host.name, "phone": room.host.phone},
        "guest": {"id": room.guest_id, "name": room.guest.name, "phone": room.guest.phone} if room.guest_id else None,
        "battle": room.battle,
        "host_stake": room.host_stake,
        "guest_stake": room.guest_stake,
        "recruits": room.recruits,
        "closed_reason": room.closed_reason,
        "created": iso(room.created),
        "closed": iso(room.closed),
        "host_seen": iso(room.host_seen),
    }


def invoice_row(invoice):
    return {
        "id": invoice.pk,
        "player": {"id": invoice.player_id, "name": invoice.player.name, "phone": invoice.player.phone},
        "kind": invoice.kind,
        "amount": invoice.amount,
        "credits": invoice.credits,
        "gold": invoice.gold,
        "status": invoice.status,
        "applied": invoice.applied,
        "platform": invoice.platform,
        "qpay_invoice_id": invoice.qpay_invoice_id,
        "qpay_payment_id": invoice.qpay_payment_id,
        "created": iso(invoice.created),
        "paid_at": iso(invoice.paid_at),
    }


def cashout_row(cashout):
    return {
        "id": cashout.pk,
        "player": {"id": cashout.player_id, "name": cashout.player.name, "phone": cashout.player.phone,
                   "won": cashout.player.won_gold},
        "gold": cashout.gold,
        "amount": cashout.amount,
        "rate": float(cashout.rate),
        "bank": cashout.bank,
        "account": cashout.account,
        "holder": cashout.holder,
        "status": cashout.status,
        "note": cashout.note,
        "handled_by": cashout.handled_by,
        "created": iso(cashout.created),
        "verified_at": iso(cashout.verified_at),
        "closed_at": iso(cashout.closed_at),
    }


def event_row(event):
    return {
        "id": event.pk,
        "player": {"id": event.player_id, "name": event.player.name, "phone": event.player.phone} if event.player_id else None,
        "kind": event.kind,
        "data": event.data,
        "device_id": event.device_id,
        "ip": event.ip,
        "created": iso(event.created),
    }


def settings_json(settings):
    return {
        "free_provinces": settings.free_provinces,
        "pack_size": settings.pack_size,
        "pack_price": settings.pack_price,
        "device_change_price": settings.device_change_price,
        "province_count": settings.province_count,
        "capital_index": settings.capital_index,
        "max_devices": settings.max_devices,
        "capital_price": settings.capital_price,
        "start_gold": settings.start_gold,
        "price_damage": settings.price_damage,
        "price_fire_rate": settings.price_fire_rate,
        "price_magazine": settings.price_magazine,
        "price_health": settings.price_health,
        "price_soldier": settings.price_soldier,
        "skin_price": settings.skin_price,
        "skin_price_iron": settings.skin_price_iron,
        "coop_reward": settings.coop_reward,
        "coop_daily_limit": settings.coop_daily_limit,
        "room_fee": settings.room_fee,
        "room_stake": settings.room_stake,
        "stage_bonus": settings.stage_bonus,
        "stage_min_minutes": settings.stage_min_minutes,
        "battle_pair_limit": settings.battle_pair_limit,
        "gold_packs": [{"gold": p["gold"], "price": p["price"]} for p in logic.gold_packs()],
        "cashout_enabled": settings.cashout_enabled,
        "cashout_min": settings.cashout_min,
        "cashout_rate": float(settings.cashout_rate),
    }


def players_annotated():
    return Player.objects.annotate(
        province_total=Count("provinces", distinct=True),
        paid_total=Sum("invoices__amount", filter=Q(invoices__status=Invoice.PAID)),
    )


# ---------------------------------------------------------------- Нэвтрэлт

@staff_api(["POST"], auth=False)
def login(request):
    ip = client_ip(request)
    since = timezone.now() - timedelta(minutes=15)
    if Event.objects.filter(kind="staff_login_failed", ip=ip, created__gte=since).count() >= LOGIN_FAILURES_LIMIT:
        raise ManageError("Хэт олон удаа буруу оруулсан. 15 минутын дараа оролдоно уу", 429)
    username = str(request.json.get("username", ""))
    user = authenticate(request, username=username, password=str(request.json.get("password", "")))
    if user is None or not user.is_staff:
        logic.log_event(None, "staff_login_failed", {"username": username[:60]}, "", ip)
        raise ManageError("Нэр эсвэл нууц үг буруу байна", 401)
    logic.log_event(None, "staff_login", {"username": user.username}, "", ip)
    token = signing.dumps({"uid": user.pk}, salt=TOKEN_SALT)
    return ok(token=token, user={"username": user.username, "superuser": user.is_superuser})


@staff_api(["GET"])
def me(request):
    return ok(user={"username": request.staff.username, "superuser": request.staff.is_superuser})


# ---------------------------------------------------------------- Хянах самбар

ONLINE_WINDOW = timedelta(minutes=3)


@staff_api(["GET"])
def dashboard(request):
    now = timezone.now()
    today = timezone.localtime(now).replace(hour=0, minute=0, second=0, microsecond=0)
    days = 30
    start = today - timedelta(days=days - 1)
    paid = Invoice.objects.filter(status=Invoice.PAID)

    def revenue(qs):
        return qs.aggregate(total=Sum("amount"))["total"] or 0

    registrations = {row["day"]: row["n"] for row in Player.objects.filter(created__gte=start)
                     .annotate(day=TruncDate("created")).values("day").annotate(n=Count("id"))}
    income = {row["day"]: row["total"] for row in paid.filter(paid_at__gte=start)
              .annotate(day=TruncDate("paid_at")).values("day").annotate(total=Sum("amount"))}
    active = {row["day"]: row["n"] for row in Event.objects.filter(created__gte=start, player__isnull=False)
              .annotate(day=TruncDate("created")).values("day").annotate(n=Count("player", distinct=True))}
    series = []
    for i in range(days):
        day = (start + timedelta(days=i)).date()
        series.append({"day": day.isoformat(), "registrations": registrations.get(day, 0), "revenue": income.get(day, 0),
                       "active": active.get(day, 0)})

    provinces = {row["province"]: row["n"] for row in UnlockedProvince.objects.values("province").annotate(n=Count("id"))}
    starts = {}
    for row in Event.objects.filter(kind="province_start").values("data"):
        province = row["data"].get("province") if isinstance(row["data"], dict) else None
        if isinstance(province, int):
            starts[province] = starts.get(province, 0) + 1
    settings = GameSettings.load()
    platforms = {row["platform"] or "?": row["n"] for row in Device.objects.filter(active=True)
                 .values("platform").annotate(n=Count("id"))}

    # Одоо онлайн: нэвтэрсэн тоглоом минут тутам account/ асуудаг (токены last_used шинэчлэгдэнэ). Сүүлийн
    # ONLINE_WINDOW-д хүсэлт илгээсэн тоглоомын токенууд (вэбийн Gold цэнэглэх хуудасны токен орохгүй).
    online_tokens = AuthToken.objects.filter(last_used__gte=now - ONLINE_WINDOW, revoked=False).exclude(device_id=logic.WEB_DEVICE)
    online_devices = list(online_tokens.values_list("device_id", flat=True))
    online_platforms = {row["platform"] or "?": row["n"] for row in Device.objects.filter(device_id__in=online_devices)
                        .values("platform").annotate(n=Count("id"))}
    open_rooms = Room.objects.filter(status=Room.OPEN)

    return ok(
        totals={
            "players": Player.objects.count(),
            "players_today": Player.objects.filter(created__gte=today).count(),
            "active_24h": Event.objects.filter(created__gte=now - timedelta(hours=24), player__isnull=False)
            .values("player").distinct().count(),
            "revenue_total": revenue(paid),
            "revenue_today": revenue(paid.filter(paid_at__gte=today)),
            "revenue_30d": revenue(paid.filter(paid_at__gte=start)),
            "paying_players": paid.values("player").distinct().count(),
            "invoices_pending": Invoice.objects.filter(status=Invoice.PENDING).count(),
            "blocked": Player.objects.filter(blocked=True).count(),
            "online": online_tokens.values("player").distinct().count(),
            "active_1h": AuthToken.objects.filter(last_used__gte=now - timedelta(hours=1)).exclude(device_id=logic.WEB_DEVICE)
            .values("player").distinct().count(),
            "rooms_open": open_rooms.count(),
            "battles_live": open_rooms.filter(guest__isnull=False).count(),
        },
        online_platforms=online_platforms,
        series=series,
        provinces=[{"province": i, "unlocked": provinces.get(i, 0), "starts": starts.get(i, 0)}
                   for i in range(settings.province_count)],
        platforms=platforms,
        recent_payments=[invoice_row(i) for i in paid.select_related("player").order_by("-paid_at")[:8]],
    )


# ---------------------------------------------------------------- Тоглогчид

@staff_api(["GET"])
def players(request):
    qs = players_annotated()
    search = request.GET.get("q", "").strip()
    if search:
        qs = qs.filter(Q(name__icontains=search) | Q(phone__icontains=search))
    status = request.GET.get("status")
    if status == "blocked":
        qs = qs.filter(blocked=True)
    elif status == "paying":
        qs = qs.filter(invoices__status=Invoice.PAID).distinct()
    ordering = {"created": "created", "-created": "-created", "credits": "credits", "-credits": "-credits",
                "gold": "gold", "-gold": "-gold",
                "paid": "paid_total", "-paid": "-paid_total", "name": "name", "-name": "-name"}
    qs = qs.order_by(ordering.get(request.GET.get("sort", "-created"), "-created"), "-id")
    return ok(**page_of(request, qs, player_row))


def player_detail_json(player):
    settings = GameSettings.load()
    data = player_row(players_annotated().get(pk=player.pk))
    last = player.events.order_by("-created").first()
    data.update(
        free_left=max(0, settings.free_provinces - player.free_used),
        last_seen=iso(last.created) if last else None,
        progress_updated=iso(player.progress_updated),
        has_progress=bool(player.progress),
        unlocked=[{"province": u.province, "source": u.source, "created": iso(u.created)}
                  for u in player.provinces.order_by("province")],
        devices=[{"id": d.pk, "device_id": d.device_id, "platform": d.platform, "active": d.active, "bound_at": iso(d.bound_at)}
                 for d in player.devices.order_by("-active", "-bound_at")],
        sessions=player.tokens.count(),
        invoices=[invoice_row(i) for i in player.invoices.select_related("player").order_by("-created")[:50]],
        upgrades={name: getattr(player, field) for name, (field, _, _) in logic.UPGRADES.items()},
        gold_history=[{"amount": t.amount, "balance": t.balance, "reason": t.reason, "data": t.data, "created": iso(t.created)}
                      for t in player.gold_transactions.all()[:50]],
    )
    return data


def get_player(player_id):
    player = Player.objects.filter(pk=player_id).first()
    if player is None:
        raise ManageError("Тоглогч олдсонгүй", 404)
    return player


@staff_api(["GET", "PATCH"])
def player_detail(request, player_id):
    player = get_player(player_id)
    if request.method == "PATCH":
        changes = {}
        j = request.json
        with transaction.atomic():
            player = Player.objects.select_for_update().get(pk=player.pk)
            if "name" in j:
                name = str(j["name"]).strip()
                if not 2 <= len(name) <= 60:
                    raise ManageError("Нэр 2-60 тэмдэгт байна")
                changes["name"] = [player.name, name]
                player.name = name
            for field in ("credits", "free_used"):
                if field in j:
                    value = j[field]
                    if not isinstance(value, int) or not 0 <= value <= 1000:
                        raise ManageError("Тоо буруу байна")
                    changes[field] = [getattr(player, field), value]
                    setattr(player, field, value)
            if "gold" in j:
                # Gold-ийг шууд бичихгүй: зөрүүг бүртгэлд "admin" гүйлгээгээр
                value = j["gold"]
                if not isinstance(value, int) or isinstance(value, bool) or not 0 <= value <= 10_000_000:
                    raise ManageError("Gold буруу байна")
                if value != player.gold:
                    changes["gold"] = [player.gold, value]
                    logic.change_gold(player, value - player.gold, GoldTransaction.ADMIN,
                                      f"admin:{player.pk}:{secrets.token_hex(8)}",
                                      {"staff": request.staff.username})
            if "blocked" in j:
                changes["blocked"] = [player.blocked, bool(j["blocked"])]
                player.blocked = bool(j["blocked"])
                if player.blocked:
                    player.tokens.all().delete()
            player.save()
        audit(request, player, "player_updated", {"changes": changes})
    return ok(player=player_detail_json(player))


@staff_api(["POST"])
def player_province(request, player_id):
    player = get_player(player_id)
    province = request.json.get("province")
    settings = GameSettings.load()
    if not isinstance(province, int) or not 0 <= province < settings.province_count:
        raise ManageError("Аймаг буруу байна")
    if request.json.get("action") == "remove":
        player.provinces.filter(province=province).delete()
        audit(request, player, "province_removed", {"province": province})
    else:
        UnlockedProvince.objects.get_or_create(player=player, province=province, defaults={"source": UnlockedProvince.ADMIN})
        audit(request, player, "province_added", {"province": province})
    return ok(player=player_detail_json(player))


@staff_api(["POST"])
def player_device_release(request, player_id, device_pk):
    player = get_player(player_id)
    device = player.devices.filter(pk=device_pk).first()
    if device is None:
        raise ManageError("Төхөөрөмж олдсонгүй", 404)
    audit(request, player, "device_released", {"device": device.device_id})
    AuthToken.objects.filter(player=player, device_id=device.device_id).delete()
    device.delete()
    return ok(player=player_detail_json(player))


@staff_api(["POST"])
def player_logout(request, player_id):
    player = get_player(player_id)
    count = player.tokens.count()
    player.tokens.all().delete()
    audit(request, player, "sessions_cleared", {"count": count})
    return ok(player=player_detail_json(player))


@staff_api(["POST"])
def player_password(request, player_id):
    player = get_player(player_id)
    password = str(request.json.get("password", ""))
    if len(password) < logic.MIN_PASSWORD:
        raise ManageError(f"Нууц үг {logic.MIN_PASSWORD}-аас дээш тэмдэгт байна")
    player.set_password(password)
    player.save(update_fields=["password"])
    player.tokens.all().delete()
    audit(request, player, "password_set")
    return ok(player=player_detail_json(player))


# ---------------------------------------------------------------- Төлбөр

@staff_api(["GET"])
def invoices(request):
    qs = Invoice.objects.select_related("player")
    for field in ("status", "kind"):
        value = request.GET.get(field)
        if value:
            qs = qs.filter(**{field: value})
    if request.GET.get("player"):
        qs = qs.filter(player_id=request.GET["player"])
    search = request.GET.get("q", "").strip()
    if search:
        qs = qs.filter(Q(player__name__icontains=search) | Q(player__phone__icontains=search) |
                       Q(qpay_invoice_id__icontains=search) | Q(qpay_payment_id__icontains=search))
    summary = qs.filter(status=Invoice.PAID).aggregate(total=Sum("amount"), count=Count("id"))
    data = page_of(request, qs.order_by("-created"), invoice_row)
    return ok(**data, paid_total=summary["total"] or 0, paid_count=summary["count"])


def get_invoice(invoice_id):
    invoice = Invoice.objects.select_related("player").filter(pk=invoice_id).first()
    if invoice is None:
        raise ManageError("Нэхэмжлэх олдсонгүй", 404)
    return invoice


@staff_api(["POST"])
def invoice_check(request, invoice_id):
    invoice = logic.refresh_invoice(get_invoice(invoice_id), "admin_check")
    return ok(invoice=invoice_row(Invoice.objects.select_related("player").get(pk=invoice.pk)))


@staff_api(["POST"])
def invoice_mark_paid(request, invoice_id):
    invoice = get_invoice(invoice_id)
    if invoice.status == Invoice.CANCELLED and not request.json.get("force"):
        raise ManageError("Цуцлагдсан нэхэмжлэх. Итгэлтэй бол дахин баталгаажуулна уу", 409)
    audit(request, invoice.player, "invoice_marked_paid", {"invoice": invoice.pk, "note": str(request.json.get("note", ""))[:200]})
    logic.apply_payment(invoice.pk, f"manual:{request.staff.username}", "admin")
    return ok(invoice=invoice_row(Invoice.objects.select_related("player").get(pk=invoice.pk)))


# ---------------------------------------------------------------- Gold татах

@staff_api(["GET"])
def cashouts(request):
    logic.expire_cashouts()
    qs = Cashout.objects.select_related("player")
    if request.GET.get("status"):
        qs = qs.filter(status=request.GET["status"])
    if request.GET.get("player"):
        qs = qs.filter(player_id=request.GET["player"])
    search = request.GET.get("q", "").strip()
    if search:
        qs = qs.filter(Q(player__name__icontains=search) | Q(player__phone__icontains=search) | Q(account__icontains=search) |
                       Q(holder__icontains=search))
    pending = Cashout.objects.filter(status=Cashout.PENDING).aggregate(total=Sum("amount"), count=Count("id"))
    paid = Cashout.objects.filter(status=Cashout.PAID).aggregate(total=Sum("amount"))
    data = page_of(request, qs.order_by("-created"), cashout_row)
    return ok(**data, pending_total=pending["total"] or 0, pending_count=pending["count"], paid_total=paid["total"] or 0)


def get_cashout(cashout_id):
    cashout = Cashout.objects.select_related("player").filter(pk=cashout_id).first()
    if cashout is None:
        raise ManageError("Хүсэлт олдсонгүй", 404)
    return cashout


@staff_api(["POST"])
def cashout_paid(request, cashout_id):
    """Админ банкаар шилжүүлсний дараа: олгосон гэж тэмдэглэнэ (note: гүйлгээний дугаар г.м.)."""
    cashout = get_cashout(cashout_id)
    note = str(request.json.get("note", ""))[:200]
    logic.admin_pay_cashout(cashout, request.staff.username, note)
    audit(request, cashout.player, "cashout_paid", {"cashout": cashout.pk, "amount": cashout.amount, "note": note})
    return ok(cashout=cashout_row(get_cashout(cashout_id)))


@staff_api(["POST"])
def cashout_reject(request, cashout_id):
    """Татгалзана: Gold тоглогчид буцна. note: шалтгаан (тоглогчид харагдана)."""
    cashout = get_cashout(cashout_id)
    note = str(request.json.get("note", "")).strip()[:200]
    if not note:
        raise ManageError("Татгалзсан шалтгаанаа бичнэ үү")
    logic.admin_reject_cashout(cashout, request.staff.username, note)
    audit(request, cashout.player, "cashout_rejected", {"cashout": cashout.pk, "note": note})
    return ok(cashout=cashout_row(get_cashout(cashout_id)))


# ---------------------------------------------------------------- Лог

@staff_api(["GET"])
def rooms(request):
    qs = Room.objects.select_related("host", "guest")
    if request.GET.get("status") in (Room.OPEN, Room.CLOSED):
        qs = qs.filter(status=request.GET["status"])
    return ok(**page_of(request, qs.order_by("-created"), room_row))


@staff_api(["GET"])
def events(request):
    qs = Event.objects.select_related("player")
    kind = request.GET.get("kind", "").strip()
    if kind:
        qs = qs.filter(kind__startswith=kind) if kind.endswith("_") else qs.filter(kind=kind)
    if request.GET.get("player"):
        qs = qs.filter(player_id=request.GET["player"])
    search = request.GET.get("q", "").strip()
    if search:
        qs = qs.filter(Q(player__name__icontains=search) | Q(player__phone__icontains=search) | Q(device_id__icontains=search) |
                       Q(ip__icontains=search))
    return ok(**page_of(request, qs.order_by("-created", "-id"), event_row))


@staff_api(["GET"])
def event_kinds(request):
    return ok(kinds=list(Event.objects.values_list("kind", flat=True).distinct().order_by("kind")))


# ---------------------------------------------------------------- Тохиргоо

@staff_api(["GET", "PUT"])
def settings_view(request):
    settings = GameSettings.load()
    if request.method == "PUT":
        if not request.staff.is_superuser:
            raise ManageError("Зөвхөн ерөнхий админ үнэ, тохиргоо өөрчилнө", 403)
        limits = {"free_provinces": (0, 22), "pack_size": (1, 22), "pack_price": (0, 1_000_000),
                  "device_change_price": (0, 1_000_000), "max_devices": (1, 5),
                  "capital_price": (0, 1_000_000), "start_gold": (0, 100_000),
                  "price_damage": (0, 100_000), "price_fire_rate": (0, 100_000), "price_magazine": (0, 100_000),
                  "price_health": (0, 100_000), "price_soldier": (0, 100_000), "skin_price": (0, 100_000), "skin_price_iron": (0, 100_000),
                  "coop_reward": (0, 10_000),
                  "coop_daily_limit": (0, 100), "room_fee": (0, 10_000),
                  "room_stake": (0, 10_000), "stage_bonus": (0, 10_000), "stage_min_minutes": (0, 240),
                  "battle_pair_limit": (0, 100)}
        before = settings_json(settings)
        for field, (low, high) in limits.items():
            if field in request.json:
                value = request.json[field]
                if not isinstance(value, int) or not low <= value <= high:
                    raise ManageError("Утга буруу байна")
                setattr(settings, field, value)
        if "cashout_enabled" in request.json:
            if not isinstance(request.json["cashout_enabled"], bool):
                raise ManageError("Утга буруу байна")
            settings.cashout_enabled = request.json["cashout_enabled"]
        if "cashout_min" in request.json:
            value = request.json["cashout_min"]
            if not isinstance(value, int) or isinstance(value, bool) or not 1 <= value <= 10_000_000:
                raise ManageError("Татах доод хэмжээ буруу байна")
            settings.cashout_min = value
        if "cashout_rate" in request.json:
            value = request.json["cashout_rate"]
            if not isinstance(value, (int, float)) or isinstance(value, bool) or not 0.01 <= value <= 1000:
                raise ManageError("Ханш 0.01-1000 ₮ байна")
            settings.cashout_rate = Decimal(str(round(value, 2)))
        if "gold_packs" in request.json:
            packs = request.json["gold_packs"]
            if not isinstance(packs, list) or not 1 <= len(packs) <= 8:
                raise ManageError("Gold-ийн багц 1-8 байна")
            clean = []
            for pack in packs:
                gold, price = (pack.get("gold"), pack.get("price")) if isinstance(pack, dict) else (None, None)
                if not all(isinstance(v, int) and not isinstance(v, bool) for v in (gold, price)) or \
                        not 1 <= gold <= 1_000_000 or not 100 <= price <= 10_000_000:
                    raise ManageError("Багцын Gold, үнэ буруу байна")
                clean.append([gold, price])
            settings.gold_packs = clean
        settings.save()
        audit(request, None, "settings_updated", {"before": before, "after": settings_json(settings)})
    return ok(settings=settings_json(settings))
